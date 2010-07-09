/**
 *  @file testwidechar.c
 *  @brief Test wide character operations.
 *
 *  This file is in the public domain.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "configure.h"
#include "widechar.h"
#include "xalloc.h"

/** Encode a string and decode it back again. */
int src_testwidechar (int argc, char *argv[])
{
	const char *string = "Tak mówią wszyscy wariaci: że są normalni.";
	uint16_t *wideString = NULL;
	char *mbString = NULL;
	int length, fail = 1;

	/* This function cannot assure the string won't be touched
	 * because of the iconv interface. Since we expect it not to change
	 * the input string, let's cast it to a non-const pointer.
	 */
	length = encodeMBString((char *) string, &wideString);
	if (!length || !wideString)
	{
		puts("widechar test failed on encodeMBString().");
		goto src_testwidechar_end;
	}

	length = decodeWideString(wideString, length, &mbString);
	if (!length || !mbString)
	{
		puts("widechar test failed on decodeWideString().");
		goto src_testwidechar_end;
	}

	if (strcmp(string, mbString))
	{
		puts("widechar test failed");
		printf("Original: %s\n", string);
		printf("Encoded & decoded: %s\n", mbString);
		goto src_testwidechar_end;
	}
	puts("widechar test passed");
	fail = 0;

src_testwidechar_end:
	if (wideString)
		free(wideString);
	if (mbString)
		free(mbString);

	return fail;
}

