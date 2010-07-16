/**
 *  @file xalloc.c
 *  @brief Defines the xalloc_die function.
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "configure.h"

#include "xalloc.h"


void xalloc_die (const char *fun)
{
	fprintf(stderr, _("Error: %s: %s\n"), fun, strerror(errno));
	exit(EXIT_FAILURE);
}

