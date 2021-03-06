#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <getopt.h>

#include "lib/libt.h"
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
	struct link *saved = lnk;

	/* test if any motor is moving. stop it first */
	for (lnk = s.links; lnk; lnk = lnk->next) {
		if (speed2bool(get_iopar(lnk->dmot)))
			return 0;
	}
	/* find new direction for lnk */
	lnk = saved;
	if (!lnk)
		return 0;
	return speed2bool(get_iopar(lnk->dmot)) ? 0 : -lnk->lastdir;
}

/* create optional parameter */
static const char *posname(const char *str)
{
	static char buf[128];
	char *p = buf;

	if (*str == '+')
		*p++ = *str++;
	sprintf(p, "p%s", str);
	return buf;
}

static int hamotor(int argc, char *argv[])
{
	int opt, short_press_event, j;
	char *sep, *tmpstr;
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
			elog(LOG_CRIT, 0, "bind %s failed", optarg);
		break;
	case 'i':
		if (s.nin >= MAX_IN)
			elog(LOG_CRIT, 0, "maximum %u inputs", MAX_IN);
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
			elog(LOG_WARNING, 0, "failed to create %s", s.instr[j]);
	}

	for (; optind < argc; ++optind) {
		tmpstr = strdup(argv[optind]);
		sep = strchr(tmpstr, '=');
		if (!sep)
			elog(LOG_CRIT, 0, "bad spec '%s', missing '='", tmpstr);
		*sep++ = 0;
		lnk = zalloc(sizeof(*lnk));
		lnk->dmot = create_iopar(sep);
		if (lnk->dmot < 0) {
			free(lnk);
			free(tmpstr);
			continue;
		}
		lnk->pmot = create_iopar("pmotor:");
		lnk->pdmot = create_ioparf("netio:%s", tmpstr);
		lnk->ppmot = create_ioparf("netio:%s", posname(tmpstr));
		lnk->next = s.links;
		s.links = lnk;
		/* preset lastdir to a nonzero default */
		lnk->lastdir = -1;
		free(tmpstr);
	}
	if (!s.links)
		/* no parameters */
		elog(LOG_WARNING, 0, "no motors, what will I do?");

	short_press_event = 0;
	/* main ... */
	while (1) {
		/* determine local input */
		for (j = 0; j < s.nin; ++j) {
			if (s.in[j] < 0)
				/* ignore */
				continue;
			if (!iopar_dirty(s.in[j])) {
				/* nothing */
			} else if (get_iopar(s.in[j]) > 0.5) {
				libt_add_timeout(1, btn_down_timer, NULL);
				break;
			} else if (get_iopar(s.in[j]) < 0.5) {
				libt_remove_timeout(btn_down_timer, NULL);
				short_press_event = !long_press_pending;
				long_press_pending = 0;
				break;
			}
		}

		for (lnk = s.links; lnk; lnk = lnk->next) {
			/* PER motor control */
			if (iopar_dirty(lnk->pdmot)) {
				/* netio direction set */
				set_iopar(lnk->dmot, get_iopar(lnk->pdmot));
				/* write back in case the real value didn't change */
				set_iopar(lnk->pdmot, get_iopar(lnk->dmot));
				/* remember dir */
				remember_dir(lnk, get_iopar(lnk->dmot));
			} else if (iopar_dirty(lnk->dmot)) {
				/*
				 * motor changed direction itself
				 * most probably due to position update
				 */
				set_iopar(lnk->pdmot, get_iopar(lnk->dmot));
				remember_dir(lnk, get_iopar(lnk->dmot));
			}
			/* flush position parameters */
			if (iopar_dirty(lnk->pmot))
				set_iopar(lnk->ppmot, get_iopar(lnk->pmot));
			else if (iopar_dirty(lnk->ppmot)) {
				set_iopar(lnk->pmot, get_iopar(lnk->ppmot));
				set_iopar(lnk->ppmot, get_iopar(lnk->pmot));
				/* in case the direction changed, flush to netio */
				set_iopar(lnk->pdmot, get_iopar(lnk->dmot));
				remember_dir(lnk, get_iopar(lnk->dmot));
			}
		}

		/* handle clicks */
		if (short_press_event || long_press_event) {
			/* movement change requested. */
			value = get_new_dir(s.links);

			for (lnk = s.links; lnk; lnk = lnk->next) {
				/* change direction of all motors */
				set_iopar(lnk->dmot, value);
				set_iopar(lnk->pdmot, get_iopar(lnk->dmot));
				remember_dir(lnk, value);
				if (long_press_event)
					/* only first motor */
					break;
			}
		}

		long_press_event = short_press_event = 0;
		if (libio_wait() < 0)
			break;
	}
	return 0;
}

__attribute__((constructor))
static void init(void)
{
	register_applet(NAME, hamotor);
}
