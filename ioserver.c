#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <getopt.h>

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

static int ioserver(int argc, char *argv[])
{
	int opt;
	struct link *lnk;
	char *sep, *tmpstr;

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
			elog(LOG_CRIT, 0, "bind %s failed", optarg);
		break;

	case '?':
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}
	libio_set_trace(s.verbose);

	for (; optind < argc; ++optind) {
		tmpstr = strdup(argv[optind]);
		sep = strchr(tmpstr, '=');
		if (!sep)
			continue;
		*sep++ = 0;
		lnk = zalloc(sizeof(*lnk));
		lnk->a = create_ioparf("netio:%s", tmpstr);
		lnk->b = create_iopar(sep);
		lnk->next = s.links;
		s.links = lnk;
		free(tmpstr);
	}

	/* main ... */
	while (1) {
		for (lnk = s.links; lnk; lnk = lnk->next) {
			if (iopar_dirty(lnk->a)) {
				set_iopar(lnk->b, get_iopar(lnk->a));
				/* TODO: warn if failed */
				/* write back in case the real value didn't change */
				set_iopar(lnk->a, get_iopar(lnk->b));
			} else if (iopar_dirty(lnk->b))
				set_iopar(lnk->a, get_iopar(lnk->b));
		}

		if (libio_wait() < 0)
			break;
	}
	return 0;
}

__attribute__((constructor))
static void init(void)
{
	register_applet(NAME, ioserver);
}
