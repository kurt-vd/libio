#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <error.h>

#include "_libio.h"

struct lookup {
	struct lookup *next, *prev;

	char *key;
	char *value;
	char buf[2];

};

static struct {
	struct lookup *first, *last;
	int level;
	int loaded;
} s;

/* load presets of 1 named file */
static void load_presets_file(const char *file)
{
	FILE *fp;
	int ret, linenr = 0;
	char *line = NULL, *key, *value;
	size_t linesize = 0;
	struct lookup *ptr;

	fp = fopen(file, "r");
	if (!fp) {
		if (errno != ENOENT)
			elog(LOG_WARNING, errno, "open %s", file);
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
			elog(LOG_WARNING, 0, "bad line %s:%i", file, linenr);
			continue;
		}
		if (!strcmp(key, "include")) {
			load_presets_file(value);
			continue;
		}
		/* create entry */
		ptr = zalloc(sizeof(*ptr) + ret);
		strcpy(ptr->buf, key);
		ptr->key = ptr->buf;
		ptr->value = ptr->key + strlen(ptr->key) + 1;
		strcpy(ptr->value, value);
		/* add in linked list */
		if (s.last) {
			ptr->prev = s.last;
			ptr->prev->next = ptr;
		}
		s.last = ptr;
		if (!s.first)
			s.first = ptr;
		if (libio_trace >= 2)
			fprintf(stderr, "%s: %s\t%s\n", file, key, value);
	}
	fclose(fp);
	return;
}

/* load all system-wide & user definded presets */
static void load_presets(void)
{
	load_presets_file(".libio-presets");
	load_presets_file("/etc/libio-presets.conf");
	fflush(stderr);
	s.loaded = 1;
}

__attribute__((destructor))
static void free_presets(void)
{
	struct lookup *ptr;

	while (s.first) {
		ptr = s.first;
		s.first = ptr->next;
		free(ptr);
	}
}

const char *libio_get_preset(const char *name)
{
	struct lookup *ptr;

	if (!s.loaded)
		load_presets();
	if (!name)
		return NULL;

	for (ptr = s.first; ptr; ptr = ptr->next) {
		if (!strcmp(ptr->key, name))
			return ptr->value;
	}
	return NULL;
}

struct iopar *mkpreset(char *str)
{
	const char *value;

	if (!s.loaded)
		load_presets();
	if (++s.level > 10) {
		--s.level;
		elog(LOG_NOTICE, 0, "%s: max. nesting reached, are you looping?", __func__);
		return NULL;
	}
	--s.level;
	value = libio_get_preset(str);
	if (!value) {
		elog(LOG_NOTICE, 0, "preset %s not found", str);
		return NULL;
	}
	return create_libiopar(libio_get_preset(str));
}

/* iterator */
const char *libio_next_preset(const char *name)
{
	struct lookup *ptr;

	if (!s.loaded)
		load_presets();

	if (!name)
		return s.first ? s.first->key : NULL;

	for (ptr = s.first; ptr; ptr = ptr->next) {
		if (!strcmp(ptr->key, name))
			break;
	}
	return ptr->next ? ptr->next->key : NULL;
}
