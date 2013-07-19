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

/* configs */
#define NBADK	3

/* definitions */
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
	" badk badk2 badk3 ...\n"
	" alloff\n"
	" poets\n"
	"Used consts:\n"
	" (longpress)\n"
	" opstaan	0:00:00..24:00:00\n"
	" slapen	0:00:00..24:00:00\n"
	" lednight	0.01..1\n"
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
	int badk[NBADK], alloff, poets;
	double hopstaan, hslapen;
	double lednight;
} s;

static inline int btnpushed(int iopar)
{
	return iopar_dirty(iopar) && (get_iopar(iopar, NAN) >= 0.5);
}

static inline int btnspushed(const int *iopars, int niopars)
{
	int j;

	for (j = 0; j < niopars; ++j) {
		if (btnpushed(iopars[j]))
			return 1;
	}
	return 0;
}

static inline int btnsdown(const int *iopars, int niopars)
{
	int j;

	for (j = 0; j < niopars; ++j) {
		if (get_iopar(iopars[j], NAN) > 0.5)
			return 1;
	}
	return 0;
}

static inline int lavabo_dimmed(void)
{
	time_t now;
	struct tm tm;
	double hours;

	time(&now);
	tm = *localtime(&now);
	hours = tm.tm_hour + (tm.tm_min / 60) + (tm.tm_sec / 3600);

	return !((hours > s.hopstaan) && (hours < s.hslapen));
}

/* output timer */
static void output_timeout(void *dat)
{
	set_iopar((long)dat, 0);
}

static void schedule_output_reset_timer(int iopar, double timeout)
{
	if (iopar_dirty(iopar) && (get_iopar(iopar, NAN) > 0))
		evt_add_timeout(60*60*1.5, output_timeout, (void *)(long)iopar);
	else if (iopar_dirty(iopar) && (get_iopar(iopar, NAN) < 0.001))
		evt_remove_timeout(output_timeout, (void *)(long)iopar);
}

/* main */
static int ha2addons(int argc, char *argv[])
{
	int opt;
	int ldbadk;

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

	s.hopstaan = libio_const("opstaan");
	s.hslapen = libio_const("slapen");
	s.lednight = libio_const("lednight");

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

	s.badk[0] = create_iopar("badk");
	s.badk[1] = create_iopar("badk2");
	s.badk[2] = create_iopar("badk3");
	s.alloff = create_iopar("alloff");
	s.poets = create_iopar("poets");

	ldbadk = new_longdet();

	/* main ... */
	while (1) {
		/* special badkamer input */

		set_longdet(ldbadk, btnsdown(s.badk, NBADK));
		if (longdet_edge(ldbadk) && longdet_state(ldbadk)) {
			int longpress = longdet_state(ldbadk) == LONGPRESS;

			if (longpress || get_iopar(s.led, NAN) < 0.01) {
				if (!longpress && lavabo_dimmed()) {
					set_iopar(s.led, s.lednight);
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

		/* reset FAN */
		schedule_output_reset_timer(s.fan, 1.5 HOUR);
		/* reset zolder light */
		schedule_output_reset_timer(s.zolder, 2 HOUR);

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
