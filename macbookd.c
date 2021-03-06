#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <getopt.h>

#include "libio.h"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": control Backlights from Light sensor\n"
	"	with external modifications possible\n"
	"Usage: " NAME " [OPTIONS]\n"
	"\n"
	"Options:\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Be more verbose\n"

	" -i, --light=IO	Read light from IO\n"
	" -k, --kbd=IO		Control kbd leds via IO\n"
	" -b, --backlight=IO	Control backlight via IO\n"

	" -l, --listen=URI	Socket address to listen on (default: unix:@macbookd)\n"
	;

#ifdef _GNU_SOURCE
static const struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },

	{ "light", required_argument, NULL, 'i', },
	{ "kbd", required_argument, NULL, 'k', },
	{ "backlight", required_argument, NULL, 'b', },

	{ "listen", required_argument, NULL, 'l', },
	{ },
};

#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif

static const char optstring[] = "?Vvi:k:b:l:";

static struct args {
	int verbose;
	int dryrun;

	char *light, *kbd, *bl;
} s = {
	.light = "preset:maclight",
	.kbd = "preset:mackbdled",
	.bl = "preset:macbacklight",
};

static double fit_float(double value, double min, double max)
{
	if (value < min)
		return min;
	else if (value > max)
		return max;
	else
		return value;
}

static int macbookd(int argc, char *argv[])
{
	int opt, nsocks = 0, changed;
	/* parameter indices */
	int light, kbd, bl, nlight, nkbd, nbl, okbd, obl;

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
	
	case 'i':
		s.light = optarg;
		break;
	case 'k':
		s.kbd = optarg;
		break;
	case 'b':
		s.bl = optarg;
		break;

	case 'l':
		if (libio_bind_net(optarg) < 0)
			elog(LOG_CRIT, 0, "bind %s failed", optarg);
		++nsocks;
		break;
	case '?':
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}
	if (!nsocks) {
		const char uri[] = "unix:@macbookd";
		if (libio_bind_net(uri) < 0)
			elog(LOG_CRIT, 0, "bind %s failed", uri);
	}
	libio_set_trace(s.verbose);

	light = create_iopar(s.light);
	kbd = create_iopar(s.kbd);
	bl = create_iopar(s.bl);

	nlight = create_iopar("netio:light");
	nkbd = create_iopar("netio:kbd");
	nbl = create_iopar("netio:backlight");
	okbd = create_iopar("netio:++kbd");
	obl = create_iopar("netio:++backlight");

	set_iopar(okbd, 0);
	set_iopar(obl, 0);

	/* main ... */
	while (1) {
		while (netio_msg_pending()) {
			const char *msg = netio_recv_msg();

			if (!strncmp("refresh", msg, 7)) {
				char buf[64];

				set_iopar(kbd, get_iopar(kbd));
				set_iopar(bl, get_iopar(bl));
				sprintf(buf, "refresh=backlight=%.3lf,kbd=%.3lf",
						get_iopar(bl), get_iopar(kbd));
				netio_ack_msg(buf);
			} else {
				netio_ack_msg("unknown");
			}
		}

		changed = 0;

		if (iopar_dirty(light))
			set_iopar(nlight, get_iopar(light));

		if (iopar_dirty(light) || iopar_dirty(okbd)) {
			double newvalue = get_iopar(light) * -2 + 1.5;

			newvalue = fit_float(newvalue, 0, 0.5) +
				get_iopar(okbd);
			if (set_iopar(kbd, newvalue) < 0)
				elog(LOG_WARNING, errno, "%.3lf > %s", newvalue, s.kbd);
			set_iopar(nkbd, newvalue);
			++changed;
		}

		if (iopar_dirty(light) || iopar_dirty(obl)) {
			double newvalue = get_iopar(light) * 2 + 0.1;

			newvalue = fit_float(newvalue, 0.05, 1) +
				get_iopar(obl);
			if (set_iopar(bl, newvalue) < 0)
				elog(LOG_WARNING, errno, "%.3lf > %s", newvalue, s.bl);
			set_iopar(nbl, newvalue);
			++changed;
		}

		if (changed)
			elog(LOG_INFO, 0, "light %.3lf kbd %.3lf bl %.3lf",
					get_iopar(light),
					get_iopar(kbd),
					get_iopar(bl));

		/* flush & wait */
		if (libio_wait() < 0)
			break;
	}
	return 0;
}

__attribute__((constructor))
static void init_macbookd(void)
{
	register_applet(NAME, macbookd);
}
