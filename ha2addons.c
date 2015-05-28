#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <unistd.h>
#include <getopt.h>

#include "lib/libt.h"
#include "_libio.h"
#include "sun.h"

/* configs */
#define NBADK	4
#define NBLUE	4
#define NMAIN	4

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
	" -l, --listen=SPEC	Listen on SPEC\n"
	"Required preset parameters:\n"
	" led\n"
	" zolder\n"
	" fan lavabo bad\n"
	" bluebad\n"
	" main blueled\n"
	" hal\n"
	" veluxhgpos veluxlgpos\n"
	"Required inputs:\n"
	" badk1 badk2 badk3 ...\n"
	" blue1 blue2 ... \n"
	" main1 main2 ... \n"
	" poets\n"
	"Used consts:\n"
	" (longpress)\n"
	" opstaan	0:00:00..24:00:00\n"
	" slapen	0:00:00..24:00:00\n"
	" lednight	0.01..1\n"
	" longitude	-180..180\n"
	" latitude	-90..90\n"
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

static struct args {
	int verbose;
	int led, zolder, fan, lavabo, bad,
	    bluebad, main, blueled, hal,
	    veluxhgpos, veluxlgpos;
	int badk[NBADK], blue[NBLUE], imain[NMAIN], poets;
	double hopstaan, hslapen;
	double lednight;
	double longitude, latitude;

	/* state */
	const char *dim;
} s = {
	.dim = "velux", /* .dim must be set */
};

static inline int active(int iopar)
{
	return get_iopar(iopar) >= 0.5;
}

