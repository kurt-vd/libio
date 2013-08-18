#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <unistd.h>
#include <fcntl.h>
#include <error.h>
#include <linux/input.h>
#include <sys/poll.h>

#include <libevt.h>
#include "_libio.h"

#define NCODES	(KEY_MAX + 1)

/* bitops */
#define NINTS(x)	(((x) + sizeof(int) -1) / sizeof(int))
#define	getbit(x, ptr)	(((ptr)[(x)/sizeof(int)] >> ((x)%sizeof(int))) & 1)

static inline void setbit(int value, int bit, int *ptr)
{
	ptr += bit/sizeof(int);
	bit %= sizeof(int);
	
	if (value)
		*ptr |= 1 << bit;
	else
		*ptr &= ~(1 << bit);
}

/* decl */
struct inputdev;

static const char *const strflags[] = {
	"debounce",
		#define FL_DEBOUNCE	0x01
	NULL,
};

struct evbtn {
	struct iopar iopar;
	struct inputdev *dev;
	struct evbtn *next;
	int flags;

	int type;
	int code;
};

struct inputdev {
	struct inputdev *next;
	int fd;
	struct evbtn *btns;
	int cache[NINTS(NCODES)];
	char file[2];
};

static struct inputdev *inputdevs;
static double debouncetime = -1; /* init to 'uninitialized */

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

static void evbtn_debounced(void *dat)
{
	struct evbtn *btn = dat;

	iopar_set_dirty(&btn->iopar);
}

static void evbtn_newdata(struct evbtn *btn, const struct input_event *ev)
{
	if ((int)btn->iopar.value != ev->value) {
		if (btn->flags & FL_DEBOUNCE)
			evt_add_timeout(debouncetime, evbtn_debounced, btn);
		else
			iopar_set_dirty(&btn->iopar);
	}

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

	while (1) {
		/* wrong indent, avoided history pollution */
	ret = read(fd, &ev, sizeof(ev));
	if (ret <= 0) {
		if (errno == EAGAIN)
			/* blocked */
			break;
		elog(LOG_ERR, ret ? errno : 0, "%s %s%s",
				__func__, dev->file, ret ? "" : ": EOF");
		free_inputdev(dev);
		return;
	}
		/* keep cache */
		if (ev.type == EV_KEY)
			setbit(ev.value ? 1 : 0, ev.code, dev->cache);

	for (btn = dev->btns; btn; btn = btn->next) {
		if (btn->type == ev.type && btn->code == ev.code)
			evbtn_newdata(btn, &ev);
	}
		/* keep busy? */
		if ((ev.type == EV_SYN) && (ev.code == SYN_REPORT))
			break;
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

	dev->fd = open(dev->file, O_RDONLY /*| O_CLOEXEC*/ | O_NONBLOCK);
	if (dev->fd < 0)
		elog(LOG_CRIT, errno, "open %s", dev->file);
	fcntl(dev->fd, F_SETFD, fcntl(dev->fd, F_GETFD) | FD_CLOEXEC);

	/* flush initial pending events */
	read_inputdev(dev->fd, dev);

	/* register */
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

struct iopar *mkinputevbtn(char *str)
{
	struct evbtn *btn;
	struct inputdev *dev;
	char *tok;
	int flag;

	btn = zalloc(sizeof(*btn));
	btn->iopar.del = del_evbtn_hook;
	btn->iopar.set = NULL;
	btn->iopar.value = 0;

	dev = lookup_inputdev(strtok(str, ":;,") ?: "/dev/input/event0");
	btn->type = strtoul(strtok(NULL, ":;,") ?: "1", NULL, 0);
	btn->code = strtoul(strtok(NULL, ":;,") ?: "0", NULL, 0);
	if (!btn->code)
		elog(LOG_NOTICE, 0, "input: no code or zero?");
	while (1) {
		tok = strtok(NULL, ",");
		if (!tok)
			break;
		flag = strlookup(tok, strflags);
		if (flag >= 0)
			btn->flags |= 1 << flag;
	}

	/* make sure debounce time has been read */
	if ((debouncetime < 0) && (btn->flags & FL_DEBOUNCE)) {
		debouncetime = libio_const("debouncetime");
		if (isnan(debouncetime))
			debouncetime = 0.002;
	}
	/* TODO: test input device for type:code presence */

	/* TODO: test for duplicate btns on this device */

	/* register evbtn */
	add_evbtn(btn, dev);
	/* set as present */
	iopar_set_present(&btn->iopar);
	btn->iopar.state &= ~ST_DIRTY;

	/* test initial state */
	if ((btn->type == EV_KEY) && getbit(btn->code, dev->cache)) {
		btn->iopar.value = 1;
		iopar_set_dirty(&btn->iopar);
	}
	return &btn->iopar;
}
