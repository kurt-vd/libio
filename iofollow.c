#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <error.h>
#include <getopt.h>

#include <libevt.h>
#include "libio.h"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": control <LED> or <BACKLIGHT> on input\n"
	"	with keyboard modifications possible\n"
	"Usage: " NAME " [OPTIONS] <INPUT> <OUTPUT>\n"
	"\n"
	"Options:\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Be more verbose\n"
	" -d, --dryrun		Don't actually set the output\n"
	" -s, --slope=SLOPE	SLOPE of conversion\n"
	" -o, --offset=OFFSET	Fixed OFFSET of conversion\n"
	"			The program will export an additional 'offset' parameter\n"
	" -l, --listen=URI	Socket address to listen on\n"
	;

#ifdef _GNU_SOURCE
static const struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },
	{ "dryrun", no_argument, NULL, 'd', },
	{ "slope", required_argument, NULL, 's', },
	{ "offset", required_argument, NULL, 'o', },
	{ "listen", required_argument, NULL, 'l', },
	{ },
};

#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif

static const char optstring[] = "?Vvds:o:l:";

static struct args {
	int verbose;
	int dryrun;
} s;

int main(int argc, char *argv[])
{
	int opt;
	/* parameter indices */
	int indev, outdev, uoffset;
	double slope = 1, offset;

	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s\n", NAME, LOCALVERSION);
		return 0;
	case 'v':
		++s.verbose;
		break;
	case 'd':
		++s.dryrun;
		break;
	case 's':
		slope = strtod(optarg, 0);
		break;
	case 'o':
		offset = strtod(optarg, 0);
		break;
	case 'l':
		libio_bind_net(optarg);
		break;
	case '?':
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}
	libio_set_trace(s.verbose);

	if (optind + 2 > argc) {
		fputs(help_msg, stderr);
		exit(1);
	}

	indev = create_iopar(argv[optind++]);
	outdev = create_iopar(argv[optind++]);
	uoffset = create_iopar("netio:offset");
	iopar_set_writeable(uoffset);
	set_iopar(uoffset, 0);

	/* main ... */
	while (1) {
		if (iopar_dirty(indev) || iopar_dirty(uoffset)) {
			double newvalue = get_iopar(indev, 0) * slope + offset +
				get_iopar(uoffset, 0);
			if (!s.dryrun && (set_iopar(outdev, newvalue) < 0))
				error(1, errno, "set output device %.3lf", newvalue);
			else if (s.dryrun || s.verbose)
				error(0, 0, "%.3f +%.3f > %.3f",
						get_iopar(indev, 0),
						get_iopar(uoffset, 0),
						newvalue);
		}

		/* flush & wait */
		libio_flush();
		if (evt_loop(-1) < 0) {
			error(0, errno, "evt_loop");
			break;
		}
	}
	return 0;
}
