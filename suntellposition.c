#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#include <unistd.h>
#include <error.h>
#include <getopt.h>

#include "libio.h"
#include "sun.h"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": calculate the sun's position based on GPS coordinates & date/time\n"
	"Usage: " NAME " [OPTIONS] LAT LON [DATETIME]\n"
	"\n"
	"Options:\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Be more verbose\n"
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

static const char optstring[] = "+?Vvs";

static struct args {
	int verbose;
} s;

static int get_num(const char *str, int n)
{
	static const char numchrs[] = "0123456789";
	char *p;
	int value = 0;

	for (; *str && n; ++str, --n) {
		p = strchr(numchrs, *str);
		if (!p)
			break;
		value = value * 10 + (p - numchrs);
	}
	return value;
}

static time_t strtolocaltime(const char *str, char **endp)
{
	struct tm tm, tmp;
	time_t t;

	time(&t);
	tm = *localtime(&t);

	switch (strlen(str)) {
	case 5:
		tm.tm_wday = get_num(str, 1);
		str += 1;
		goto hour;
	case 12:
		tm.tm_year = get_num(str, 4) -1900;
		str += 4;
	case 8:
		tm.tm_mon = get_num(str, 2) -1;
		str += 2;
	case 6:
		tm.tm_mday = get_num(str, 2);
		str += 2;
	case 4:
	hour:
		tm.tm_hour = get_num(str, 2);
		str += 2;
	case 2:
		tm.tm_min = get_num(str, 2);
		str += 2;
		break;
	}
	tm.tm_sec = 0;
	if (endp)
		*endp = (char *)str;
	/* get normal isdst condition for that time */
	tmp = tm;
	mktime(&tmp);
	tm.tm_isdst = tmp.tm_isdst;

	return mktime(&tm);
}

static const char *secstohuman(unsigned int secs)
{
	static char buf[128];
	char *str = buf;
	unsigned int div, width = 0;

	div = secs / 86400;
	secs %= 86400;
	if (div) {
		str += sprintf(str, "%0*ud ", width, div);
		width = 2;
	}
	div = secs / 3600;
	secs %= 3600;
	if (div || (width && secs)) {
		str += sprintf(str, "%0*uh", width, div);
		width = 2;
	}
	div = secs / 60;
	secs %= 60;
	if (div || (width && secs)) {
		str += sprintf(str, "%0*um", width, div);
		width = 2;
	}
	if (secs || !width)
		str += sprintf(str, "%0*us", width, div);

	return buf;
}

int suntellposition(int argc, char *argv[])
{
	int opt, ret;
	time_t t = time(0);
	double lat, lon, incl, azim;
	unsigned int secs_to_sunupdown;

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

	if (optind+2 > argc) {
		lat = libio_const("latitude");
		lon = libio_const("longitude");
		if (isnan(lat) || isnan(lon)) {
			fputs(help_msg, stderr);
			exit(1);
		}
	} else {
		lat = strtod(argv[optind], 0);
		lon = strtod(argv[optind+1], 0);
	}

	if (optind+3 <= argc)
		t = strtolocaltime(argv[optind+2], NULL);
	printf("time %s", ctime(&t));

	ret = sungetpos(t, lat, lon, &incl, &azim, &secs_to_sunupdown);
	if (ret < 0)
		elog(LOG_CRIT, 0, "failed");

	printf("incl\t%.3lf\nazimuth\t%.3lf\n", incl, azim);
	printf("next-%s\t%s\n", (incl < 0) ? "up" : "down",
			secstohuman(secs_to_sunupdown));
	return 0;
}

__attribute__((constructor))
static void init(void)
{
	register_applet(NAME, suntellposition);
}
