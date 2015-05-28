#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <unistd.h>
#include <error.h>

#include "libio.h"

/* libio-related applets */

static int libio_presets(int argc, char *argv[])
{
	const char *key = NULL;
	int j;

	if (argc > 1)
		for (j = 1; j < argc; ++j)
			printf("%s=%s\n", argv[j], libio_get_preset(argv[j]));
	else
		while ((key = libio_next_preset(key)) != NULL)
			printf("%s=%s\n", key, libio_get_preset(key));
	return 0;
}

static int libio_consts(int argc, char *argv[])
{
	const char *key = NULL;
	int j;

	if (argc > 1)
		for (j = 1; j < argc; ++j)
			printf("%s=%lf\n", argv[j], libio_const(argv[j]));
	else
		while ((key = libio_next_const(key)) != NULL)
			printf("%s=%lf\n", key, libio_const(key));
	return 0;
}

static int netiomsg_sendto(int argc, char *argv[])
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s SOCKET MESSAGE\n", argv[0]);
		exit(1);
	}
	return (netio_send_msg(argv[1], argv[2]) >= 0) ? 0 : 1;
}

static int netiomsg_request(int argc, char *argv[])
{
	int id;
	const char *msg;

	if (argc < 3) {
		fprintf(stderr, "usage: %s SOCKET MESSAGE\n", argv[0]);
		exit(1);
	}

	id = netio_send_msg(argv[1], argv[2]);
	if (id < 0)
		/* failed */
		return 1;

	alarm(2);
	while (1) {
		while (netio_msg_pending()) {
			/* fetch message before testing ID! */
			msg = netio_recv_msg();
			if (netio_msg_id() == id) {
				printf("%s\n", msg);
				return 0;
			}
		}

		if (libio_wait() < 0)
			break;
	}
	return 1;
}

__attribute__((constructor))
static void add_default_applets(void)
{
	register_applet("presets", libio_presets);
	register_applet("consts", libio_consts);
	register_applet("sendto", netiomsg_sendto);
	register_applet("request", netiomsg_request);
}
