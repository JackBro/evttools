/**
 *  @file testsid.c
 *  @brief Test Windows SID operations.
 *
 *  This file is in the public domain.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xtnd/xtnd.h>

#include <config.h>
#include <src/sid.h>

/** Convert a text SID to a binary one and back. */
int
tests_sid (int argc, char *argv[])
{
	const char *sid = "S-1-5-21-1085031214-1563985344-725345543";
	void *binary = NULL;
	char *text = NULL;
	size_t length;
	int fail = 1;

	binary = sidToBinary (sid, &length);
	if (!binary)
	{
		puts ("SID test failed on sidToBinary().");
		goto src_testsid_end;
	}

	text = sidToString (binary, length);
	if (!text)
	{
		puts ("SID test failed on sidToString().");
		goto src_testsid_end;
	}

	if (strcmp (sid, text))
	{
		puts ("SID test failed");
		printf ("Original: %s\n", sid);
		printf ("Encoded & decoded: %s\n", text);
		goto src_testsid_end;
	}
	puts ("SID test passed");
	fail = 0;

src_testsid_end:
	if (binary)
		free (binary);
	if (text)
		free (text);

	return fail;
}

