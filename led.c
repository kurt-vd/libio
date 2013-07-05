#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

#include <error.h>

#include "_libio.h"

struct led {
	struct iopar iopar;
	char *sysfs;
	int max;
};

static inline int fit_int(int val, int min, int max)
{
	return (val < min) ? min : ((val > max) ? max : val);
}

static int led_set(struct iopar *iopar, double value)
{
	struct led *led = (struct led *)iopar;
	int ret;
	FILE *fp;

	/* NAN may be passed to release control */
	if (isnan(value))
		value = 0;
	fp = fopen(led->sysfs, "w");
	if (!fp) {
		if (led->iopar.state & ST_PRESENT)
			error(0, errno, "fopen %s", led->sysfs);
		goto fail_open;
	}
	ret = fprintf(fp, "%u", fit_int(value*led->max, 0, led->max));
	if (ret < 0) {
		if (led->iopar.state & ST_PRESENT)
			error(0, errno, "fwrite %s", led->sysfs);
		goto fail_write;
	}
	fclose(fp);
	led->iopar.value = value;
	iopar_set_present(&led->iopar);
	return ret;

fail_write:
	fclose(fp);
fail_open:
	iopar_clr_present(&led->iopar);
	return -1;
}

static int led_set_bool(struct iopar *iopar, double value)
{
	return led_set_bool(iopar, (value < 0.5) ? 0 : 1);
}

static void del_led(struct iopar *iopar)
{
	struct led *led = (struct led *)iopar;

	cleanup_libiopar(&led->iopar);
	free(led);
}

static const char *const led_opts[] = {
	"bool",
	NULL,
};

struct iopar *mkled(char *str)
{
	struct led *led;
	const char *name = strtok(str, ",");

	led = zalloc(sizeof(*led));
	asprintf(&led->sysfs, "/sys/class/leds/%s/brightness", name);
	led->iopar.del = del_led;
	led->iopar.set = led_set;
	led->max = attr_read(255, "/sys/class/leds/%s/max_brightness", name);
	led->iopar.value = attr_read(0, led->sysfs) / (double)led->max;
	iopar_set_present(&led->iopar);

	while ((str = strtok(NULL, ",")) != NULL)
	switch (strlookup(str, led_opts)) {
	case 0:
		led->iopar.set = led_set_bool;
		led->iopar.value = (led->iopar.value < 0.5) ? 0 : 1;
		break;
	}
	return &led->iopar;
}

/* backlight is so similar to leds! */
struct iopar *mkbacklight(char *str)
{
	struct led *led;
	const char *name = strtok(str, ",");

	led = zalloc(sizeof(*led));
	asprintf(&led->sysfs, "/sys/class/backlight/%s/brightness", name);
	led->iopar.set = led_set;
	led->max = attr_read(255, "/sys/class/backlight/%s/max_brightness", name);
	/* the default value is read elsewhere compared to leds */
	led->iopar.value = attr_read(led->max / 2,
			"/sys/class/backlight/%s/actual_brightness", name)
		/ (double)led->max;
	iopar_set_present(&led->iopar);
	return &led->iopar;
}
