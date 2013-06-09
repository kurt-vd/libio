#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <error.h>

#include <libevt.h>
#include "_libio.h"

/* MOTOR TYPES */
static const char *const motortypes[] = {
	"updown",
		/* UPDOWN: out1 for up, out2 for down */
		#define TYPE_UPDOWN	0
	"godir",
		/* GODIR: out1 to drive, out2 for direction */
		#define TYPE_GODIR	1
	NULL,
};

static int lookup_motor_type(const char *str)
{
	int len = strlen(str);
	const char *const *table;

	if (!len)
		return 0; /* default type */
	for (table = motortypes; *table; ++table) {
		if (!strncmp(*table, str, len))
			return table - motortypes;
	}
	return -1;
}

/* MOTOR struct */
struct motor {
	/* usage counter of this struct */
	int refcnt;
	int state;
		#define ST_IDLE		0
		#define ST_BUSY		1
		#define ST_WAIT		2 /* implement cooldown period */
	#define COOLDOWN_TIME	0.2
	/* how to combine 2 outputs to 1 motor */
	int type;
	/* backend gpio/pwm's */
	int out1, out2;

	/* direction control */
	struct iopar dirpar;
	/* requested, but postponed, direction */
	int reqspeed;

	/* position control */
	struct iopar pospar;
	/* time of last sample */
	double lasttime;
		#define HYST	0.002
		#define UPDINT	0.5
		#define WAITTIME 0.5
	/* (calibrated) maximum that equals 1.0 */
	double maxval;
};

/* find motor struct */
static inline struct motor *pospar2motor(struct iopar *iopar)
{
	return (void *)(((char *)iopar) - offsetof(struct motor, pospar));
}
static inline struct motor *dirpar2motor(struct iopar *iopar)
{
	return (void *)(((char *)iopar) - offsetof(struct motor, dirpar));
}

/* generic control */
static inline double motor_curr_speed(struct motor *mot)
{
	return mot->dirpar.value;
}

static inline double motor_curr_position(struct motor *mot)
{
	return mot->pospar.value;
}

static inline int motor_moving(struct motor *mot)
{
	return fpclassify(motor_curr_speed(mot)) != FP_ZERO;
}

/* get value */
static void motor_update_position(struct motor *mot)
{
	double currtime;

	currtime = libevt_now();
	mot->pospar.value += motor_curr_speed(mot) * (currtime - mot->lasttime);
	mot->lasttime = currtime;
	if (motor_curr_speed(mot) != 0)
		iopar_set_dirty(&mot->pospar);
}

/* actually set the GPIO's for the motor. caller should deal with timeouts */
static void change_motor_speed(struct motor *mot, double speed)
{
	if (speed == 0) {
		set_iopar(mot->out2, 0);
		set_iopar(mot->out1, 0);
		/* final update of value */
		motor_update_position(mot);
	} else if (speed > 0) {
		set_iopar(mot->out2, 0);
		set_iopar(mot->out1, speed);
	} else if (speed < 0) {
		if (mot->type == TYPE_GODIR) {
			set_iopar(mot->out2, 1);
			set_iopar(mot->out1, speed);
		} else {
			set_iopar(mot->out1, 0);
			set_iopar(mot->out2, speed);
		}
	}
	motor_update_position(mot);
	mot->dirpar.value = speed;
	iopar_set_dirty(&mot->dirpar);
}

static inline double next_wakeup(struct motor *mot)
{
	double result;
	double endpoint;

	endpoint = (motor_curr_speed(mot) < 0) ? (0 - HYST) : (1 + HYST);

	result = mot->lasttime + fabs(endpoint - motor_curr_position(mot)) - libevt_now();
	/* optimization */
	if (result <= UPDINT)
		return result;
	else if (result < UPDINT*2)
		return result/2;
	else
		return UPDINT;
}

