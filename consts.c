#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include "_libio.h"

struct lookup {
	struct lookup *next;

	char *key;
	char *value;
	double fvalue;
	char buf[2];
};

static struct {
	struct lookup *first, *last;
	int level;
	int loaded;
} s;

static struct lookup *add_entry(const char *key, const char *value)
{
	struct lookup *ptr;
	char *endp;

	/* create entry */
	ptr = zalloc(sizeof(*ptr) + strlen(key) + strlen(value?:"") + 2);
	strcpy(ptr->buf, key);
	ptr->key = ptr->buf;
	if (value) {
		ptr->value = ptr->key + strlen(ptr->key) + 1;
		strcpy(ptr->value, value);
		ptr->fvalue = strtod(ptr->value, &endp);
		/* parse H:M:S */
		if (*endp == ':') {
			ptr->fvalue += strtod(endp+1, &endp) /60;
			if (*endp == ':')
				ptr->fvalue += strtod(endp+1, &endp) /3600;
		}
	}
	/* add in linked list */
	if (s.last)
		s.last->next = ptr;
	s.last = ptr;
	if (!s.first)
		s.first = ptr;
	return ptr;
}

/* load consts of 1 named file */
static void load_consts_file(const char *file)
{
	FILE *fp;
	int ret, linenr = 0;
	char *line = NULL, *key, *value;
	size_t linesize = 0;

	fp = fopen(file, "r");
	if (!fp) {
		if (errno != ENOENT)
			elog(LOG_ERR, errno, "open %s", file);
		return;
	}
	while (!feof(fp)) {
		ret = getline(&line, &linesize, fp);
		if (ret <= 0)
			break;
		line[ret] = 0;
		/* strip trailing newline */
		if (line[ret-1] == '\n')
			line[--ret] = 0;
		/* count lines */
		++linenr;
		/* test for comments or empty lines */
		if (strchr("#\n", *line))
			continue;
		key = strtok(line, "\t ");
		value = strtok(NULL, "\t ");
		if (!key || !value) {
			elog(LOG_NOTICE, 0, "bad line %s:%i", file, linenr);
			continue;
		}
		if (!strcmp(key, "include")) {
			load_consts_file(value);
			continue;
		}
		/* create entry */
		add_entry(key, value);

		if (libio_trace >= 2)
			fprintf(stderr, "%s: %s\t%s\n", file, key, value);
	}
	fclose(fp);
	return;
}

/* load all system-wide & user definded consts */
static void load_consts(void)
{
	load_consts_file(".libio-consts");
	load_consts_file("/etc/libio-consts.conf");
	fflush(stderr);
	s.loaded = 1;
}

__attribute__((destructor))
static void free_consts(void)
{
	struct lookup *ptr;

	while (s.first) {
		ptr = s.first;
		s.first = ptr->next;
		free(ptr);
	}
}

double libio_const(const char *name)
{
	struct lookup *ptr;

	if (!s.loaded)
		load_consts();

	for (ptr = s.first; ptr; ptr = ptr->next) {
		if (!strcmp(name, ptr->key))
			return ptr->fvalue;
	}
	/* warn, and add fake entry */
	elog(LOG_NOTICE, 0, "%s '%s' not found", __func__, name);
	add_entry(name, "nan");
	return NAN;
}

/* iterator */
const char *libio_next_const(const char *name)
{
	struct lookup *ptr;
	static struct lookup *last = NULL;

	if (!s.loaded)
		load_consts();

	if (!name) {
		last = s.first;
		goto done;
	} else if (last && !strcmp(name, last->key)) {
		last = last->next;
		goto done;
	}

	/* lookup */
	for (ptr = s.first; ptr; ptr = ptr->next) {
		if (!strcmp(name, ptr->key)) {
			last = ptr->next;
			break;
		}
	}
done:
	return last ? last->key : NULL;
}
