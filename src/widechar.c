/**
 *  @file widechar.c
 *  @brief Conversion between Windows WCHAR's (UTF-16LE) and UTF-8
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#include <stdlib.h>
#include <assert.h>
#ifdef _WIN32
	#include <windows.h>
#else /* ! _WIN32 */
	#include <iconv.h>
	#include <errno.h>
#endif /* ! _WIN32 */

#include <xtnd/xtnd.h>

#include <config.h>

#ifndef _WIN32
/** A simple wrapper for iconv() that allocates the output buffer
 *  and creates the conversion object itself.
 */
static size_t
iconvWrapper (char *restrict from, char *restrict to,
	char *restrict in, size_t inLen, char **restrict out)
{
	iconv_t obj;
	char *buff;
	size_t outLeft, buffAlloc;

	assert (out != NULL);

	if ((obj = iconv_open (to, from)) == (iconv_t) -1)
		return 0;

	outLeft = buffAlloc = 64;
	*out = buff = xmalloc (buffAlloc);

	while (iconv (obj, (char **) &in, &inLen,
		(char **) &buff, &outLeft) == (size_t) -1)
	{
		if (errno == E2BIG)
		{
			char *newOut;

			outLeft += buffAlloc;
			newOut = xrealloc (*out, buffAlloc <<= 1);
			buff += newOut - *out;
			*out = newOut;
			continue;
		}
		free (*out);
		iconv_close (obj);
		return 0;
	}
	iconv_close (obj);
	return buffAlloc - outLeft;
}
#endif /* ! _WIN32 */

int
decodeWideString (const uint16_t *restrict in, int maxLength,
	char **restrict out)
{
	const uint16_t *i;
	int inLen;
#ifdef _WIN32
	int req;
#endif /* _WIN32 */

	assert (in  != NULL);
	assert (out != NULL);

	if (maxLength <= 0)
		return 0;

	for (i = in; *i++;)
	{
		if (((char *) i - (char *) in) > maxLength)
			return 0;
	}
	inLen = (char *) i - (char *) in;

#ifdef _WIN32
	if (!(req = WideCharToMultiByte (CP_UTF8, 0, in, i - in,
		NULL, 0, NULL, NULL)))
		return 0;
	*out = xmalloc (req * sizeof(char));
	if (!WideCharToMultiByte (CP_UTF8, 0, in, i - in,
		*out, req, NULL, NULL))
	{
		free(*out);
		return 0;
	}
#else /* ! _WIN32 */
	if (!iconvWrapper ("UTF-16LE", "UTF-8", (char *) in, inLen, out))
		return 0;
#endif /* ! _WIN32 */
	return inLen;
}

int
encodeMBString (char *restrict in, uint16_t **restrict out)
{
	const char *i;
	int inLen;
#ifdef _WIN32
	int req;
#endif /* _WIN32 */

	assert (in  != NULL);
	assert (out != NULL);

	for (i = in; *i++; )
		;
	inLen = i - in;

#ifdef _WIN32
	if (!(req = MultiByteToWideChar (CP_UTF8, 0, in, inLen, NULL, 0)))
		return 0;
	*out = xmalloc (req * sizeof (uint16_t));
	if (!MultiByteToWideChar (CP_UTF8, 0, in, inLen, *out, req))
	{
		free (*out);
		return 0;
	}
	return req * sizeof (uint16_t);
#else /* ! _WIN32 */
	return iconvWrapper ("UTF-8", "UTF-16LE", in, inLen, (char **) out);
#endif /* ! _WIN32 */
}

