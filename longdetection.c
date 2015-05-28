#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <error.h>

#include "lib/libt.h"
#include "_libio.h"

struct ld {
	struct ld *next;
	int id;
	int value;
	int oldvalue;
	int instate;
	double delay;
};

/* globals */
static struct ld *longdet_list;
static int longdet_nr;

void longdet_flush(void)
{
	struct ld *ld;

	for (ld = longdet_list; ld; ld = ld->next) {
		ld->oldvalue = ld->value;
		if (ld->value == SHORTPRESS)
			/* shortpress has no trigger to clear it's status */
			ld->value = 0;
	}
}

static struct ld *find_ld(int id)
{
	struct ld *ld;

	for (ld = longdet_list; ld; ld = ld->next) {
		if (ld->id == id)
			return ld;
	}
	return NULL;
}

int longdet_state(int id)
{
	struct ld *ld = find_ld(id);

	return ld ? ld->value : 0;
}

int longdet_edge(int id)
{
	struct ld *ld = find_ld(id);

	return ld && (ld->oldvalue != ld->value);
}

static inline int tobool(double value)
{
	/* >= 0.5 is NAN safe */
	return (value >= 0.5) ?  1 : 0;
}

static void longdetection_timeout(void *dat)
{
	((struct ld *)dat)->value = LONGPRESS;
}

void set_longdet(int id, double value)
{
	struct ld *ld = find_ld(id);
	int ivalue;

	if (!ld)
		return;
	ivalue = tobool(value);
	if (ld->instate == ivalue)
		return;
	if (ivalue) {
		libt_add_timeout(ld->delay, longdetection_timeout, ld);
	} else if (ld->instate) {
		/* released */
		if (ld->value == LONGPRESS) {
			/* clear long press event */
			ld->value = 0;
		} else {
			/* raise short press event */
			libt_remove_timeout(longdetection_timeout, ld);
			ld->value = SHORTPRESS;
		}
	}
	ld->instate = ivalue;
}

int new_longdet(void)
{
	static double default_delay;
	static int loaded = 0;

	if (!loaded) {
		default_delay = libio_const("longpress");
		if (isnan(default_delay))
			default_delay = 0.5;
		loaded = 1;
	}
	return new_longdet1(default_delay);
}

int new_longdet1(double delay)
{
	struct ld *ld, *lp;

	ld = zalloc(sizeof(*ld));
	ld->id = ++longdet_nr;
	ld->delay = delay;
	/* append in linked list */
	for (lp = longdet_list; lp && lp->next; lp = lp->next) ;
	if (lp)
		lp->next = ld;
	else
		longdet_list = ld;
	return ld->id;
}

