/**
 *  @file testbase64.c
 *  @brief Test the base64 implementation.
 *
 *  This file is in the public domain.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xtnd/xtnd.h>

#include <config.h>
#include <src/base64.h>

/** Encode a string and decode it back again. */
int tests_base64 (int argc, char *argv[])
{
	const char string[] = "Příliš žluťoučký kůň úpěl ďábelské ódy.";
	int fail = 0;

	char *enbuff, *debuff;
	int offset, length;
	base64_encodestate enstate;
	base64_decodestate destate;

	base64_init_encodestate (&enstate);
	base64_init_decodestate (&destate);

	enbuff = xmalloc (BASE64_ENCODED_BUFFER_SIZE (strlen (string)));
	offset = base64_encode_block (string, sizeof string, enbuff, &enstate);
	base64_encode_blockend (enbuff + offset, &enstate);

	debuff = xmalloc (BASE64_DECODED_BUFFER_SIZE (strlen (enbuff)));
	length = base64_decode_block (enbuff, strlen (enbuff), debuff, &destate);

	if (length != sizeof string || strcmp (string, debuff))
	{
		puts ("base64 test failed");
		printf ("Original: %s\n", string);
		printf ("Encoded & decoded: %s\n", debuff);
		fail = 1;
	}
	else
		puts("base64 test passed");

	free (enbuff);
	free (debuff);

	return fail;
}

