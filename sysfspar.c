#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>

#include <unistd.h>
#include <fcntl.h>
#include <error.h>

#include <libevt.h>

#include "_libio.h"

static const char *const strflags[] = {
	"delay",
		#define ID_DELAY	0
	"invert",
		#define ID_INVERT	1
		#define FL_INVERT	(1 << ID_INVERT)
	"edge",
		#define ID_EDGE		2
	"hysteresis",
		#define ID_HYSTERESIS	3
	"mul",
		#define ID_MULTIPLIER	4
	"max",
		#define ID_MAX		5
	NULL,
};

/* sysfs file for input or output */
struct sysfspar {
	struct iopar iopar;
	long lastval;

	int flags;
	double delay;
	double edge;
	double hyst;
	double mul;
	char sysfs[2];
};

static void sysfspar_read(struct sysfspar *sp, int warn)
{
	int fd, ret;
	long ivalue;
	double fvalue;
	char buf[32], *str;

	/* warn if requested, or param is present */
	warn |= sp->iopar.state & ST_PRESENT;

	fd = open(sp->sysfs, O_RDONLY);
	if (fd < 0) {
		/* avoid alerting too much */
		if (warn)
			elog(LOG_WARNING, errno, "open %s", sp->sysfs);
		goto fail_open;
	}
	ret = read(fd, buf, sizeof(buf)-1);
	if (ret < 0) {
		/* avoid alerting too much */
		if (warn)
			elog(LOG_WARNING, errno, "read %s", sp->sysfs);
		goto fail_read;
	}
	close(fd);
	buf[ret] = 0;

	str = strpbrk(buf, "01234567890+-.");
	if (!str)
		goto fail_parse;
	ivalue = strtoul(str, NULL, 10);
	fvalue = ivalue * sp->mul;
	if (!isnan(sp->edge)) {
		/* boolean detection */
		if (!isnan(sp->hyst)) {
			/* schmitt trigger */
			if (fvalue > sp->edge + sp->hyst)
				ivalue = 1;
			else if (fvalue < sp->edge - sp->hyst)
				ivalue = 0;
			else
				/* remain the same */
				return;
		} else {
			ivalue = fvalue >= sp->edge;
		}
		if (sp->flags & FL_INVERT)
			ivalue = !ivalue;
		fvalue = ivalue;
	}
	if (!(sp->iopar.state & ST_PRESENT) || (ivalue != sp->lastval)) {
		sp->lastval = ivalue;
		sp->iopar.value = fvalue;
		iopar_set_dirty(&sp->iopar);
	}
	/* mark as present */
	iopar_set_present(&sp->iopar);
	return;

fail_read:
	close(fd);
fail_open:
fail_parse:
	iopar_clr_present(&sp->iopar);
}

static void sysfspar_timeout(void *data)
{
	struct sysfspar *sp = data;

	sysfspar_read(sp, 0);
	evt_repeat_timeout(sp->delay, sysfspar_timeout, sp);
}

static int set_sysfspar(struct iopar *iopar, double value)
{
	struct sysfspar *sp = (struct sysfspar *)iopar;
	int ret;
	long ivalue;
	FILE *fp;

	/* NAN may be passed to release control */
	if (isnan(value))
		value = 0;

	fp = fopen(sp->sysfs, "w");
	if (!fp) {
		if (sp->iopar.state & ST_PRESENT)
			elog(LOG_WARNING, errno, "fopen %s", sp->sysfs);
		goto fail_open;
	}

	ivalue = value * 1e3;
	ret = fprintf(fp, "%lu", ivalue);
	if (ret < 0) {
		if (sp->iopar.state & ST_PRESENT)
			elog(LOG_WARNING, errno, "fwrite %s", sp->sysfs);
		goto fail_write;
	}
	fclose(fp);
	sp->iopar.value = value;
	sp->lastval = ivalue;
	iopar_set_present(&sp->iopar);
	return ret;

fail_write:
	fclose(fp);
fail_open:
	iopar_clr_present(&sp->iopar);
	return -1;
}

static void del_sysfspar(struct iopar *iopar)
{
	struct sysfspar *sp = (void *)iopar;

	evt_remove_timeout(sysfspar_timeout, sp);
	cleanup_libiopar(&sp->iopar);
	free(sp);
}

struct iopar *mksysfspar(char *spec)
{
	struct sysfspar *sp;
	const char *tok;
	int flag;

	sp = zalloc(sizeof(*sp) + strlen(spec));
	sp->iopar.del = del_sysfspar;
	sp->iopar.set = set_sysfspar;
	/* force the first read to mark value as dirty */
	strcpy(sp->sysfs, strtok(spec, ","));
	sp->edge = NAN;
	sp->hyst = NAN;
	sp->delay = 1;
	sp->mul = 1;

	while (1) {
		tok = mygetsubopt(strtok(NULL, ","));
		if (!tok)
			break;
		flag = strlookup(tok, strflags);
		if (flag < 0)
			elog(LOG_CRIT, 0, "flag %s unknown", tok);
		switch (flag) {
		case ID_DELAY:
			sp->delay = strtod(mygetsuboptvalue() ?: "1", NULL);
			break;
		case ID_INVERT:
			sp->flags |= FL_INVERT;
			break;
		case ID_HYSTERESIS:
			sp->hyst = strtod(mygetsuboptvalue() ?: "0", NULL);
			break;
		case ID_EDGE:
			sp->edge = strtod(mygetsuboptvalue() ?: "0", NULL);
			break;
		case ID_MULTIPLIER:
			sp->mul = strtod(mygetsuboptvalue() ?: "1", NULL);
			break;
		case ID_MAX:
			sp->mul = 1 / strtod(mygetsuboptvalue() ?: "1", NULL);
			break;
		default:
			sp->flags |= 1 << flag;
			break;
		}
	}

	/* read initial value & schedule next */
	if (!access(sp->sysfs, R_OK)) {
		/* read repeatedly */
		sysfspar_read(sp, 1);
		evt_add_timeout(sp->delay, sysfspar_timeout, sp);
	}
	return &sp->iopar;
}
