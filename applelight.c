#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include <unistd.h>
#include <fcntl.h>
#include <error.h>
#include <sys/timerfd.h>

#include <libevt.h>

#include "_libio.h"

/* apple light-sensor sysfs output */
struct applelight {
	struct iopar iopar;
	char sysfs[2];
};

static void applelight_read(struct applelight *al, int warn)
{
	int fd, ret, ivalue;
	char buf[32];

	/* warn if requested, or param is present */
	warn = warn ?: al->iopar.state & ST_PRESENT;

	fd = open(al->sysfs, O_RDONLY);
	if (fd < 0) {
		/* avoid alerting too much */
		if (warn)
			error(0, errno, "open %s", al->sysfs);
		goto fail_open;
	}
	ret = read(fd, buf, sizeof(buf)-1);
	if (ret < 0) {
		/* avoid alerting too much */
		if (warn)
			error(0, errno, "read %s", al->sysfs);
		goto fail_read;
	}
	close(fd);
	buf[ret] = 0;

	ivalue = strtoul(buf+1, NULL, 10);
	if (ivalue != (int)(al->iopar.value * 255)) {
		iopar_set_dirty(&al->iopar);
		al->iopar.value = ivalue / 255.0;
	}
	/* mark as present */
	iopar_set_present(&al->iopar);
	return;

fail_read:
	close(fd);
fail_open:
	iopar_clr_present(&al->iopar);
}

static void applelight_timeout(void *data)
{
	applelight_read(data, 0);
	evt_repeat_timeout(1, applelight_timeout, data);
}

static void del_applelight(struct iopar *iopar)
{
	struct applelight *al = (void *)iopar;

	evt_remove_timeout(applelight_timeout, al);
	cleanup_libiopar(&al->iopar);
	free(al);
}

struct iopar *mkapplelight(const char *sysfs)
{
	struct applelight *al;

	al = zalloc(sizeof(*al) + strlen(sysfs));
	al->iopar.del = del_applelight;
	al->iopar.set = NULL;
	/* force the first read to mark value as dirty */
	al->iopar.value = -1;
	strcpy(al->sysfs, sysfs);

	/* read initial value & schedule next */
	applelight_read(al, 1);
	evt_add_timeout(1, applelight_timeout, al);
	return &al->iopar;
}
