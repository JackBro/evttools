/**
 *  @file datastruct.c
 *  @brief Some basic data structures.
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#include <stdlib.h>
#include <string.h>

#include "configure.h"

#include "xalloc.h"
#include "datastruct.h"


int bufferAppend (Buffer *__restrict buf,
	const void *__restrict data, size_t length, size_t align)
{
	size_t offset;

	if (!buf->data)
	{
		buf->allocated = (length / BUFFER_BLOCK_SIZE + 1) * BUFFER_BLOCK_SIZE;
		buf->used = buf->cursor = 0;
		buf->data = xmalloc(buf->allocated);
	}
	else
	{
		if (align > 1)
		{
			unsigned int cursorPrev;

			cursorPrev = buf->cursor;
			buf->cursor = (buf->cursor + align - 1) / align * align;

			while (cursorPrev < buf->cursor)
				*((char *) buf->data + cursorPrev++) = '\0';
		}
		if (buf->allocated < buf->cursor + length)
		{
			do
				buf->allocated <<= 1;
			while (buf->allocated < buf->cursor + length);
			buf->data = xrealloc(buf->data, buf->allocated);
		}
	}

	if (data)
		memcpy((char *) buf->data + buf->cursor, data, length);

	offset = buf->cursor;
	buf->cursor += length;

	if (buf->cursor > buf->used)
		buf->used = buf->cursor;

	return offset;
}

int bufferAppendChar (Buffer *buf, char c)
{
	return bufferAppend(buf, &c, sizeof(char), 0);
}

