#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <error.h>
#include <getopt.h>

#include "_libio.h"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": dumb control of N ouputs with N inputs\n"
	"Usage: " NAME " [OPTIONS] NAME=OUT [IN [...]] [NAME=OUT ...] ...\n"
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

#define MAX_IN 3
struct link {
	struct link *next;
	int nin;
	/* local params */
	int out, in[MAX_IN];
	/* public params */
	int pub;
};

static struct args {
	int verbose;
	struct link *links;
} s;

static int hadirect(int argc, char *argv[])
{
	int opt, j;
	struct link *lnk;
	char *streq, *tmpstr;

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

	if (optind >= argc) {
		fputs(help_msg, stderr);
		exit(1);
	}

	/* create common input */
	for (; optind < argc; ++optind) {
		tmpstr = strdup(argv[optind]);
		streq = strchr(tmpstr, '=');
		if (streq) {
			/* new entry */
			*streq++ = 0;
			lnk = zalloc(sizeof(*lnk));
			lnk->pub = create_iopar_type("netio", tmpstr);
			lnk->out = create_iopar(streq);
			/* add link */
			lnk->next = s.links;
			s.links = lnk;
		} else if (s.links && (s.links->nin < MAX_IN)) {
			/* add input */
			lnk = s.links;
			lnk->in[lnk->nin++] = create_iopar(tmpstr);
		} else {
			elog(LOG_CRIT, 0, ">%i input for 1 output, or no output defined", MAX_IN);
		}
		free(tmpstr);
	}

	/* main ... */
	while (1) {
		for (lnk = s.links; lnk; lnk = lnk->next) {
			/* PER-link control */
			if (iopar_dirty(lnk->pub)) {
				/* remote command received, process first */
				set_iopar(lnk->out, get_iopar(lnk->pub));
				set_iopar(lnk->pub, get_iopar(lnk->out));
			} else if (iopar_dirty(lnk->out)) {
				set_iopar(lnk->pub, get_iopar(lnk->out));
			}
			for (j = 0; j < lnk->nin; ++j) {
				if (iopar_dirty(lnk->in[j]) &&
						(get_iopar(lnk->in[j]) > 0.5)) {
					/* local button input pressed, toggle */
					set_iopar(lnk->out, !(int)get_iopar(lnk->out));
					set_iopar(lnk->pub, get_iopar(lnk->out));
				}
			}
		}

		if (libio_wait() < 0)
			break;
	}
	return 0;
}

__attribute__((constructor))
static void init(void)
{
	register_applet(NAME, hadirect);
}
