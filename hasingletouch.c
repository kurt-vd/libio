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
	NAME ": Set multiple outputs with 1 touch\n"
	"Usage: " NAME " [OPTIONS] OUT [ OUT ...]\n"
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
	int out;
};

static struct args {
	int verbose;
	struct link *links;
	struct link *current;
	int in[MAX_IN]; /* common input(s) */
	int nin;
	char *instr[MAX_IN];
} s;

static int hasingletouch(int argc, char *argv[])
{
	int opt, j, changed;
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
		lnk->out = create_iopar(argv[optind]);
		/* append in list */
		lnk->next = s.links;
		s.links = lnk;
	}

	/* main ... */
	while (1) {
		/* determine local input */
		changed = 0;
		for (j = 0; j < s.nin; ++j) {
			if (iopar_dirty(s.in[j]) && (get_iopar(s.in[j]) > 0.5))
				changed = 1;

		}

		if (changed) {
			/* find old value: sum current value */
			value = 0;
			for (lnk = s.links; lnk; lnk = lnk->next)
				value += get_iopar(lnk->out);
			/* calc new value */
			value = ((value / s.nin) >= 0.5) ? 0 : 1;
			/* set new value */
			for (lnk = s.links; lnk; lnk = lnk->next)
				value += set_iopar(lnk->out, value);
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
	register_applet(NAME, hasingletouch);
}
