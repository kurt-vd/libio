#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#include <unistd.h>
#include <getopt.h>
#ifdef HAVE_IFADDRS
#include <ifaddrs.h>
#endif
#include <sys/socket.h>
#include <linux/if.h>
#include <arpa/inet.h>

#include "_libio.h"
#include "lib/libt.h"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": Print a new line with iolib values on every change\n"
	"Usage: " NAME " FORMATSTRING [[MUL*]PARAM ...]\n"
	"	" NAME " FILE\n"
	"\n"
	"The first form takes all parameters from commandline\n"
	"the second form reads all parameters from FILE (- for stdin)\n"
	"each non-empty, non-comment line is taken as parameter\n"
	"\n"
	"Parameters\n"
	" FORMATSTRING		printf-style formatstring.\n"
	"			Only floating point specifier are allowed\n"
	"			Special specifiers:\n"
	"			%date\n"
	"			%net(IFACE)\n"
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

#ifdef HAVE_IFADDRS
/* cached ifaddrs table */
static struct ifaddrs *ifa_table;

static const char *netdevstr(const char *iface)
{
	static char buf[1024];
	static char inetstr[INET6_ADDRSTRLEN];
	char *str = buf;
	int flags_printed;
	struct ifaddrs *ptr;

	str += sprintf(str, "%s: ", iface);

	if (!ifa_table) {
		if (getifaddrs(&ifa_table) < 0) {
			str += sprintf(str, "fail");
			goto done;
		}
	}

	flags_printed = 0;
	for (ptr = ifa_table; ptr; ptr = ptr->ifa_next) {
		if (strcmp(iface, ptr->ifa_name))
			continue;
		if (!flags_printed) {
			flags_printed = 1;

			/* print flags, only once */
			if (!(ptr->ifa_flags & IFF_UP))
				str += sprintf(str, "down");
			else if (!(ptr->ifa_flags & IFF_RUNNING))
				str += sprintf(str, "no-carrier");
			else
				str += sprintf(str, "up");
		}
		switch (ptr->ifa_addr->sa_family) {
		case AF_PACKET:
			break;
		case AF_INET:
			inetstr[0] = 0;
			inet_ntop(ptr->ifa_addr->sa_family, &((const struct sockaddr_in *)ptr->ifa_addr)->sin_addr, inetstr, sizeof(inetstr));
			if (strncmp(inetstr, "169.254.", 8))
				str += sprintf(str, ", %s", inetstr);
			break;
		case AF_INET6:
			inetstr[0] = 0;
			inet_ntop(ptr->ifa_addr->sa_family, &((const struct sockaddr_in6 *)ptr->ifa_addr)->sin6_addr, inetstr, sizeof(inetstr));
			if (strncmp(inetstr, "fe80:", 5))
				str += sprintf(str, ", %s", inetstr);
			break;
		}
	}
	if (!flags_printed)
		/* flags never printed means device not present */
		str += sprintf(str, "n.a.");
done:
	return buf;
}
#endif

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
#ifdef HAVE_IFADDRS
		if (!strncmp(fmt, "%net(", 5)) {
			const char *str;
			char ifname[IFNAMSIZ+1] = {};

			fmt += 5;
			str = strchr(fmt, ')');
			if ((str - fmt) >= sizeof(ifname))
				elog(LOG_CRIT, 0, "net iface name too long '%.*s'",
						(int)(str - fmt), fmt);
			strncpy(ifname, fmt, str - fmt);
			fmt = str+1;

			fputs(netdevstr(ifname), fp);
			continue;
		}
#endif
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
#ifdef HAVE_IFADDRS
	if (ifa_table) {
		freeifaddrs(ifa_table);
		ifa_table = NULL;
	}
#endif
	return result;
}

/* parameters from stdin mode */
static char *fetch_param_from_file(FILE *fp)
{
	static char *line;
	static size_t linesize;
	int ret;

	do {
		ret = getline(&line, &linesize, fp);
		if (ret < 0)
			return NULL;
		for (; ret > 0; --ret)
			if (!strchr(" \t\r\n\v\f", line[ret-1]))
				break;
		line[ret] = 0;
	} while (!ret || (*line == '#'));
	return line;
}

/* parser helper */
static void parse_param(struct ent *e, char *str)
{
	char *endp;

	e->mul = strtod(str, &endp);
	if ((endp > str) && (*endp == '*')) {
		/* skip '*' */
		++endp;
	} else {
		/* reset parser */
		endp = str;
		/* init mul */
		e->mul = 1;
	}
	e->iopar = create_iopar(endp);
	e->value = NAN;
}

static void trace_timeout(void *dat)
{
	libt_add_timeout(1.1, trace_timeout, dat);
}

/* main */
static int iotrace(int argc, char *argv[])
{
	int opt, j;

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

	if (!strcmp(s.fmt, "-")) {
		int resargs;
		char *arg;

		/* read parameters from stdin */
		s.fmt = fetch_param_from_file(stdin);
		if (!s.fmt) {
			fputs(help_msg, stderr);
			exit(1);
		}
		/* allocate format string */
		s.fmt = strdup(s.fmt);
		/* fetch parameters */
		resargs = 0;
		do {
			if (s.ne >= resargs)
				s.e = realloc(s.e, (resargs += 16) * sizeof(*s.e));
			arg = fetch_param_from_file(stdin);
			if (arg)
				parse_param(s.e + s.ne++, arg);
		} while (arg);
	} else {
		/* pre-allocate arrays */
		s.ne = argc - optind;
		s.e = malloc(s.ne * sizeof(*s.e));

		/* parse iopar arguments */
		for (j = 0; optind < argc; ++optind, ++j)
			parse_param(s.e+j, argv[optind]);
	}

	libio_set_trace(s.verbose);
	libt_add_timeout(1.1, trace_timeout, NULL);
	/* main ... */
	while (1) {
		/* netio msgs */
		while (netio_msg_pending()) {
			puts(netio_recv_msg());
			fflush(stdout);
		}

		for (j = 0; j < s.ne; ++j) {
			if (iopar_dirty(s.e[j].iopar))
				s.e[j].value = get_iopar(s.e[j].iopar);
		}

		myprint(stdout, s.fmt);
		fputc('\n', stdout);
		fflush(stdout);

		/* common libio stuff */
		if (libio_wait() < 0)
			break;
	}
	return 0;
}

__attribute__((constructor))
static void init(void)
{
	register_applet(NAME, iotrace);
}
