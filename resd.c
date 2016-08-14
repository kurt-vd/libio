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

#include "lib/libt.h"
#include "_libio.h"

/*
 * resource control:
 *
 * Clients can ask for an amount, and need to update regularly.
 * When nothing is received, client is deleted.
 * This file implements the daemon
 */

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": Manage system-wide resource counters\n"
	"Usage: " NAME " [OPTIONS] LISTENSPEC [MAX]\n"
	"\n"
	"Options:\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Be more verbose\n"
	"\n"
	" LISTENSPEC is advised to be unix:...\n"
	" MAX defaults to 1.0\n"
	;

#ifdef _GNU_SOURCE
static const struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },
	{ "listen", required_argument, NULL, 'l', },
	{ },
};

#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif

static const char optstring[] = "?Vvl:";

struct remote {
	struct remote *next, *prev;
	double value;
	char *cid; /* client id */
	int namelen;
	uint8_t name[2];
};

/* double-linked list tric */
static inline struct remote *fakeremote(struct remote **root)
{
	return (struct remote *)(((char *)root) - offsetof(struct remote, next));
}

static struct remote *remotes;
static int verbose;
static double maxavail = 1.0;

static char rdat[1024];

static struct remote *find_remote(const void *name, int namelen, const char *cid)
{
	struct remote *ptr;

	for (ptr = remotes; ptr; ptr = ptr->next) {
		if (namelen == ptr->namelen && !memcmp(name, ptr->name, namelen) &&
				!strcmp(cid, ptr->cid))
			return ptr;
	}
	return NULL;
}

static struct remote *create_remote(const void *name, int namelen, const char *cid)
{
	struct remote *ptr;

	ptr = malloc(sizeof(*ptr) + namelen);
	if (!ptr)
		elog(LOG_ERR, errno, "malloc");
	memset(ptr, 0, sizeof(*ptr));

	ptr->namelen = namelen;
	memcpy(ptr->name, name, namelen);
	ptr->cid = strdup(cid);

	/* settle double linked list */
	ptr->next = remotes;
	ptr->prev = fakeremote(&remotes);
	if (ptr->next) {
		ptr->prev = ptr->next->prev;
		ptr->next->prev = ptr;
	}
	ptr->prev->next = ptr;
	return ptr;
}

static void lost_remote(void *dat)
{
	struct remote *ptr = dat;

	/* linked-list */
	if (ptr->next)
		ptr->next->prev = ptr->prev;
	if (ptr->prev)
		ptr->prev->next = ptr->next;

	/* accounting */
	maxavail += ptr->value;
	elog(LOG_NOTICE, 0, "'%s' lost, remain %lf",
			ptr->cid, maxavail);

	/* free memory */
	free(ptr->cid);
	free(ptr);
}

static int rescontrol(int argc, char *argv[])
{
	int opt, ret, sock, saved_umask;
	char *tok, *cid, *sockname;
	struct remote *remote;
	struct sockaddr_storage name;
	int namelen = 0;
	struct pollfd pfd;
	double value;

	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s\n", NAME, LOCALVERSION);
		return 0;
	case 'v':
		++verbose;
		break;

	case '?':
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}
	libio_set_trace(verbose);

	if (optind >= argc)
		elog(LOG_CRIT, 0, "no LISTENSPEC");

	sockname = argv[optind++];
	namelen = netio_strtosockname(sockname, (void *)&name, AF_UNIX);
	if (namelen <= 0)
		elog(LOG_CRIT, 0, "bad LISTENSPEC '%s'", sockname);

	if (optind < argc)
		maxavail = strtod(argv[optind++], NULL);

	/* open socket */
	sock = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sock < 0)
		elog(LOG_CRIT, errno, "socket %i dgram 0", AF_UNIX);
	fcntl(sock, F_SETFD, fcntl(sock, F_GETFD) | FD_CLOEXEC);

	saved_umask = umask(0);
	ret = bind(sock, (void *)&name, namelen);
	umask(saved_umask);
	if (ret < 0)
		elog(LOG_CRIT, errno, "bind '%s'", sockname);

	pfd.fd = sock;
	pfd.events = POLLIN;

	/* main ... */
	while (1) {
		libt_flush();
		ret = poll(&pfd, 1, libt_get_waittime());
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			elog(LOG_ERR, errno, "poll");
			exit(1);
		}
		if (!ret)
			continue;
		/* read new packet */
		namelen = sizeof(name);
		ret = recvfrom(sock, &rdat, sizeof(rdat)-1, 0, (void *)&name, (unsigned int *)&namelen);
		if ((rdat < 0 && errno != EINTR) || !ret)
			elog(LOG_ERR, ret ? errno : 0, "recvfrom");

		rdat[ret] = 0;
		tok = strtok(rdat, " \t");
		if (!strcmp(tok, "*need")) {
			cid = strtok(NULL, " \t");
			value = strtod(strtok(NULL, " \t") ?: "", NULL);

			remote = find_remote(&name, namelen, cid);
			if (!remote) {
				remote = create_remote(&name, namelen, cid);
				remote->value = 0;
			}

			/* postpone timeout */
			libt_add_timeout(1, lost_remote, remote);

			if (value > (maxavail + remote->value)) {
				sprintf(rdat, "*nack %lf", remote->value);
			} else {
				maxavail -= value - remote->value;
				if (value != remote->value)
				elog(LOG_NOTICE, 0, "'%s' need %lf (was %lf), remain %lf",
						remote->cid, value, remote->value, maxavail);
				remote->value = value;
				sprintf(rdat, "*ack %lf", remote->value);
			}
			ret = sendto(sock, rdat, strlen(rdat), 0, (void *)&name, namelen);
			if (ret < 0)
				elog(LOG_WARNING, errno, "sendto");
		}
	}
	return 0;
}

__attribute__((constructor))
static void init(void)
{
	register_applet(NAME, rescontrol);
}
