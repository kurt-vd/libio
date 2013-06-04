#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

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

static void del_led(struct iopar *iopar)
{
	struct led *led = (struct led *)iopar;

	cleanup_libiopar(&led->iopar);
	free(led);
}

struct iopar *mkled(const char *str)
{
	struct led *led;

	led = zalloc(sizeof(*led));
	asprintf(&led->sysfs, "/sys/class/leds/%s/brightness", str);
	led->iopar.del = del_led;
	led->iopar.set = led_set;
	led->max = attr_read(255, "/sys/class/leds/%s/max_brightness", str);
	led->iopar.value = attr_read(0, led->sysfs) / (double)led->max;
	iopar_set_present(&led->iopar);
	return &led->iopar;
}

static int led_set_bool(struct iopar *iopar, double value)
{
	return led_set_bool(iopar, (value < 0.5) ? 0 : 1);
}

struct iopar *mkledbool(const char *str)
{
	struct iopar *iopar;

	iopar = mkled(str);
	if (iopar) {
		iopar->set = led_set_bool;
		iopar->value = (iopar->value < 0.5) ? 0 : 1;
	}
	return iopar;
}

/* backlight is so similar to leds! */
struct iopar *mkbacklight(const char *str)
{
	struct led *led;

	led = zalloc(sizeof(*led));
	asprintf(&led->sysfs, "/sys/class/backlight/%s/brightness", str);
	led->iopar.set = led_set;
	led->max = attr_read(255, "/sys/class/backlight/%s/max_brightness", str);
	/* the default value is read elsewhere compared to leds */
	led->iopar.value = attr_read(led->max / 2,
			"/sys/class/backlight/%s/actual_brightness", str)
		/ (double)led->max;
	iopar_set_present(&led->iopar);
	return &led->iopar;
}
