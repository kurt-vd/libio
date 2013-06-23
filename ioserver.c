#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <error.h>
#include <getopt.h>

#include <libevt.h>
#include "_libio.h"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": Serve IO's\n"
	"Usage: " NAME " [OPTIONS] NAME=IOSPEC [...]\n"
	"\n"
	"Options:\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Be more verbose\n"
	" -l, --listen=SPEC	Listen on SPEC\n"
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

struct link {
	struct link *next;
	int a, b;
};

static struct args {
	int verbose;
	struct link *links;
} s;

int main(int argc, char *argv[])
{
	int opt, j;
	struct link *lnk;

	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s\n", NAME, LOCALVERSION);
		return 0;
	case 'v':
		++s.verbose;
		break;
	case 'l':
		if (libio_bind_net(optarg) < 0)
			error(1, 0, "bind %s failed", optarg);
		break;

	case '?':
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}
	libio_set_trace(s.verbose);

	for (j = optind; j < argc; ++j) {
		lnk = zalloc(sizeof(*lnk));
		lnk->a = create_iopar_type("netio", strtok(argv[j], "="));
		lnk->b = create_iopar(strtok(NULL, "="));
		lnk->next = s.links;
		s.links = lnk;
	}

	/* main ... */
	while (1) {
		for (lnk = s.links; lnk; lnk = lnk->next) {
			if (iopar_dirty(lnk->a)) {
				set_iopar(lnk->b, get_iopar(lnk->a, 0));
				/* TODO: warn if failed */
				/* write back in case the real value didn't change */
				set_iopar(lnk->a, get_iopar(lnk->b, 0));
			} else if (iopar_dirty(lnk->b))
				set_iopar(lnk->a, get_iopar(lnk->b, 0));
		}

		libio_flush();
		if (evt_loop(-1) < 0) {
			if (errno == EINTR)
				continue;
			error(0, errno, "evt_loop");
			break;
		}
	}
	return 0;
}
