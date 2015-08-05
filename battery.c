#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>

#include <unistd.h>
#include <fcntl.h>

#include "lib/libt.h"

#include "_libio.h"

/* bat file for input or output */
struct batpar {
	struct iopar iopar;
	int flags;
	long lastnum, lastdenom;
	double delay;

	char *id;
	char *numerator;
	char *denominator;
	char saved[2];
};

static const char *const strflags[] = {
	"delay",
		#define ID_DELAY	0
	NULL,
};

static void batpar_read(struct batpar *bp, int warn)
{
	long num, denom;
	const char *sval;

	/* warn if requested, or param is present */
	warn |= bp->iopar.state & ST_PRESENT;

	sval = attr_reads("/sys/class/power_supply/%s/%s",
			bp->id, bp->numerator);
	if (!sval)
		goto fail_read;
	num = strtol(sval, NULL, 0);
	sval = attr_reads("/sys/class/power_supply/%s/%s",
			bp->id, bp->denominator);
	if (!sval)
		goto fail_read;
	denom = strtol(sval, NULL, 0);

	if (!(bp->iopar.state & ST_PRESENT) ||
			(num != bp->lastnum) || (denom != bp->lastdenom)) {
		bp->lastnum = num;
		bp->lastdenom = denom;
		bp->iopar.value = num*1.0/denom;
		iopar_set_dirty(&bp->iopar);
	}
	/* mark as present */
	iopar_set_present(&bp->iopar);
	return;

fail_read:
	iopar_clr_present(&bp->iopar);
}

static void batpar_timeout(void *data)
{
	struct batpar *bp = data;

	batpar_read(bp, 0);
	libt_repeat_timeout(bp->delay, batpar_timeout, bp);
}

static void del_batpar(struct iopar *iopar)
{
	struct batpar *bp = (void *)iopar;

	libt_remove_timeout(batpar_timeout, bp);
	cleanup_libiopar(&bp->iopar);
	free(bp);
}

struct iopar *mkbatterypar(char *spec)
{
	struct batpar *bp;
	const char *tok;
	int flag;

	bp = zalloc(sizeof(*bp) + strlen(spec));
	bp->iopar.del = del_batpar;
	strcpy(bp->saved, spec);
	bp->delay = 60;

	bp->id = strtok(bp->saved, ",");
	bp->numerator = strtok(NULL, ",");
	bp->denominator = strtok(NULL, ",");
	if (!bp->id || !bp->numerator || !bp->denominator)

	while (1) {
		tok = mygetsubopt(strtok(NULL, ","));
		if (!tok)
			break;
		flag = strlookup(tok, strflags);
		if (flag < 0)
			elog(LOG_CRIT, 0, "flag %s unknown", tok);
		switch (flag) {
		case ID_DELAY:
			bp->delay = strtod(mygetsuboptvalue() ?: "60", NULL);
			break;
		default:
			bp->flags |= 1 << flag;
			break;
		}
	}

	/* read initial value & schedule next */
	batpar_read(bp, 1);
	libt_add_timeout(bp->delay, batpar_timeout, bp);
	return &bp->iopar;
}
