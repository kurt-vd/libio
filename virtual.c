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
	int index;
};

/* hooks */
static int set_virtual(struct iopar *iopar, double value)
{
	struct virtualpar *virt = (struct virtualpar *)iopar;
	char buf[sizeof(int)*10+1], *str;
	int mask, j;

	mask = 1 << virt->index;
	/* test for 1, that is NAN-safe */
	if (value > 0.5)
		state |= mask;
	else
		state &= ~(mask);

	virt->iopar.value = value;

	/* print state */
	for (j = 0, str = buf; j < sizeof(int)*8; ++j) {
		if (j == virt->index)
			*str++ = (state & mask) ? 'X' : '_';
		else
			*str++ = (state & (1 << j)) ? 'x' : '-';
		if ((j & 3) == 3)
			*str++ = ' ';
	}
	*str++ = 0;
	printf("virtual %.3lf: %s\n", libevt_now(), buf);
	return 0;
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

	virt = zalloc(sizeof(*virt));
	virt->index = strtoul(str, NULL, 0);

	virt->iopar.del = del_virtual;
	virt->iopar.set = set_virtual;
	virt->iopar.value = state >> virt->index;

	return &virt->iopar;
}
