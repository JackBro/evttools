/**
 *  @file evt2csv.c
 *  @brief .evt to CSV convertor
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "configure.h"

#include "xalloc.h"
#include "evt.h"
#include "csv.h"
#include "widechar.h"
#include "base64.h"
#include "sid.h"
#include "datastruct.h"


/** Process an .evt file. */
static int processFile (FILE *__restrict input, FILE *__restrict output);
/** Handle read() failure in proccessFile(). */
static inline int handleReadRecordFailure
	(FILE *__restrict input, EvtHeader *__restrict hdr, int wraps);

/** Process a record. */
static void processRecord (EvtRecord *__restrict rec,
	const void *__restrict nonFixed, size_t nonFixedLength,
	CsvWriter __restrict wrt);
/** Write a CSV field in base64. */
static int writeFieldBase64 (CsvWriter __restrict wrt,
	const void *__restrict field, size_t length);


int main (int argc, char *argv[])
{
	FILE *output, *input;

#ifdef HAVE_GETTEXT
    /* All we want is localized messages. */
    setlocale(LC_MESSAGES, "");

    bindtextdomain(GETTEXT_DOMAIN, "/usr/locale/share");
    textdomain(GETTEXT_DOMAIN);
#endif

	if (argc == 1 || argc > 3)
	{
		fputs(_("Usage: evt2csv input-file [output-file]\n"), stderr);
		exit(EXIT_FAILURE);
	}

	if (!(input = fopen(argv[1], "rb")))
	{
		fprintf(stderr, _("Error: Failed to open %s for reading.\n"), argv[1]);
		exit(EXIT_FAILURE);
	}

	output = stdout;
	if (argc == 3 && *argv[2] && strcmp(argv[2], "-")
		&& !(output = fopen(argv[2], "wb")))
	{
		fprintf(stderr, _("Error: Failed to open %s for writing.\n"), argv[2]);
		exit(EXIT_FAILURE);
	}

	if (processFile(input, output))
		exit(EXIT_FAILURE);

	fclose(input);
	fclose(output);

	return 0;
}

static int processFile (FILE *__restrict input, FILE *__restrict output)
{
	EvtHeader hdr;
	union
	{
		EvtEOF eof;
		EvtRecord fixed;
	}
	rec;
	CsvWriter wrt;
	long fileSize;
	int wraps, ret = -1;

	/* FIXME: Shuffle the bits on big endian machines. */

	if (!fread(&hdr, sizeof(hdr), 1, input))
	{
		fputs(_("Error: Failed to read ELF header.\n"), stderr);
		return -1;
	}
	if (hdr.signature != EVT_SIGNATURE)
	{
		fputs(_("Error: ELF signature doesn't match.\n"), stderr);
		return -1;
	}
	if (hdr.flags & EVT_HEADER_DIRTY)
		fputs(_("Warning: The log file is marked dirty.\n"), stderr);
	wraps = hdr.flags & EVT_HEADER_WRAP;

	/* This should cut out the case when stdin is given as the input. */
	if (fseek(input, hdr.startOffset, SEEK_SET))
	{
		fprintf(stderr, _("Error: fseek: %s.\n"), strerror(errno));
		return -1;
	}
	if ((fileSize = filelength(fileno(input))) == -1)
	{
		fputs(_("Error: Failed to get file size.\n"), stderr);
		return -1;
	}

	/* Write out a special header record with file size.
	 * (The only non-record value that is really useful
	 * for reconstructing the .evt.)
	 */
	fprintf(output, "%lu\n", fileSize);

	wrt = csvCreateWriter(output);
	while (1)
	{
		void *nonFixed;
		uint32_t nonFixedLength;

		if (!fread(&rec, sizeof(rec.eof), 1, input))
		{
			if (handleReadRecordFailure(input, &hdr, wraps))
				break;
			else
				continue;
		}

		if (rec.eof.one == 0x11111111 && rec.eof.two == 0x22222222
			&& rec.eof.three == 0x33333333 && rec.eof.four == 0x44444444)
		{
			ret = 0;
			break;
		}

		/* Not an EOF record (which is shorter), so let's read the rest. */
		if (!fread((char *) &rec + sizeof(rec.eof),
				sizeof(rec.fixed) - sizeof(rec.eof), 1, input))
		{
			if (handleReadRecordFailure(input, &hdr, wraps))
				break;
			else
				continue;
		}

		nonFixedLength = rec.fixed.length - sizeof(rec.fixed);
		if (nonFixedLength > (unsigned) fileSize)
		{
			fprintf(stderr, _("Error: Record %u is longer than "
				"the whole file.\n"), rec.fixed.recordNumber);
			break;
		}
		nonFixed = xmalloc(nonFixedLength);

		/* FIXME: These functions are not checked for errors. */
		if (wraps)
		{
			long pos;

			pos = ftell(input);
			if (pos + nonFixedLength > (unsigned) fileSize)
			{
				fread(nonFixed, fileSize - pos, 1, input);
				/* Wrap around the end of file and read the rest. */
				fseek(input, hdr.headerSize, SEEK_SET);
				fread(nonFixed, nonFixedLength - (fileSize - pos), 1, input);
			}
			else
				fread(nonFixed, nonFixedLength, 1, input);
		}
		else
			fread(nonFixed, nonFixedLength, 1, input);

		processRecord(&rec.fixed, nonFixed, nonFixedLength, wrt);
		free(nonFixed);
	}
	csvDestroyWriter(wrt);
	return ret;
}

static inline int handleReadRecordFailure
	(FILE *__restrict input, EvtHeader *__restrict hdr, int wraps)
{
	if (ferror(input))
		fprintf(stderr, _("Error: fread: %s\n"), strerror(errno));
	else if (wraps)
	{
		/* Wrap around the end of file. */
		fseek(input, hdr->headerSize, SEEK_SET);
		return 0;
	}
	else
		fputs(_("Error: Unexpected end of file.\n"), stderr);
	return 1;
}

