#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <unistd.h>
#include <error.h>

#include "libio.h"

/* libio-related applets */

__attribute__((constructor))
static void add_default_applets(void)
{
}
