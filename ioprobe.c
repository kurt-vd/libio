#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <unistd.h>
#include <error.h>
#include <getopt.h>

#include <libevt.h>
#include "libio.h"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": get/set/modify libio parameter\n"
	"Usage: " NAME " [OPTIONS] <PARAM>\n"
	"	" NAME " [OPTIONS] <PARAM> <VALUE>\n"
	"	" NAME " [OPTIONS] <PARAM> [i|d] <VALUE>\n"
	"\n"
	"Options:\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Be more verbose\n"
	" -f, --follow		Keep reading\n"
	;

#ifdef _GNU_SOURCE
static const struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },
	{ "follow", no_argument, NULL, 'f', },
	{ },
};

#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif

static const char optstring[] = "+?Vvsf";

static struct args {
	int verbose;
} s;

static int ioprobe(int argc, char *argv[])
{
	int opt;
	/* parameter indices */
	int param, follow = 0;
	int actionchar = 'g'; /* '+' | '-' */
	double modvalue = 0;

	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s\n", NAME, LOCALVERSION);
		return 0;
	case 'v':
		++s.verbose;
		break;
	case 'f':
		follow = 1;
		break;
	case '?':
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}
	libio_set_trace(s.verbose);

	if (!argv[optind]) {
		fputs(help_msg, stderr);
		exit(1);
	}

	param = create_iopar(argv[optind++]);
	if (argv[optind]) {
		actionchar = islower(*argv[optind]) ? *argv[optind++] : 's';
		modvalue = strtod(argv[optind] ?: "1", 0);
	}

	if (!follow)
	schedule_itimer(2);

	/* main ... */
	while (1) {
		if (iopar_dirty(param)) {
			double value = get_iopar(param, 0);
			switch (actionchar) {
			default:
			case 'g':
				printf("%.3f\n", value);
				if (!follow)
					return 0;
				break;
			case 'd':
				modvalue = -modvalue;
			case 'i':
				modvalue = value+modvalue;
			case 's':
				if (set_iopar(param, modvalue) < 0)
					error(1, errno, "set output");
				actionchar = 'g';
				break;
			}
		}

		/* enter sleep */
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

__attribute__((constructor))
static void init(void)
{
	register_applet(NAME, ioprobe);
}
