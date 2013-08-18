#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <error.h>
#include <getopt.h>

#include <libevt.h>
#include "_libio.h"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": Toggle <LED> on each <INPUT> press\n"
	"Usage: " NAME " [OPTIONS] <INPUT> <LED>\n"
	"     : " NAME " [OPTIONS] <UP> <DOWN> <LED>\n"
	"\n"
	"Options:\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Be more verbose\n"
	" -s, --step=STEP	Step value (default 0.2)\n"
	;

#ifdef _GNU_SOURCE
static const struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },
	{ "step", required_argument, NULL, 's', },
	{ },
};

#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif

static const char optstring[] = "?Vvs:";

static struct args {
	int verbose;
	double step;
} s = {
	.step = 0.125,
};

static int iotoggle(int argc, char *argv[])
{
	int opt;
	int outdev, updev, downdev;

	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s\n", NAME, LOCALVERSION);
		return 0;
	case 'v':
		++s.verbose;
		break;
	case 's':
		s.step = strtod(optarg, NULL);
		break;

	case '?':
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}
	libio_set_trace(s.verbose);

	if ((optind + 2 > argc) || (optind + 3 < argc)) {
		fputs(help_msg, stderr);
		exit(1);
	}

	updev = create_iopar(argv[optind++]);
	downdev = (optind + 2 == argc) ? create_iopar(argv[optind++]) : 0;
	outdev = create_iopar(argv[optind++]);

	/* main ... */
	while (1) {
		if (iopar_dirty(updev) && (get_iopar(updev) > 0)) {
			set_iopar(outdev, get_iopar(outdev) + s.step);
			elog(LOG_INFO, 0, "> %.3f", get_iopar(outdev));
		}
		if ((downdev && iopar_dirty(downdev) &&
					(get_iopar(downdev) > 0)) ||
				(!downdev && iopar_dirty(updev) &&
					(get_iopar(updev) < 1))) {
			set_iopar(outdev, get_iopar(outdev) - s.step);
			if (s.verbose)
				elog(LOG_INFO, 0, "> %.3f", get_iopar(outdev));
		}

		libio_flush();
		if (evt_loop(-1) < 0) {
			if (errno == EINTR)
				continue;
			elog(LOG_ERR, errno, "evt_loop");
			break;
		}
	}
	return 0;
}

__attribute__((constructor))
static void init(void)
{
	register_applet(NAME, iotoggle);
}
