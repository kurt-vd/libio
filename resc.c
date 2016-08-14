#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>

#include "_libio.h"

/*
 * resource control:
 *
 * Clients can ask for an amount, and need to update regularly.
 * When nothing is received, client is deleted.
 * This file implements the client
 */

static char rdat[1024];
static int clsock = -1;

__attribute__((destructor))
static void cleanup(void)
{
	if (clsock >= 0)
		close(clsock);
	clsock = -1;
}

static int make_local_sock(void)
{
	int namelen;
	struct sockaddr_storage name;
	int ret;

	if (clsock >= 0)
		return 0;
	sprintf(rdat, "@libio-rclient-%i", getpid());
	namelen = netio_strtosockname(rdat, &name, AF_UNIX);
	if (namelen <= 0) {
		elog(LOG_ERR, 0, "bad LISTENSPEC '%s'", rdat);
		return -1;
	}
	ret = clsock = socket(AF_UNIX, SOCK_DGRAM/* | SOCK_CLOEXEC*/, 0);
	if (ret < 0) {
		elog(LOG_WARNING, errno, "socket %i dgram 0", (*(struct sockaddr*)&name).sa_family);
		return -1;
	}
	fcntl(clsock, F_SETFD, fcntl(clsock, F_GETFD) | FD_CLOEXEC);

	ret = bind(clsock, (void *)&name, namelen);
	if (ret < 0) {
		elog(LOG_WARNING, errno, "bind '%s'", rdat);
		close(clsock);
		clsock = -1;
		return -1;
	}
	return 0;
}

int libio_take_resource(const char *uri, const char *cid, double value)
{
	int ret;
	int namelen;
	struct sockaddr_storage name;

	if (!uri)
		/* ignore, make it easy to use */
		return 0;

	namelen = netio_strtosockname(uri, &name, AF_UNIX);
	if (namelen <= 0) {
		elog(LOG_ERR, 0, "bad LISTENSPEC '%s'", uri);
		return -1;
	}
	make_local_sock();

	/* send request */
	sprintf(rdat, "*need %s %lf", cid, value);
	ret = sendto(clsock, rdat, strlen(rdat), 0, (void *)&name, namelen);
	if (ret < 0) {
		elog(LOG_WARNING, errno, "sendto");
		return ret;
	}
	/* wait for response */
	ret = recvfrom(clsock, rdat, sizeof(rdat)-1, 0, (void *)&name, (unsigned int *)&namelen);
	if (ret < 0) {
		elog(LOG_WARNING, errno, "recvfrom");
		return ret;
	}
	/* null terminator for string operations */
	rdat[ret] = 0;
	/* parse */
	return strncmp("*ack", rdat, 4) ? -1 : 0;
}
