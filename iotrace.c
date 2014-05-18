#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#include <unistd.h>
#include <getopt.h>

#include <libevt.h>
#include "_libio.h"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": Print a new line with iolib values on every change\n"
	"Usage: " NAME " FORMATSTRING [[MUL*]PARAM ...]\n"
	"\n"
	"Parameters\n"
	" FORMATSTRING		printf-style formatstring.\n"
	"			Only floating point specifier are allowed\n"
	" PARAM			The parameters that provide values\n"
	"Options:\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Be more verbose\n"
	" -l, --listen=SPEC	Listen on SPEC\n"
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
	struct ent {
		int iopar;
		double value;
		double mul;
	} *e;
	int ne;
	const char *fmt;
} s;

static int myprint(FILE *fp, const char *fmt)
{
	const char *str;
	int result = 0, idx;
	static char fmtbuf[64], strbuf[64];

	for (idx = 0; *fmt; ) {
		/* find next %..f sequence */
		str = strchr(fmt, '%');
		if (!str) {
			/* put final part */
			result += strlen(fmt);
			fputs(fmt, fp);
			break;
		} else if (str[1] == '%') {
			/* %% sequence */
			for (; fmt <= str; ++fmt)
				fputc(*fmt, fp);
			/* skip second '%' */
			++fmt;
			continue;
		}
		/* put chars up to %..f sequence */
		result += str - fmt;
		for (; fmt < str; ++fmt)
			fputc(*fmt, fp);
		if (!strncmp(fmt, "%date", 5)) {
			time_t now;

			time(&now);
			result += strftime(strbuf, sizeof(strbuf), "%a %d %b %Y %H:%M:%S", localtime(&now));
			fputs(strbuf, fp);
			fmt += 5;
			continue;
		}
		/* put number */
		str = strchr(fmt, 'f');
		if (!str) {
			/* wrong sequence */
			fputc(*fmt++, fp);
			++result;
			continue;
		}
		/* include 'f' character */
		++str;
		if (idx >= s.ne) {
			elog(LOG_CRIT, 0, "not enough parameters for format #%i", s.ne);
		}
		/* copy format string part */
		strncpy(fmtbuf, fmt, str - fmt);
		fmtbuf[str - fmt] = 0;
		/* proceed format string */
		fmt = str;
		/* print */
		sprintf(strbuf, fmtbuf, s.e[idx].value * s.e[idx].mul);
		++idx;
		fputs(strbuf, fp);
		result += strlen(strbuf);
	}
	return result;
}

/* main */
static int iotrace(int argc, char *argv[])
{
	int opt, j, changed;
	char *endp;

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

	/* parse arguments */
	if (!argv[optind]) {
		fputs(help_msg, stderr);
		exit(1);
	}
	s.fmt = argv[optind++];

	/* pre-allocate arrays */
	s.ne = argc - optind;
	s.e = malloc(s.ne * sizeof(*s.e));

	/* parse iopar arguments */
	for (j = 0; optind < argc; ++optind, ++j) {
		s.e[j].mul = strtod(argv[optind], &endp);
		if ((endp > argv[optind]) && (*endp == '*')) {
			/* skip '*' */
			++endp;
		} else {
			/* reset parser */
			endp = argv[optind];
			/* init mul */
			s.e[j].mul = 1;
		}
		s.e[j].iopar = create_iopar(endp);
		s.e[j].value = NAN;
	}

	libio_set_trace(s.verbose);
	/* main ... */
	while (1) {
		/* netio msgs */
		while (netio_msg_pending()) {
			puts(netio_recv_msg());
			fflush(stdout);
		}

		changed = 0;
		for (j = 0; j < s.ne; ++j) {
			if (iopar_dirty(s.e[j].iopar)) {
				++changed;
				s.e[j].value = get_iopar(s.e[j].iopar);
			}
		}

		if (changed) {
			myprint(stdout, s.fmt);
			fputc('\n', stdout);
			fflush(stdout);
		}

		/* common libio stuff */
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
	register_applet(NAME, iotrace);
}
