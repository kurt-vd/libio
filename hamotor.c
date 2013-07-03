#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <error.h>
#include <getopt.h>

#include <libevt.h>
#include "_libio.h"

#define MAX_IN 3

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": Control 2+ motors with similar function\n"
	"Usage: " NAME " [OPTIONS] IN NAME=DMOT1 [ NAME=DMOT2 ... ]\n"
	"\n"
	"Options:\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Be more verbose\n"
	" -l, --listen=SPEC	Listen on SPEC\n"
	" -i, --in=SPEC		Use SPEC as input\n"
	;

#ifdef _GNU_SOURCE
static const struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },
	{ "listen", required_argument, NULL, 'l', },
	{ "in", required_argument, NULL, 'i', },
	{ },
};

#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif

static const char optstring[] = "?Vvl:i:";

struct link {
	struct link *next;
	/* local params */
	int dmot, pmot;
	/* public params */
	int pdmot, ppmot;
	/* 2 last dirctions */
	int lastdir;
};

static struct args {
	int verbose;
	struct link *links;
	struct link *current;
	int in[MAX_IN]; /* common input(s) */
	int nin;
	char *instr[MAX_IN];
} s;

/*
 * long-click detection
 * @long_press_event: single event
 * @long_press_pending: set until btn released
 */
static int long_press_event, long_press_pending;

static void btn_down_timer(void *dat)
{
	long_press_pending = 1;
	long_press_event = 1;
}

/* switch */
static void motor_select_timer(void *dat)
{
	/* reset motor selection */
	s.current = NULL;
}

/* motor direction caching */
static int speed2bool(double speed)
{
	return (speed > 0) ? 1 : ((speed < 0) ? -1 : 0);
}

static void remember_dir(struct link *lnk, double dir)
{
	int idir = speed2bool(dir);

	if (idir)
		lnk->lastdir = idir;
}

static double get_new_dir(struct link *lnk)
{
	int cdir = speed2bool(get_iopar(lnk->dmot, 0));

	return cdir ? 0 : -lnk->lastdir;
}

/* create optional parameter */
static const char *posname(const char *str)
{
	static char buf[128];

	sprintf(buf, "p%s", str);
	return buf;
}

static int hamotor(int argc, char *argv[])
{
	int opt, short_press_event, j;
	char *name;
	struct link *lnk;
	double value;

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
	case 'i':
		if (s.nin >= MAX_IN)
			error(1, 0, "maximum %u inputs", MAX_IN);
		s.instr[s.nin++] = optarg;
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
	for (j = 0; j < s.nin; ++j) {
		s.in[j] = create_iopar(s.instr[j]);
		if (s.in[j] < 0)
			error(1, 0, "failed to create %s", s.instr[j]);
	}

	for (; optind < argc; ++optind) {
		lnk = zalloc(sizeof(*lnk));
		name = strtok(argv[optind], "=");
		lnk->pdmot = create_iopar_type("netio", name);
		lnk->ppmot = create_iopar_type("netio", posname(name));
		lnk->dmot = create_iopar(strtok(NULL, "="));
		lnk->pmot = create_iopar("pmotor:");
		lnk->next = s.links;
		s.links = lnk;
		/* preset lastdir to a nonzero default */
		lnk->lastdir = -1;
	}

	short_press_event = 0;
	/* main ... */
	while (1) {
		/* determine local input */
		for (j = 0; j < s.nin; ++j) {
			if (!iopar_dirty(s.in[j])) {
				/* nothing */
			} else if (get_iopar(s.in[j], 0) > 0.5) {
				evt_add_timeout(1, btn_down_timer, NULL);
				break;
			} else if (get_iopar(s.in[j], 1) < 0.5) {
				evt_remove_timeout(btn_down_timer, NULL);
				short_press_event = !long_press_pending;
				long_press_pending = 0;
				break;
			}
		}

		for (lnk = s.links; lnk; lnk = lnk->next) {
			/* PER motor control */
			if (iopar_dirty(lnk->pdmot)) {
				/* netio direction set */
				set_iopar(lnk->dmot, get_iopar(lnk->pdmot, 0));
				/* write back in case the real value didn't change */
				set_iopar(lnk->pdmot, get_iopar(lnk->dmot, 0));
				/* remember dir */
				remember_dir(lnk, get_iopar(lnk->dmot, 0));
			} else if (iopar_dirty(lnk->dmot)) {
				/*
				 * motor changed direction itself
				 * most probably due to position update
				 */
				set_iopar(lnk->pdmot, get_iopar(lnk->dmot, 0));
				remember_dir(lnk, get_iopar(lnk->dmot, 0));
			}
			/* flush position parameters */
			if (iopar_dirty(lnk->pmot))
				set_iopar(lnk->ppmot, get_iopar(lnk->pmot, 0));
			else if (iopar_dirty(lnk->ppmot)) {
				set_iopar(lnk->pmot, get_iopar(lnk->ppmot, 0));
				set_iopar(lnk->ppmot, get_iopar(lnk->pmot, 0));
			}
		}

		/* handle clicks */
		if (long_press_event) {
			/* deal with long-press timers */
			evt_remove_timeout(motor_select_timer, NULL);
			evt_add_timeout(5, motor_select_timer, NULL);
			s.current = s.current ? s.current->next : s.links;
		}

		if (short_press_event || long_press_event) {
			/* movement change requested. */
			value = get_new_dir(s.current ?: s.links);

			if (s.current) {
				set_iopar(s.current->dmot, value);
				set_iopar(s.current->pdmot, get_iopar(s.current->dmot, 0));
				remember_dir(s.current, value);
			} else for (lnk = s.links; lnk; lnk = lnk->next) {
				/* change direction of all motors */
				set_iopar(lnk->dmot, value);
				set_iopar(lnk->pdmot, get_iopar(lnk->dmot, 0));
				remember_dir(lnk, value);
			}
		}

		long_press_event = short_press_event = 0;
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
	register_applet(NAME, hamotor);
}
