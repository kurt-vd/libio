#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>

#include <glob.h>
#include <sys/time.h>

#include "lib/libt.h"
#include "lib/libe.h"
#include "_libio.h"

int libio_wait(void)
{
	int ret;

	libio_flush();
	ret = libe_wait(libt_get_waittime());
	if (ret < 0) {
		if (errno != EINTR) {
			elog(LOG_ERR, errno, "libio_wait");
			return ret;
		}
		/* preset ret, to avoid exiting */
		ret = 0;
	} else
		libe_flush();
	libt_flush();
	libio_run_notifiers();
	return ret;
}

void elog(int prio, int errnum, const char *fmt, ...)
{
	char *str;
	va_list va;

	va_start(va, fmt);
	vasprintf(&str, fmt, va);
	va_end(va);
	if (errnum) {
		char *tmp = str;

		asprintf(&str, "%s: %s", tmp, strerror(errnum));
		free(tmp);
	}
	syslog(prio, "%s\n", str);
	free(str);
	if (prio <= LOG_CRIT)
		exit(1);
}

void *zalloc(unsigned int size)
{
	void *ptr;

	ptr = malloc(size);
	if (!ptr)
		elog(LOG_CRIT, errno, "malloc %u", size);
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
				elog(LOG_NOTICE, 0, "%s %s: not unique",
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

char *mygetsuboptvalue(void)
{
	return subopt_value;
}

char *mygetsubopt(char *haystack)
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

static const char *vattr_reads(const char *fmt, va_list va)
{
	FILE *fp;
	char *file;
	static char *line;
	static size_t linesize;
	int ret;

	vasprintf(&file, fmt, va);

	fp = fopen(file, "r");
	if (!fp) {
		free(file);
		return NULL;
	}
	ret = getline(&line, &linesize, fp);
	fclose(fp);
	free(file);
	return (ret < 0) ? NULL : line;
}

const char *attr_reads(const char *fmt, ...)
{
	va_list va;
	const char *result;

	va_start(va, fmt);
	result = vattr_reads(fmt, va);
	va_end(va);
	return result;
}

/* */
int attr_read(int default_value, const char *fmt, ...)
{
	va_list va;
	const char *result;

	va_start(va, fmt);
	result = vattr_reads(fmt, va);
	va_end(va);

	if (!result) {
		elog(LOG_WARNING, errno, "fopen %s r", fmt);
		return default_value;
	}
	return strtol(result, NULL, 0);
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
		elog(LOG_WARNING, errno, "fopen %s w", file);
		ret = -1;
	}
	free(file);
	return ret;
}

/* wildcard match */
static int globerr(const char *path, int errnum)
{
	elog(LOG_WARNING, errnum, "glob ... %s", path);
	return 0;
}

char *findfile(const char *pattern)
{
	int ret;
	glob_t res = {};
	char *resstr = NULL;

	ret = glob(pattern, 0, globerr, &res);
	if (ret == GLOB_NOMATCH)
		;
	else if (ret)
		elog(LOG_ERR, errno, "glob %s", pattern);
	else
		resstr = strdup(res.gl_pathv[0]);
	globfree(&res);
	return resstr;
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
		elog(LOG_WARNING, errno, "setitimer(%.3lf)", v);
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
	{ "sysfs", mksysfspar, },
	{ "led", mkled, },
	{ "backlight", mkbacklight, },
	{ "battery", mkbatterypar, },
	{ "in", mkinputevbtn, },
	{ "button", mkinputevbtn, },
	{ "kbd", mkinputevbtn, },
	{ "applelight", mkapplelight, },
	{ "cpu", mkcpupar, },
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

__attribute__((destructor))
static void free_table(void)
{
	if (table)
		free(table);
}

static inline struct iopar *_lookup_iopar(int iopar_id)
{
	return ((iopar_id >= 0) && (iopar_id < tablesize)) ?
		table[iopar_id] : NULL;
}

struct iopar *lookup_iopar(int iopar_id)
{
	return _lookup_iopar(iopar_id);
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
		elog(LOG_CRIT, errno, "realloc");
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
			elog(LOG_NOTICE, 0, "%s %s failed", __func__, str);
			return NULL;
		}
	}
	elog(LOG_NOTICE, 0, "%s type %.*s unknown", __func__, len, str);
	return NULL;
}

int create_iopar(const char *str)
{
	struct iopar *iopar;

	if (!str)
		return -1;
	iopar = create_libiopar(str);
	if (iopar) {
		iopar->name = strdup(str);
		add_iopar(iopar);
		return iopar->id;
	}
	elog(LOG_NOTICE, 0, "%s %s failed", __func__, str);
	return -1;
}

int create_ioparf(const char *fmt, ...)
{
	int ret;
	char *spec;
	va_list va;

	va_start(va, fmt);
	ret = vasprintf(&spec, fmt, va);
	va_end(va);
	if (ret < 0)
		return ret;
	ret = create_iopar(spec);
	free(spec);
	return ret;
}

void cleanup_libiopar(struct iopar *iopar)
{
	if (iopar->name)
		free(iopar->name);
}

void destroy_iopar(int iopar_id)
{
	struct iopar *iopar = _lookup_iopar(iopar_id);
	struct iopar_notifier *notifier;

	if (!iopar)
		return;
	/* remove notifiers */
	for (; iopar->notifiers; ) {
		notifier = iopar->notifiers;
		iopar->notifiers = iopar->notifiers->next;
		free(notifier);
	}
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

const char *iopar_name(int iopar_id)
{
	struct iopar *iopar = _lookup_iopar(iopar_id);

	return iopar ? iopar->name : NULL;
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

static void iopar_notify(struct iopar *iopar)
{
	struct iopar_notifier *notifier;

	for (notifier = iopar->notifiers; notifier; notifier = notifier->next)
		notifier->fn(notifier->dat);
}

void libio_run_notifiers(void)
{
	int j;

	for (j = 1; j < tablesize; ++j) {
		if (table[j] && (table[j]->state & ST_DIRTY))
			iopar_notify(table[j]);
	}
}

/* direct event notifications */
int iopar_add_notifier(int iopar_id, void (*fn)(void *), void *dat)
{
	struct iopar *iopar = _lookup_iopar(iopar_id);
	struct iopar_notifier *notifier;

	if (!iopar) {
		errno = ENODEV;
		return -1;
	}
	notifier = zalloc(sizeof(*notifier));
	notifier->fn = fn;
	notifier->dat = dat;
	notifier->next = iopar->notifiers;
	iopar->notifiers = notifier;
	return 0;
}

int iopar_del_notifier(int iopar_id, void (*fn)(void *), void *dat)
{
	struct iopar *iopar = _lookup_iopar(iopar_id);
	struct iopar_notifier *notifier, **pnot;

	if (!iopar) {
		errno = ENODEV;
		return -1;
	}
	for (pnot = &iopar->notifiers; *pnot; pnot = &(*pnot)->next) {
		notifier = *pnot;
		if (notifier->dat == dat && notifier->fn == fn) {
			*pnot = notifier->next;
			free(notifier);
			return 0;
		}
	}
	errno = ENOENT;
	return -1;
}
