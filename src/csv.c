/**
 *  @file csv.c
 *  @brief Routines for CSV files.
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "configure.h"

#include "xalloc.h"
#include "csv.h"

struct CsvReader
{
	FILE *fp;
	enum {STA_NORMAL, STA_INQUOTES, STA_EOR, STA_EOR_EOF, STA_EOF} state;
};

CsvReader csvCreateReader (FILE *stream)
{
	CsvReader rdr;

	rdr = xmalloc(sizeof(struct CsvReader));
	rdr->fp = stream;
	rdr->state = STA_NORMAL;

	return rdr;
}

CsvReadStatus csvRead (CsvReader __restrict rdr, char **__restrict field)
{
	char *buff = NULL, *p = NULL;
	size_t buffAlloc = 32;
	int c;

/* At least a bit more readable than re2c output. */
csvRead_begin:
	switch (rdr->state)
	{
	case STA_NORMAL:
		switch ((c = fgetc(rdr->fp)))
		{
		case ',':
			goto csvRead_send;
		case '\r':
			if ((c = fgetc(rdr->fp)) != '\n')
				ungetc(c, rdr->fp);
		case '\n':
			/* We're at the end of a record. */
			rdr->state = STA_EOR;
			goto csvRead_send;
		case '"':
			/* We got inside a quoted string, which may span lines
			 * and contain quotation marks or commas.
			 */
			rdr->state = STA_INQUOTES;
			goto csvRead_begin;
		case EOF:
			/* We're at the end of a file, so let's
			 * first finish the last record.
			 */
			rdr->state = STA_EOR_EOF;
			goto csvRead_send;
		default:
			goto csvRead_push;
		}
	case STA_INQUOTES:
		switch ((c = fgetc(rdr->fp)))
		{
		case EOF:
			rdr->state = STA_EOR_EOF;
			goto csvRead_send;
		case '"':
			if ((c = fgetc(rdr->fp)) != '"')
			{
				ungetc(c, rdr->fp);
				rdr->state = STA_NORMAL;
				goto csvRead_begin;
			}
		default:
			goto csvRead_push;
		}
	case STA_EOR:
		rdr->state = STA_NORMAL;
		return CSV_EOR;
	case STA_EOR_EOF:
		rdr->state = STA_EOF;
		return CSV_EOR;
	/* The final state. Nothing to read. */
	case STA_EOF:
		return ferror(rdr->fp) ? CSV_ERROR : CSV_EOF;
	}

/* Send a token to the caller. */
csvRead_send:
	if (!buff)
		p = buff = xmalloc(1);
	*p++ = '\0';
	*field = buff;
	return CSV_FIELD;

/* Add a character to the token. */
csvRead_push:
	if (!buff)
		p = buff = xmalloc(buffAlloc);
	else if (p + 1 > buff + buffAlloc)
	{
		char *newBuff;

		newBuff = xrealloc(buff, buffAlloc <<= 1);
		p += newBuff - buff;
		buff = newBuff;
	}
	*p++ = c;
	goto csvRead_begin;
}

void csvDestroyReader (CsvReader rdr)
{
	free(rdr);
}


struct CsvWriter
{
	FILE *fp;
	int notFirstField;
};

CsvWriter csvCreateWriter (FILE *stream)
{
	CsvWriter wrt;

	wrt = xmalloc(sizeof(struct CsvWriter));
	wrt->fp = stream;
	wrt->notFirstField = 0;

	return wrt;
}

#define mustBeQuoted(c) ((c) == '\n' || (c) == '\r' || (c) == '"' || (c) == ',')

int csvWrite (CsvWriter __restrict wrt, const char *__restrict field)
{
	const char *p;

	/* End of record. */
	if (!field)
	{
		fputc('\n', wrt->fp);
		wrt->notFirstField = 0;
		return 0;
	}
	if (wrt->notFirstField)
		fputc(',', wrt->fp);
	wrt->notFirstField = 1;

	for (p = field; *p; p++)
		if (mustBeQuoted(*p))
			break;

	/* Do we have to quote it || is it a null-length field? */
	if (*p || !*field)
	{
		if (fputc('"', wrt->fp) == EOF)
			return 1;
		while (*field)
		{
			if (*field == '"' && fputc('"', wrt->fp) == EOF)
				return 1;
			if (fputc(*field++, wrt->fp) == EOF)
				return 1;
		};
		if (fputc('"', wrt->fp) == EOF)
			return 1;
	}
	else if (!fwrite(field, p - field, 1, wrt->fp))
		return 1;
	return 0;
}

void csvDestroyWriter (CsvWriter wrt)
{
	free(wrt);
}
