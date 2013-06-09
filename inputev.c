#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <error.h>
#include <linux/input.h>

#include <libevt.h>
#include "_libio.h"

#define NCODES	(KEY_MAX + 1)

struct inputdev;

struct evbtn {
	struct iopar iopar;
	struct inputdev *dev;
	struct evbtn *next;

	int type;
	int code;
};

struct inputdev {
	struct inputdev *next;
	int fd;
	struct evbtn *btns;
	char file[2];
};

static struct inputdev *inputdevs;

/* list management */
static void add_evbtn(struct evbtn *btn, struct inputdev *dev)
{
	btn->next = dev->btns;
	dev->btns = btn;
	btn->dev = dev;
}

static void del_evbtn(struct evbtn *btn)
{
	struct evbtn **pbtn;

	if (!btn->dev)
		return;
	for (pbtn = &btn->dev->btns; *pbtn; pbtn = &(*pbtn)->next) {
		if (*pbtn == btn) {
			*pbtn = btn->next;
			break;
		}
	}
}

static void add_inputdev(struct inputdev *dev)
{
	dev->next = inputdevs;
	inputdevs = dev;
}

static void del_inputdev(struct inputdev *dev)
{
	struct inputdev **pdev;

	for (pdev = &inputdevs; *pdev; pdev = &(*pdev)->next) {
		if (*pdev == dev) {
			*pdev = dev->next;
			break;
		}
	}
}

/* Device */
static void free_inputdev(struct inputdev *dev)
{
	struct evbtn *btn;

	evt_remove_fd(dev->fd);
	close(dev->fd);
	del_inputdev(dev);
	/*
	 * set evbtns to 0, but don't destroy, they're iopars
	 * and refrenced by the application
	 */
	for (btn = dev->btns; btn; btn = btn->next) {
		btn->iopar.value = 0;
		iopar_set_dirty(&btn->iopar);
		iopar_clr_present(&btn->iopar);
	}
	free(dev);
}

static void evbtn_newdata(struct evbtn *btn, const struct input_event *ev)
{
	if (btn->iopar.state & ST_PUSHBTN) {
		/* pushbtn signalling */
		if (!(int)btn->iopar.value && ev->value)
			iopar_set_dirty(&btn->iopar);
	} else /*if ((int)btn->iopar.value != ev->value) */
		/* regular signalling */
		iopar_set_dirty(&btn->iopar);
	/* always set the correct value, regardless of signalling */
	btn->iopar.value = ev->value;
	/* iopar_set_present(&btn->iopar); */
}

static void read_inputdev(int fd, void *data)
{
	struct inputdev *dev = data;
	struct evbtn *btn;
	int ret;
	struct input_event ev;

	ret = read(fd, &ev, sizeof(ev));
	if (ret <= 0) {
		error(0, ret ? errno : 0, "%s %s%s",
				__func__, dev->file, ret ? "" : ": EOF");
		free_inputdev(dev);
		return;
	}

	for (btn = dev->btns; btn; btn = btn->next) {
		if (btn->type == ev.type && btn->code == ev.code) {
			evbtn_newdata(btn, &ev);
			break;
		}
	}
}

static struct inputdev *lookup_inputdev(const char *spec)
{
	struct inputdev *dev;
	char *file = NULL;

	/* find device file */
	if (!strchr(spec, '/')) {
		int num;
		char *endp;

		num = strtoul(spec, &endp, 10);
		if (!*endp)
			/* number did convert, or empty str */
			asprintf(&file, "/dev/input/event%u", num);
		else
			asprintf(&file, "/dev/input/%s", spec);
		spec = file;
	}

	for (dev = inputdevs; dev; dev = dev->next) {
		if (!strcmp(dev->file, spec))
			goto found;
	}

	dev = zalloc(sizeof(*dev) + strlen(spec));
	strcpy(dev->file, spec);

	dev->fd = open(dev->file, O_RDONLY);
	if (dev->fd < 0)
		error(1, errno, "open %s", dev->file);
	evt_add_fd(dev->fd, read_inputdev, dev);
	add_inputdev(dev);
found:
	if (file)
		free(file);
	return dev;
}

/* button */
static void del_evbtn_hook(struct iopar *iopar)
{
	struct evbtn *btn = (void *)iopar;

	del_evbtn(btn);
	if (!btn->dev->btns)
		/* this was the last button */
		free_inputdev(btn->dev);
	cleanup_libiopar(&btn->iopar);
	free(btn);
}

struct iopar *mkinputevbtn(const char *cstr)
{
	struct evbtn *btn;
	struct inputdev *dev;
	char *str;

	str = strdup(cstr);

	btn = zalloc(sizeof(*btn));
	btn->iopar.del = del_evbtn_hook;
	btn->iopar.set = NULL;
	btn->iopar.value = 0;

	dev = lookup_inputdev(strtok(str, ":;,") ?: "/dev/input/event0");
	btn->type = strtoul(strtok(NULL, ":;,") ?: "1", NULL, 0);
	btn->code = strtoul(strtok(NULL, ":;,") ?: "0", NULL, 0);
	if (!btn->code)
		error(0, 0, "'%s': no code or zero?", cstr);

	/* TODO: test input device for type:code presence */

	/* TODO: test for duplicate btns on this device */

	/* register evbtn */
	add_evbtn(btn, dev);
	/* set as present */
	iopar_set_present(&btn->iopar);
	btn->iopar.state &= ~ST_DIRTY;

	free(str);
	return &btn->iopar;
}
