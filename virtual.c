#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <error.h>

#include <libevt.h>
#include "_libio.h"

/* global data */
static int state;

struct virtualpar {
	struct iopar iopar;
	/* mask of this parameters bit */
	int mask;
	/* mask of related bit, as for virtual teleruptor */
	int mask2;
};

static void prn_virtual_state(int active_mask)
{
	int j, mask;
	char *str, buf[sizeof(int)*10+1];

	for (j = 0, mask = 1, str = buf; j < sizeof(int)*8; ++j, mask <<= 1) {
		if (mask & 0x11111110)
			/* seperator */
			*str++ = ' ';

		if (active_mask & mask)
			*str++ = (state & mask) ? 'X' : '_';
		else
			*str++ = (state & mask) ? 'x' : '-';
	}
	*str++ = 0;
	printf("virtual %.3lf: %s\n", libevt_now(), buf);
}

/* hooks */
static int set_virtual(struct iopar *iopar, double value)
{
	struct virtualpar *virt = (struct virtualpar *)iopar;

	/* test for 1, that is NAN-safe */
	if (value > 0.5)
		state |= virt->mask;
	else
		state &= ~(virt->mask);

	virt->iopar.value = value;
	prn_virtual_state(virt->mask);
	return 0;
}

static void get_virtual(struct iopar *iopar)
{
	struct virtualpar *virt = (struct virtualpar *)iopar;

	virt->iopar.value = (state & virt->mask) ? 1 : 0;
}

static void del_virtual(struct iopar *iopar)
{
	struct virtualpar *virt = (struct virtualpar *)iopar;

	cleanup_libiopar(&virt->iopar);
	free(virt);
}

struct iopar *mkvirtual(const char *str)
{
	struct virtualpar *virt;
	char *endp;

	virt = zalloc(sizeof(*virt));
	virt->mask = 1 << strtoul(str, &endp, 0);
	if (*endp)
		virt->mask2 = 1 << strtoul(endp+1, &endp, 0);

	virt->iopar.del = del_virtual;
	virt->iopar.set = set_virtual;
	virt->iopar.jitget = get_virtual;
	iopar_set_present(&virt->iopar);

	return &virt->iopar;
}

/* teleruptor simulator */
static void on_teleruptor(void *dat)
{
	struct virtualpar *virt = dat;

	state ^= virt->mask2;
	prn_virtual_state(virt->mask2);
}

static int set_virtual_teleruptor(struct iopar *iopar, double value)
{
	struct virtualpar *virt = (struct virtualpar *)iopar;
	int saved_state, ret;

	saved_state = state;
	ret = set_virtual(iopar, value);

	if ((saved_state ^ state) & state & virt->mask)
		evt_add_timeout(0.05, on_teleruptor, virt);
	return ret;
}

struct iopar *mkvirtualteleruptor(const char *str)
{
	struct iopar *iopar;

	iopar = mkvirtual(str);
	if (iopar)
		iopar->set = set_virtual_teleruptor;
	return iopar;
}
