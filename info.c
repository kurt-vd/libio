#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <unistd.h>
#include <error.h>

#include "libio.h"

/* libio-related applets */

static int libio_presets(int argc, char *argv[])
{
	const char *key = NULL;
	int j;

	if (argc > 1)
		for (j = 1; j < argc; ++j)
			printf("%s=%s\n", argv[j], libio_get_preset(argv[j]));
	else
		while ((key = libio_next_preset(key)) != NULL)
			printf("%s=%s\n", key, libio_get_preset(key));
	return 0;
}

__attribute__((constructor))
static void add_default_applets(void)
{
	register_applet("presets", libio_presets);
}