static void motor_handler(void *dat)
{
	struct motor *mot = dat;
	double oldspeed;

	motor_update_position(mot);

	/* test for end-of-course statuses */
	if ((motor_curr_speed(mot) < 0) && (motor_curr_position(mot) <= 0-HYST))
		mot->reqspeed = 0;
	else if ((motor_curr_speed(mot) > 0) && (motor_curr_position(mot) >= 1+HYST))
		mot->reqspeed = 0;

	oldspeed = motor_curr_speed(mot);
	change_motor_speed(mot, mot->reqspeed);
	if (motor_curr_speed(mot) != 0) {
		mot->state = ST_BUSY;
		evt_add_timeout(next_wakeup(mot), motor_handler, mot);
	} else if (oldspeed != 0) {
		/* go into cooldown state for a bit */
		mot->state = ST_WAIT;
		evt_add_timeout(COOLDOWN_TIME, motor_handler, mot);
	} else
		/* return to idle */
		mot->state = ST_IDLE;
}

/* iopar methods */
static int set_motor_pos(struct iopar *iopar, double newvalue)
{
#if 0
	struct motor *mot = pospar2motor(iopar);
#endif

	return -1;
}

static int set_motor_dir(struct iopar *iopar, double newvalue)
{
	struct motor *mot = dirpar2motor(iopar);

	/* test for end-of-course positions */
	if (((newvalue > 0) && (motor_curr_position(mot) >= 1)) ||
			((newvalue < 0) && (motor_curr_position(mot) <= 0))) {
		/* trigger update with old value */
		iopar_set_dirty(&mot->dirpar);
		return -1;
	}
	mot->reqspeed = newvalue;
	if (mot->state != ST_WAIT)
		motor_handler(mot);
	return 0;
}

static void del_motor_dir(struct iopar *iopar)
{
	struct motor *mot = dirpar2motor(iopar);

	if (!--mot->refcnt) {
		destroy_iopar(mot->out1);
		destroy_iopar(mot->out2);
	}
}
static void del_motor_pos(struct iopar *iopar)
{
	return del_motor_dir(&(pospar2motor(iopar))->dirpar);
}


/*
 * constructors:
 * mkmotordir is supposed to be called first,
 * and mkmotorpos is cached to be returned directly after that.
 */
static struct iopar *next_pospar;

struct iopar *mkmotorpos(const char *cstr)
{
	struct iopar *result;

	(void)cstr;
	result = next_pospar;
	next_pospar = NULL;
	return result;
}

struct iopar *mkmotordir(const char *cstr)
{
	struct motor *mot;
	char *tok;
	int ntok;
	char *sstr = strdup(cstr);

	mot = zalloc(sizeof(*mot));
	mot->dirpar.del = del_motor_dir;
	mot->dirpar.set = set_motor_dir;
	mot->dirpar.value = 0;

	mot->pospar.del = del_motor_pos;
	mot->pospar.set = set_motor_pos;
	mot->pospar.value = 0;

	for (ntok = 0, tok = strtok(sstr, "+"); tok; ++ntok, tok = strtok(NULL, "+"))
	switch (ntok) {
	case 0:
		mot->type = lookup_motor_type(tok);
		if (mot->type < 0) {
			error(0, 0, "%s: bad motor type '%s'", __func__, tok);
			goto fail_config;
		}
		break;
	case 1:
		mot->maxval = strtod(tok, NULL);
		break;
	case 2:
		mot->out1 = create_iopar(tok);
		if (mot->out1 <= 0) {
			error(0, 0, "%s: bad output '%s'", __func__, tok);
			goto fail_config;
		}
		break;
	case 3:
		mot->out2 = create_iopar(tok);
		if (mot->out2 <= 0) {
			error(0, 0, "%s: bad output '%s'", __func__, tok);
			goto fail_config;
		}
		break;
	default:
		break;
	}
	if (ntok < 4) {
		error(0, 0, "%s: need arguments \"[MOTORTYPE(updown|godir)]+MAXVAL+OUT1+OUT2\"", __func__);
		goto fail_config;
	}
	free(sstr);
	iopar_set_present(&mot->pospar);
	iopar_set_present(&mot->dirpar);
	/* save next pospar */
	next_pospar = &mot->pospar;
	return &mot->dirpar;

fail_config:
	if (mot->out2)
		destroy_iopar(mot->out2);
	if (mot->out1)
		destroy_iopar(mot->out1);
	free(mot);
	free(sstr);
	return NULL;
}
