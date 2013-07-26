#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <error.h>

struct applet {
	const char *name;
	int (*fn)(int argc, char *argv[]);
};

static struct applet *applets;
static int napplets;
static int sapplets;

void register_applet(const char *name, int (*fn)(int, char *[]))
{
	if (napplets >= sapplets) {
		sapplets += 16;
		applets = realloc(applets, sizeof(*applets)*sapplets);
		if (!applets)
			error(1, errno, "realloc");
	}
	applets[napplets++] = (struct applet){ .name = name, .fn = fn,};
}

static const struct applet *find_applet(const char *name)
{
	const struct applet *result = NULL;
	int j, len;

	len = strlen(name ?: "");
	if (!len)
		return NULL;
	for (j = 0; j < napplets; ++j) {
		if (!strncasecmp(name, applets[j].name, len)) {
			if (result)
				/* duplicate applet */
				return NULL;
			result = applets+j;
		}
	}
	return result;
}

int main(int argc, char *argv[])
{
	char *strapp;
	const struct applet *applet;
	int j;

	strapp = strrchr(argv[0], '/');
	strapp = strapp ? strapp+1 : argv[0];

	applet = find_applet(strapp);
	if (!applet && argv[1]) {
		applet = find_applet(argv[1]);
		++argv;
		--argc;
	}
	if (applet) {
#if 0
		program_invocation_name = applet->name;
#endif
		return applet->fn(argc, argv);
	}

	fprintf(stderr, "Available applets:\n");
	for (j = 0; j < napplets; ++j)
		fprintf(stderr, "\t%s\n", applets[j].name);
	return 1;
}
