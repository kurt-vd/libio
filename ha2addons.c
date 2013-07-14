#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#include <error.h>
#include <getopt.h>

#include <libevt.h>
#include "_libio.h"

#define HOUR *3600

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": Addons for my level2 automation\n"
	"Usage: " NAME " \n"
	"\n"
	"Options:\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Be more verbose\n"
	"Required preset parameters:\n"
	" led\n"
	" zolder\n"
	" fan lavabo bad\n"
	" bluebad\n"
	" main blueled\n"
	" hal\n"
	" veluxhg veluxlg\n"
	"Required inputs:\n"
	" badk\n"
	" alloff\n"
	" poets\n"
	;

#ifdef _GNU_SOURCE
static const struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },
	{ },
};

#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif

static const char optstring[] = "?Vv";

static struct args {
	int verbose;
	int led, zolder, fan, lavabo, bad,
	    bluebad, main, blueled, hal,
	    veluxhg, veluxlg;
	int badk, alloff, poets;
} s;

static inline int btnpushed(int iopar)
{
	return iopar_dirty(iopar) && (get_iopar(iopar, NAN) >= 0.5);
}

static inline int lavabo_dimmed(void)
{
	time_t now;
	struct tm tm;
	unsigned long mins;

	time(&now);
	tm = *localtime(&now);
	mins = tm.tm_hour * 60 + tm.tm_min;
#define MINS(H, M) (((H)*60)+(M))

	return !((mins > MINS(7, 0)) && (mins < MINS(23, 30)));
}

/* main */
static int ha2addons(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s\n", NAME, LOCALVERSION);
		return 0;
	case 'v':
		++s.verbose;
		break;

	case '?':
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}
	libio_set_trace(s.verbose);

	s.led = create_iopar("led");
	s.zolder = create_iopar("zolder");
	s.fan = create_iopar("fan");
	s.lavabo = create_iopar("lavabo");
	s.bad = create_iopar("bad");
	s.bluebad = create_iopar("bluebad");
	s.main = create_iopar("main");
	s.blueled = create_iopar("blueled");
	s.hal = create_iopar("hal");
	s.veluxhg = create_iopar("veluxhg");
	s.veluxlg = create_iopar("veluxlg");

	s.badk = create_iopar("badk");
	s.alloff = create_iopar("alloff");
	s.poets = create_iopar("poets");

	/* main ... */
	while (1) {
		/* special badkamer input */

		if (btnpushed(s.badk)) {
			if (get_iopar(s.led, NAN) < 0.01) {
				if (lavabo_dimmed()) {
					set_iopar(s.led, 0.025);
					set_iopar(s.lavabo, 0);
				} else {
					set_iopar(s.led, 1);
					set_iopar(s.lavabo, 1);
				}
			} else {
				set_iopar(s.led, 0);
				set_iopar(s.lavabo, 0);
			}
		}
		/* all off */
		if (btnpushed(s.alloff)) {
			set_iopar(s.led, 0);
			set_iopar(s.zolder, 0);
			set_iopar(s.fan, 0);
			set_iopar(s.lavabo, 0);
			set_iopar(s.bad, 0);
			set_iopar(s.bluebad, 0);
			set_iopar(s.main, 0);
			set_iopar(s.blueled, 0);
			set_iopar(s.hal, 0);
		}

		if (btnpushed(s.poets)) {
			if (get_iopar(s.main, 1) >= 0.5) {
				/* turn a lot off */
				set_iopar(s.led, 0);
				set_iopar(s.lavabo, 0);
				set_iopar(s.bad, 0);
				set_iopar(s.bluebad, 0);
				set_iopar(s.main, 0);
				set_iopar(s.blueled, 0);
				set_iopar(s.hal, 0);
			} else {
				/* turn some things on */
				set_iopar(s.lavabo, 1);
				set_iopar(s.bad, 1);
				set_iopar(s.main, 1);
			}
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

__attribute__((constructor))
static void init(void)
{
	register_applet(NAME, ha2addons);
}