static inline int btnpushed(int iopar)
{
	return iopar_dirty(iopar) && active(iopar);
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

static inline void set_longdet_btns(int longdet, const int *iopars, int niopars)
{
	int j;

	for (j = 0; j < niopars; ++j) {
		if (iopar_dirty(iopars[j])) {
			set_longdet(longdet, get_iopar(iopars[j]));
			break;
		}
	}
}

static inline int lavabo_dimmed(void)
{
	/* test manual overrule */
	if (strstr(s.dim, "on"))
		return 1;
	else if (strstr(s.dim, "off"))
		return 0;
	else if (strstr(s.dim, "velux")) {
		/* velux gordijn pos */
		if ((get_iopar(s.veluxhgpos) + get_iopar(s.veluxlgpos)) > 1.5)
			return 1;
	} else if (strstr(s.dim, "sun")) {
		/* sun's position */
		double incl, az;
		where_is_the_sun(time(NULL), s.latitude, s.longitude, &incl, &az);
		if (incl < -0.25)
			return 1;
	} else if (strstr(s.dim, "time")) {
		/* time based */
		time_t now;
		struct tm tm;
		double hours;

		time(&now);
		tm = *localtime(&now);
		hours = tm.tm_hour + (tm.tm_min / 60) + (tm.tm_sec / 3600);

		return !((hours >= s.hopstaan) && (hours <= s.hslapen));
	}
	return 0;
}

/* output timer */
static void output_timeout(void *dat)
{
	set_iopar((long)dat, 0);
}

static void schedule_output_reset_timer(int iopar, double timeout)
{
	if (iopar_dirty(iopar) && (get_iopar(iopar) > 0))
		libt_add_timeout(timeout, output_timeout, (void *)(long)iopar);
	else if (iopar_dirty(iopar) && (get_iopar(iopar) < 0.001))
		libt_remove_timeout(output_timeout, (void *)(long)iopar);
}

/* main */
static int ha2addons(int argc, char *argv[])
{
	int opt, j;
	int ldbadk, ldmain, ldblue;

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

	s.hopstaan = libio_const("opstaan");
	s.hslapen = libio_const("slapen");
	s.lednight = libio_const("lednight");
	s.longitude = libio_const("longitude");
	s.latitude = libio_const("latitude");

	/* wait up to 5sec for remote */
	for (j = 0; j < 50; ++j, usleep(100000))
		if (netio_probe_remote("unix:@ha2") >= 0)
			break;
	if (j)
		elog(LOG_NOTICE, 0, "waited %.1lfs for remote", j*0.1);

	s.led = create_iopar("led");
	s.zolder = create_iopar("zolder");
	s.fan = create_iopar("fan");
	s.lavabo = create_iopar("lavabo");
	s.bad = create_iopar("bad");
	s.bluebad = create_iopar("bluebad");
	s.main = create_iopar("main");
	s.blueled = create_iopar("blueled");
	s.hal = create_iopar("hal");
	s.veluxhgpos = create_iopar("veluxhgpos");
	s.veluxlgpos = create_iopar("veluxlgpos");

	s.badk[0] = create_iopar("badk1");
	s.badk[1] = create_iopar("badk2");
	s.badk[2] = create_iopar("badk3");
	s.badk[3] = create_iopar("badk4");
	s.blue[0] = create_iopar("blue1");
	s.blue[1] = create_iopar("blue2");
	s.blue[2] = create_iopar("blue3");
	s.blue[3] = create_iopar("blue4");
	s.imain[0] = create_iopar("main1");
	s.imain[1] = create_iopar("main2");
	s.imain[2] = create_iopar("main3");
	s.imain[3] = create_iopar("main4");
	s.poets = create_iopar("poets");

	ldbadk = new_longdet();
	ldmain = new_longdet();
	ldblue = new_longdet();

	/* main ... */
	while (1) {
		/* netio msgs */
		while (netio_msg_pending()) {
			const char *msg = netio_recv_msg();

			if (!strncmp("dim", msg, 3)) {
				char *str;

				if (msg[3] == '=') {
					static char *buf;

					if (buf)
						free(buf);
					buf = strdup(msg+4);
					s.dim = buf;
				}
				asprintf(&str, "dim=%s", s.dim);
				netio_ack_msg(str);
				free(str);
			} else {
				netio_ack_msg("unknown");
			}
		}
		/* special badkamer input */

		set_longdet_btns(ldbadk, s.badk, NBADK);
		if (longdet_edge(ldbadk) && longdet_state(ldbadk)) {
			if (longdet_state(ldbadk) == LONGPRESS) {
				/* force on */
				set_iopar(s.led, 1);
				set_iopar(s.lavabo, 1);
			} else if (get_iopar(s.led) > 0.01) {
				/* turn off */
				set_iopar(s.led, 0);
				set_iopar(s.lavabo, 0);
			} else if (lavabo_dimmed()) {
				/* turn on dimmed */
				set_iopar(s.led, s.lednight);
			} else {
				/* turn on 100% */
				set_iopar(s.led, 1);
				set_iopar(s.lavabo, 1);
			}
		}

		/* blue lights */
		set_longdet_btns(ldblue, s.blue, NBLUE);
		if (longdet_edge(ldblue) && longdet_state(ldblue)) {
			if (longdet_state(ldblue) == LONGPRESS) {
				if (active(s.bluebad) && active(s.blueled))
					/* keep bluebad only, turn led off */
					set_iopar(s.blueled, 0);
				else {
					set_iopar(s.bluebad, 1);
					set_iopar(s.blueled, 1);
				}
			} else if (active(s.blueled) || active(s.bluebad)) {
				set_iopar(s.bluebad, 0);
				set_iopar(s.blueled, 0);
			} else if (lavabo_dimmed()) {
				set_iopar(s.blueled, 1);
			} else {
				set_iopar(s.bluebad, 1);
				set_iopar(s.blueled, 1);
			}
		}

		/* main */
		set_longdet_btns(ldmain, s.imain, NMAIN);
		if (longdet_edge(ldmain) && (longdet_state(ldmain) == LONGPRESS)) {
			/* all off */
			set_iopar(s.led, 0);
			set_iopar(s.zolder, 0);
			set_iopar(s.fan, 0);
			set_iopar(s.lavabo, 0);
			set_iopar(s.bad, 0);
			set_iopar(s.bluebad, 0);
			set_iopar(s.main, 0);
			set_iopar(s.blueled, 0);
			set_iopar(s.hal, 0);
		} else if (longdet_edge(ldmain) && (longdet_state(ldmain) == SHORTPRESS)) {
			/* toggle */
			set_iopar(s.main, !active(s.main));
		}

		if (btnpushed(s.poets)) {
			if (active(s.main) || active(s.lavabo) ||
					active(s.bad) || active(s.hal)) {
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

		if (libio_wait() < 0)
			break;
	}
	return 0;
}

__attribute__((constructor))
static void init(void)
{
	register_applet(NAME, ha2addons);
}