static void processRecord (EvtRecord *__restrict rec,
	const void *__restrict nonFixed, size_t nonFixedLength,
	CsvWriter __restrict wrt)
{
	time_t timeGenerated, timeWritten;
	/* Yes, the buffer is large enough. */
	char buff[40], *s, *sCur;
	Buffer final = BUFFER_INITIALIZER;
	int offset, len;

	/* First field: record number. */
	snprintf(buff, sizeof(buff), "%d", rec->recordNumber);
	csvWrite(wrt, buff);

	/* Second field: time generated (GMT). */
	timeGenerated = rec->timeGenerated;
	strftime(buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", gmtime(&timeGenerated));
	csvWrite(wrt, buff);

	/* Third field: time written (GMT). */
	timeWritten = rec->timeWritten;
	strftime(buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", gmtime(&timeWritten));
	csvWrite(wrt, buff);

	/* Fourth field: event ID. */
	snprintf(buff, sizeof(buff), "%u", rec->eventID);
	csvWrite(wrt, buff);

	/* Fifth field: event type. */
	switch (rec->eventType)
	{
		case EVT_INFORMATION_TYPE:
			csvWrite(wrt, "Information");
			break;
		case EVT_WARNING_TYPE:
			csvWrite(wrt, "Warning");
			break;
		case EVT_ERROR_TYPE:
			csvWrite(wrt, "Error");
			break;
		case EVT_AUDIT_SUCCESS:
			csvWrite(wrt, "Audit Success");
			break;
		case EVT_AUDIT_FAILURE:
			csvWrite(wrt, "Audit Failure");
			break;
		default:
			/* Unknown type: express with a number. */
			snprintf(buff, sizeof(buff), "%u", rec->eventType);
			csvWrite(wrt, buff);
	}

	/* Sixth field: event category. */
	snprintf(buff, sizeof(buff), "%d", rec->eventCategory);
	csvWrite(wrt, buff);

	/* Seventh field: source name (in UTF-8). */
	if ((offset = decodeWideString((uint16_t *) nonFixed,
		nonFixedLength, &s)))
	{
		csvWrite(wrt, s);
		free(s);
	}
	else
	{
		fprintf(stderr, _("Warning: Failed to decode the source name string "
			"in record %u.\n"), rec->recordNumber);
		csvWrite(wrt, s);
	}

	/* Eighth field: computer name (in UTF-8). */
	if ((decodeWideString((uint16_t *) ((char *) nonFixed + offset),
		nonFixedLength - offset, &s)))
	{
		csvWrite(wrt, s);
		free(s);
	}
	else
	{
		fprintf(stderr, _("Warning: Failed to decode the computer name string "
			"in record %u.\n"), rec->recordNumber);
		csvWrite(wrt, s);
	}

	/* Nineth field: SID. */
	if (rec->userSidOffset + rec->userSidLength > rec->length)
	{
		fprintf(stderr, _("Warning: Record %u has overflowing SID field. "
			"I'm not reading it.\n"), rec->recordNumber);
		csvWrite(wrt, "");
	}
	else if (!rec->userSidLength)
		csvWrite(wrt, "");
	else
	{
		if ((s = sidToString((char *) nonFixed + rec->userSidOffset
			- sizeof(EvtRecord), rec->userSidLength)))
		{
			csvWrite(wrt, s);
			free(s);
		}
		else
		{
			fprintf(stderr, _("Error: SID decoding failed in record %u.\n"),
				rec->recordNumber);
			csvWrite(wrt, "");
		}
	}

	/* Tenth field: strings (in UTF-8). */
	offset = rec->stringOffset - sizeof(EvtRecord);
	while (rec->numStrings--)
	{
		if (!(len = decodeWideString((uint16_t *) ((char *) nonFixed + offset),
			nonFixedLength - offset, &s)))
		{
			fprintf(stderr, _("Error: String decoding failed in record %u.\n"),
				rec->recordNumber);
			break;
		}
		offset += len;

		/* The strings are separated by '|' chars.
		 * This separator may be escaped with '\'.
		 */
		for (sCur = s; *sCur; sCur++)
		{
			if (*sCur == '|' || *sCur == '\\')
				bufferAppendChar(&final, '\\');
			bufferAppendChar(&final, *sCur);
		}
		if (rec->numStrings)
			bufferAppendChar(&final, '|');
		free(s);
	}
	s = NULL;
	bufferAppendChar(&final, '\0');
	csvWrite(wrt, final.data);
	bufferDestroy(&final);

	/* Eleventh field: data (in base64). */
	if (rec->dataOffset + rec->dataLength > rec->length)
	{
		fprintf(stderr, _("Warning: Record %u has overflowing data field. "
			"I'm not reading it.\n"), rec->recordNumber);
		csvWrite(wrt, "");
	}
	else if (writeFieldBase64(wrt, (char *) nonFixed + rec->dataOffset
		- sizeof(EvtRecord), rec->dataLength))
		csvWrite(wrt, "");

	/* End of record. */
	csvWrite(wrt, NULL);
}

static int writeFieldBase64 (CsvWriter __restrict wrt,
	const void *__restrict field, size_t length)
{
	base64_encodestate state;
	char *buff;
	int offset, ret;

	base64_init_encodestate(&state);
	buff = xmalloc(BASE64_ENCODED_BUFFER_SIZE(length));

	offset = base64_encode_block(field, length, buff, &state);
	base64_encode_blockend(buff + offset, &state);
	ret = csvWrite(wrt, buff);
	free(buff);
	return ret;
}

