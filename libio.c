#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include <error.h>
#include <sys/time.h>

#include "_libio.h"

void *zalloc(unsigned int size)
{
	void *ptr;

	ptr = malloc(size);
	if (!ptr)
		error(1, errno, "malloc %u", size);
	memset(ptr, 0, size);
	return ptr;
}

/* */
int attr_read(int default_value, const char *fmt, ...)
{
	FILE *fp;
	int value;
	char *file;
	va_list va;

	va_start(va, fmt);
	vasprintf(&file, fmt, va);
	va_end(va);

	fp = fopen(file, "r");
	if (fp) {
		fscanf(fp, "%i", &value);
		fclose(fp);
	} else {
		error(0, errno, "fopen %s r", file);
		value = default_value;
	}
	free(file);
	return value;
}

int attr_write(int value, const char *fmt, ...)
{
	FILE *fp;
	int ret;
	char *file;
	va_list va;

	va_start(va, fmt);
	vasprintf(&file, fmt, va);
	va_end(va);

	fp = fopen(file, "w");
	if (fp) {
		ret = fprintf(fp, "%i\n", value);
		fclose(fp);
	} else {
		error(0, errno, "fopen %s w", file);
		ret = -1;
	}
	free(file);
	return ret;
}

/* setup ITIMER */
int schedule_itimer(double v)
{
	int ret;
	struct itimerval it = {};
	long il;

	il = v*1e6;
	it.it_value.tv_sec  = il/1000000;
	it.it_value.tv_usec = il%1000000;
	if (!it.it_value.tv_sec && !it.it_value.tv_usec) {
		it.it_value.tv_sec = 1;
		it.it_value.tv_usec = 0;
	}

	ret = setitimer(ITIMER_REAL, &it, 0);
	if (ret)
		error(0, errno, "setitimer(%.3lf)", v);
	return ret;
}

/* iopars */
static const struct {
	const char *prefix;
	struct iopar *(*create)(const char *str);
} iotypes[] = {
	{ "preset", mkpreset, },
	{ "virtual", mkvirtual, },
	{ "led", mkled, },
	{ "bled", mkledbool, },
	{ "backlight", mkbacklight, },
	{ "button", mkinputevbtn, },
	{ "kbd", mkinputevbtn, },
	{ "applelight", mkapplelight, },

	{ "netio", mknetiolocal, },
	{ "unix", mknetiounix, },
	{ "udp4", mknetioudp4, },
	{ "udp6", mknetioudp6, },
	{ "udp", mknetioudp4, },
	{ },
};

/* tracing */
int libio_trace;

void libio_set_trace(int value)
{
	libio_trace = value;
}

/* tables */
static struct iopar **table;
static int tablesize, tablespot = 1;

static inline struct iopar *_lookup_iopar(int iopar_id)
{
	return ((iopar_id >= 0) && (iopar_id < tablesize)) ?
		table[iopar_id] : NULL;
}

struct iopar *lookup_iopar(int iopar_id)
{
	return ((iopar_id >= 0) && (iopar_id < tablesize)) ?
		table[iopar_id] : NULL;
}

static void add_iopar(struct iopar *iopar)
{
	int oldtablesize = tablesize;

	for (; tablespot < tablesize; ++tablespot) {
		if (!table[tablespot])
			goto empty_spot;
	}
	tablesize += 16;
	table = realloc(table, sizeof(*table)*tablesize);
	if (!table)
		error(1, errno, "realloc");
	memset(table + oldtablesize, 0,
			sizeof(*table)*(tablesize - oldtablesize));
empty_spot:
	iopar->id = tablespot;
	table[tablespot] = iopar;
	++tablespot;
}

struct iopar *create_libiopar(const char *str)
{
	const char *sep, *iostr;
	int j, len;

	sep = strchr(str, ':');
	if (!sep)
		return mkpreset(str);

	len = sep - str;
	iostr = sep + 1;
	for (j = 0; iotypes[j].prefix; ++j) {
		if (!strncmp(iotypes[j].prefix, str, len))
			return iotypes[j].create(iostr);
	}
	return NULL;
}

int create_iopar(const char *str)
{
	struct iopar *iopar;

	if (!str)
		return -1;
	iopar = create_libiopar(str);
	if (iopar) {
		add_iopar(iopar);
		return iopar->id;
	}
	error(1, 0, "%s %s failed", __func__, str);
	return -1;
}

void cleanup_libiopar(struct iopar *iopar)
{
}

void destroy_iopar(int iopar_id)
{
	struct iopar *iopar = _lookup_iopar(iopar_id);

	if (!iopar)
		return;
	/* iopar_id has proven valid here */
	if (iopar->del)
		iopar->del(iopar);
	else
		/* default cleanup: we cannot do anything else here */
		cleanup_libiopar(iopar);

	table[iopar_id] = NULL;
	if (iopar_id < tablespot)
		tablespot = iopar_id;
}

/* iopar use */
double get_iopar(int iopar_id, double default_value)
{
	struct iopar *iopar = _lookup_iopar(iopar_id);

	if (!iopar) {
		errno = ENODEV;
		return default_value;
	}
	return iopar->value;
}

int set_iopar(int iopar_id, double value)
{
	struct iopar *iopar = _lookup_iopar(iopar_id);
	int ret;
	double saved_value;

	if (!iopar) {
		errno = ENODEV;
		return -1;
	}
	if (!iopar->set) {
		errno = ENOTSUP;
		return -1;
	}

	saved_value = iopar->value;
	ret = iopar->set(iopar, value);
	if ((ret >= 0) && (iopar->value != saved_value))
		iopar->state |= ST_DIRTY;
	return ret;
}

int iopar_dirty(int iopar_id)
{
	struct iopar *iopar = _lookup_iopar(iopar_id);

	if (!iopar) {
		errno = ENODEV;
		return -1;
	}
	return (iopar->state & ST_DIRTY) ? 1 : 0;
}

int iopar_present(int iopar_id)
{
	struct iopar *iopar = _lookup_iopar(iopar_id);

	if (!iopar) {
		errno = ENODEV;
		return -1;
	}
	return (iopar->state & ST_PRESENT) ? 1 : 0;
}

int iopar_set_writeable(int iopar_id)
{
	struct iopar *iopar = _lookup_iopar(iopar_id);

	if (!iopar) {
		errno = ENODEV;
		return -1;
	} else {
		iopar->state |= ST_WRITABLE;
		return 0;
	}
}

int iopar_set_pushbtn(int iopar_id)
{
	struct iopar *iopar = _lookup_iopar(iopar_id);

	if (!iopar) {
		errno = ENODEV;
		return -1;
	} else {
		iopar->state |= ST_PUSHBTN;
		return 0;
	}
}

void libio_flush(void)
{
	int j;

	netio_sync();
	for (j = 1; j < tablesize; ++j) {
		if (table[j])
			table[j]->state &= ~ST_DIRTY;
	}
}
