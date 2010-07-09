/**
 *  @file testdatastruct.c
 *  @brief Test the data structures.
 *
 *  This file is in the public domain.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "configure.h"
#include "datastruct.h"
#include "xalloc.h"

/** Write something and try to read it back. */
int src_testdatastruct (int argc, char *argv[])
{
	Buffer buf;
	int fail = 0;

	bufferInit(&buf);
	bufferAppend(&buf, "abc", 3, 0);
	bufferAppendChar(&buf, 'd');
	bufferAppendChar(&buf, 'e');
	bufferAppendChar(&buf, 'f');
	bufferAppend(&buf, "ghi", 3, 0);
	bufferAppendChar(&buf, '\0');

	if (buf.used != 10
		|| strcmp(buf.data, "abcdefghi"))
	{
		puts("First part of datastruct test failed.");
		fail = 1;
	}

	bufferEmpty(&buf);
	bufferAppendChar(&buf, 'a');
	bufferAppend(&buf, "b", 1, 4);
	bufferAppendChar(&buf, 'c');
	bufferAppend(&buf, "d", 1, 8);

	if (buf.used != 9
		|| memcmp(buf.data, "a\0\0\0bc\0\0d", 9))
	{
		puts("Second part of datastruct test failed.");
		fail = 1;
	}

	bufferDestroy(&buf);
	return fail;
}

