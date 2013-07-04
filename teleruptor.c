#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <error.h>

#include <libevt.h>
#include "_libio.h"

struct tr {
	struct iopar iopar;
	int out; /* driving output */
	int fdb; /* feedback input */
	int state;
		#define ST_IDLE	0 /* ready to go */
		#define ST_SET	1 /* output set, wait a bit for reading feedback */
		#define ST_WAIT	2 /* output released, wait time to stabilize again */
	int retries;
	int newvalue; /* setpoint */
};

static inline int tobool(double value)
{
	/* >= 0.5 is NAN safe */
	return (value >= 0.5) ?  1 : 0;
}

static void teleruptor_update(struct tr *tr)
{
	double saved_value;

	if (!iopar_present(tr->fdb))
		return;

	saved_value = tr->iopar.value;
	tr->iopar.value = get_iopar(tr->fdb, -1);
	if (tobool(saved_value) != tobool(tr->iopar.value))
		iopar_set_dirty(&tr->iopar);
	iopar_set_present(&tr->iopar);
}

static void teleruptor_handler(void *dat)
{
	struct tr *tr = dat;

	switch (tr->state) {
	case ST_WAIT:
		teleruptor_update(tr);
		if (tobool(tr->iopar.value) == tr->newvalue) {
			/* no problem */
			tr->state = ST_IDLE;
			break;
		}
		if (tr->retries >= 3) {
			error(0, 0, "teleruptor: maximum retry count reached");
			tr->state = ST_IDLE;
			break;
		}
		/* fall trough, retry! */
	case ST_IDLE:
		/* activate teleruptor */
		set_iopar(tr->out, 1);
		evt_add_timeout(0.200, teleruptor_handler, tr);
		tr->state = ST_SET;
		++tr->retries;
		break;
	case ST_SET:
		/* release teleruptor */
		set_iopar(tr->out, 0);
		evt_add_timeout(0.200, teleruptor_handler, tr);

		teleruptor_update(tr);
		tr->state = ST_WAIT;
		break;
	}
}

static int set_teleruptor(struct iopar *iopar, double newvalue)
{
	struct tr *tr = (struct tr *)iopar;

	if (tobool(newvalue) == tr->newvalue)
		return 0;

	tr->newvalue = tobool(newvalue);
	tr->retries = 0;
	if (tr->state == ST_IDLE)
		teleruptor_handler(tr);
	/* else: wait for the timeout */
	return 0; /* no real return code present yet */
}

static void del_teleruptor(struct iopar *iopar)
{
	struct tr *tr = (struct tr *)iopar;

	destroy_iopar(tr->fdb);
	destroy_iopar(tr->out);
}

struct iopar *mkteleruptor(const char *cstr)
{
	char *sstr = strdup(cstr);
	char *pstr;
	struct tr *tr;

	pstr = strchr(sstr, '+');
	if (!pstr) {
		error(0, 0, "bad teleruptor spec '%s'", cstr);
		goto fail_spec;
	}
	*pstr++ = 0;

	tr = zalloc(sizeof(*tr));
	tr->iopar.del = del_teleruptor;
	tr->iopar.set = set_teleruptor;
	tr->iopar.value = FP_NAN;

	tr->out = create_iopar(sstr);
	if (tr->out < 0)
		goto fail_out;
	tr->fdb = create_iopar(pstr);
	if (tr->fdb < 0)
		goto fail_fdb;
	free(sstr);
	/* preset initial state */
	teleruptor_update(tr);
	return &tr->iopar;

fail_fdb:
	destroy_iopar(tr->out);
fail_out:
	free(tr);
fail_spec:
	free(sstr);
	return NULL;
}
