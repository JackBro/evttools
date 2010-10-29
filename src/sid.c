/**
 *  @file sid.c
 *  @brief Conversion routines for Windows SID's.
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#include <xtnd/xtnd.h>

#include <config.h>
#include "datastruct.h"
#include "sid.h"


/** Convert that large big-endian number to a long long int. */
#define IDENTIFIER_AUTHORITY_TO_LLINT(ia) ( \
	(long long) (ia)[5]       | \
	(long long) (ia)[4] << 8  | \
	(long long) (ia)[3] << 16 | \
	(long long) (ia)[2] << 24 | \
	(long long) (ia)[1] << 32 | \
	(long long) (ia)[0] << 40 )

/** SID structure header. */
typedef struct
{
	/** The revision of SID. This value should be 1. */
	uint8_t revision;
	/** The number of subauthorities following the structure. */
	uint8_t subAuthorityCnt;
	/** The identifier authority. */
	uint8_t identifierAuthority[6];
/*
	uint32_t subAuthorities[];
 */
}
ATTRIBUTE_PACKED Sid;


static int snprintfExtend (char **out, size_t *size, unsigned int offset,
	const char *format, ...) ATTRIBUTE_FORMAT(printf, 4, 5);


/** A version of snprintf that automatically resizes the output buffer
 *  when needed. *out and *size must either both be zero or *out has to point
 *  to *size bytes of preallocated memory.
 *  @param[in,out] out   Points to a dynamically allocated buffer where
 *  	the formatted string should be stored.
 *  @param[in,out] size  The size of the allocated memory buffer.
 *  @param[in] offset    String offset to the buffer.
 *  @param[in] format    A string that specifies how arguments are converted.
 *  @return On success: the number of characters written, excluding \\0.
 *  	The function returns -1 on failure.
 */
static int
snprintfExtend (char **out, size_t *size, unsigned int offset,
	const char *format, ...)
{
	va_list ap;
	int ret;

	assert (out != NULL);
	assert (size != NULL);
	assert (format != NULL);

	va_start (ap, format);
	ret = vsnprintf (NULL, 0, format, ap);
	va_end (ap);

	if (ret == -1)
		return -1;
	if ((unsigned) ++ret > *size - offset)
	{
		*out = xrealloc (*out, *size + ret);
		*size += ret;
	}

	va_start (ap, format);
	ret = vsnprintf (*out + offset, *size - offset, format, ap);
	va_end (ap);
	return ret;
}

char *
sidToString (const void *sid, size_t length)
{
	const Sid *hdr;
	const uint32_t *subAuths;
	int offset, written, i;
	char *buff = NULL;
	size_t buffAlloc = 0;

	assert (sid != NULL);

	if (length < sizeof (Sid))
		return NULL;

	hdr = sid;
	subAuths = (uint32_t *) ((char *) sid + sizeof (Sid));

	if (length < sizeof (Sid) + hdr->subAuthorityCnt * sizeof (uint32_t))
		return NULL;

	if ((offset = snprintfExtend (&buff, &buffAlloc, 0,
		"S-%u-%llu", hdr->revision,
		IDENTIFIER_AUTHORITY_TO_LLINT (hdr->identifierAuthority))) == -1)
	{
		free (buff);
		return NULL;
	}

	for (i = 0; i < hdr->subAuthorityCnt; i++)
	{
		if ((written = snprintfExtend (&buff, &buffAlloc, offset,
			"-%lu", (long unsigned) subAuths[i])) == -1)
		{
			free (buff);
			return NULL;
		}
		offset += written;
	}
	return buff;
}

/* This function accepts a little bit wider range of inputs than those
 * which are really valid (thanks to those strtol-class functions).
 * This might be restricted, however it's not that important.
 */
void *
sidToBinary (const char *__restrict sid, size_t *__restrict length)
{
	Buffer buf = BUFFER_INITIALIZER;
	Sid head;
	char *cur;
	long num;
	long long auth;

	assert (sid != NULL);
	assert (length != NULL);

	if (sid[0] != 'S' || sid[1] != '-')
		return NULL;

	num = strtol (sid + 2, &cur, 10);
	if (*cur != '-' || num < 0 || num > UINT8_MAX)
		return NULL;
	head.revision = (uint8_t) num;

	auth = strtoll (cur + 1, &cur, 10);
	if (*cur && *cur != '-')
		return NULL;

	/* Put that value into the array in big-endian format. */
	head.identifierAuthority[0] = (auth >> 40) & 0xFF;
	head.identifierAuthority[1] = (auth >> 32) & 0xFF;
	head.identifierAuthority[2] = (auth >> 24) & 0xFF;
	head.identifierAuthority[3] = (auth >> 16) & 0xFF;
	head.identifierAuthority[4] = (auth >> 8)  & 0xFF;
	head.identifierAuthority[5] = (auth)       & 0xFF;

	head.subAuthorityCnt = 0;
	bufferAppend (&buf, &head, sizeof head, 0);

	while (*cur)
	{
		uint32_t sa;

		sa = strtol (cur + 1, &cur, 10);
		if (*cur && *cur != '-')
		{
			bufferDestroy (&buf);
			return NULL;
		}
		((Sid *) buf.data)->subAuthorityCnt++;
		bufferAppend (&buf, &sa, sizeof(uint32_t), 0);
	}
	*length = buf.used;
	return buf.data;
}

