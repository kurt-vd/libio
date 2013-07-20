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
	NULL,
};

/* sysfs file for input or output */
struct sysfspar {
	struct iopar iopar;
	long lastval;

	int flags;
	double delay;
	char sysfs[2];
};

static void sysfspar_read(struct sysfspar *sp, int warn)
{
	int fd, ret;
	long ivalue;
	char buf[32];

	/* warn if requested, or param is present */
	warn |= sp->iopar.state & ST_PRESENT;

	fd = open(sp->sysfs, O_RDONLY);
	if (fd < 0) {
		/* avoid alerting too much */
		if (warn)
			error(0, errno, "open %s", sp->sysfs);
		goto fail_open;
	}
	ret = read(fd, buf, sizeof(buf)-1);
	if (ret < 0) {
		/* avoid alerting too much */
		if (warn)
			error(0, errno, "read %s", sp->sysfs);
		goto fail_read;
	}
	close(fd);
	buf[ret] = 0;

	ivalue = strtoul(buf, NULL, 10);
	if (!(sp->iopar.state & ST_PRESENT) || (ivalue != sp->lastval)) {
		sp->lastval = ivalue;
		sp->iopar.value = ivalue / 1e3;
		iopar_set_dirty(&sp->iopar);
	}
	/* mark as present */
	iopar_set_present(&sp->iopar);
	return;

fail_read:
	close(fd);
fail_open:
	iopar_clr_present(&sp->iopar);
}

static void sysfspar_timeout(void *data)
{
	sysfspar_read(data, 0);
	evt_repeat_timeout(1, sysfspar_timeout, data);
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
			error(0, errno, "fopen %s", sp->sysfs);
		goto fail_open;
	}

	ivalue = value * 1e3;
	ret = fprintf(fp, "%lu", ivalue);
	if (ret < 0) {
		if (sp->iopar.state & ST_PRESENT)
			error(0, errno, "fwrite %s", sp->sysfs);
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
	sp->delay = 1;

	while (1) {
		tok = mygetsubopt(strtok(NULL, ","));
		if (!tok)
			break;
		flag = strlookup(tok, strflags);
		if (flag < 0)
			error(1, 0, "flag %s unknown", tok);
		switch (flag) {
		case ID_DELAY:
			sp->delay = strtod(mygetsuboptvalue() ?: "1", NULL);
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
