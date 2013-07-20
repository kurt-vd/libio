#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>

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

int strlookup(const char *str, const char *const table[])
{
	int j, result = -1, len = strlen(str ?: "");

	if (!len)
		return -1;

	for (j = 0; *table; ++j, ++table) {
		if (!strncasecmp(str, *table, len)) {
			if (result >= 0) {
				error(0, 0, "%s %s: not unique",
						__func__, str);
				return -1;
			}
			result = j;
		}
	}
	return result;
}

static char *subopt_value;
static char *subopt_haystack;

const char *mygetsuboptvalue(void)
{
	return subopt_value;
}

const char *mygetsubopt(char *haystack)
{
	char *key = haystack ?: subopt_haystack;

	if (!key)
		return NULL;
	subopt_haystack = strchr(key, ',');
	if (subopt_haystack)
		*haystack++ = 0;
	subopt_value = strchr(key, '=');
	if (subopt_value)
		*subopt_value++ = 0;
	return key;
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
	struct iopar *(*create)(char *str);
} iotypes[] = {
	{ "preset", mkpreset, },
	{ "virtual", mkvirtual, },
	{ "shared", mkshared, },
	{ "led", mkled, },
	{ "backlight", mkbacklight, },
	{ "in", mkinputevbtn, },
	{ "button", mkinputevbtn, },
	{ "kbd", mkinputevbtn, },
	{ "applelight", mkapplelight, },
	{ "dmotor", mkmotordir, },
	{ "pmotor", mkmotorpos, },

	{ "teleruptor", mkteleruptor, },
	{ "vteleruptor", mkvirtualteleruptor, },

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
	struct iopar *iopar;

	sep = strchr(str, ':');
	if (!sep)
		return mkpreset(strdupa(str));

	len = sep - str;
	iostr = sep + 1;
	for (j = 0; iotypes[j].prefix; ++j) {
		if (!strncmp(iotypes[j].prefix, str, len)) {
			iopar = iotypes[j].create(strdupa(iostr));
			if (iopar)
				return iopar;
			error(0, 0, "%s %s failed", __func__, str);
			return NULL;
		}
	}
	error(0, 0, "%s type %.*s unknown", __func__, len, str);
	return NULL;
}

int create_iopar_type(const char *type, const char *spec)
{
	int j;
	struct iopar *iopar;

	for (j = 0; iotypes[j].prefix; ++j) {
		if (!strcmp(iotypes[j].prefix, type)) {
			iopar = iotypes[j].create(strdupa(spec));
			if (iopar) {
				add_iopar(iopar);
				return iopar->id;
			}
			error(0, 0, "%s %s %s failed", __func__, type, spec);
			return -1;
		}
	}
	error(0, 0, "%s type %s unknown", __func__, spec);
	return -1;
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
	error(0, 0, "%s %s failed", __func__, str);
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
double get_iopar(int iopar_id)
{
	struct iopar *iopar = _lookup_iopar(iopar_id);

	if (!iopar) {
		errno = ENODEV;
		return NAN;
	}
	if (iopar->jitget)
		iopar->jitget(iopar);
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
		return 0;
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

void libio_flush(void)
{
	int j;

	longdet_flush();
	netio_sync();
	for (j = 1; j < tablesize; ++j) {
		if (table[j])
			table[j]->state &= ~ST_DIRTY;
	}
}
