/**
 *  @file csv2evt.c
 *  @brief CSV to .evt convertor
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

/* strptime, ftruncate */
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "configure.h"

#include "xalloc.h"
#include "csv.h"
#include "evt.h"
#include "base64.h"
#include "sid.h"
#include "widechar.h"
#include "datastruct.h"


/** Reindex log records. */
#define CSV2EVT_REINDEX 1

/** The first field in a record was empty. */
#define RECORD_EMPTY_FIRST_FIELD 1

/** An error happened, don't output the record. */
#define RECORD_IGNORE 2


/** A conversion context to be passed to various functions. */
typedef struct
{
	/** EVT file header. */
	EvtHeader *hdr;
	/** EVT file EOF record. */
	EvtEOF *eof;
	/** The current record being processed. */
	EvtRecord *rec;
	/** Non-fixed-length data related to the record. */
	Buffer *nonFixed;

	/** The output file. */
	FILE *output;
	/** Options related to log processing. See CSV2EVT_*. */
	int options;
	/** Current line number. */
	long lineNo;
	/** Whether we haven't read the first record yet. */
	int firstRecRead;
	/** The cached value of the length of the first record.
	 *  Special value of zero: no first record = no record at all.
	 */
	int firstRecLength;
	/** How many bytes of free space is at the end of our output file. */
	long tailSpace;

	/** A token from the CSV file. */
	char *token;
	/** Flags for the currently processed record.
	 *  See the RECORD_* defines.
	 */
	int recFlags;
	/** The current field we're parsing. The order in which
	 *  the fields are parsed may be changed by swapping
	 *  the order in which the enumeration items appear. */
	enum
	{
		FIELD_RECORD_NO,
		FIELD_TIME_GEN,
		FIELD_TIME_WRI,
		FIELD_EVENT_ID,
		FIELD_EVENT_TYPE,
		FIELD_EVENT_CAT,
		FIELD_SOURCE_NAME,
		FIELD_COMPUTER_NAME,
		FIELD_SID,
		FIELD_STRINGS,
		FIELD_DATA,
		/* We're at the end. */
		FIELD_END,
		/* Ignore the fields. */
		FIELD_IGNORE
	}
	field;
}
ConvCtx;


/** Process the input file and output the result into the output file. */
static void processFile (FILE *__restrict input, FILE *__restrict output,
	int options);

/** Tell the position in the file specified by @a stream and call exit()
 *  if the operation fails.
 */
static long xftell (FILE *stream);

/** Set the position in the file specified by @a stream and call exit()
 *  if the operation fails.
 */
static int xfseek (FILE *fp, long offset, int whence);

/** Read the filesize record.
 *  @param[out] hdr  An EvtHeader structure of which maxSize
 *                   will be set on success.
 *  @param[in,out] rdr  A CSV reader object.
 *  @return -1 on error, 0 on success.
 */
static int readFilesizeRecord
	(EvtHeader *__restrict hdr, CsvReader __restrict rdr);

/** Process a field from the input file. */
static void processField (ConvCtx *ctx);

/** Parse a time token.
 *  @param[in] token  The string to be parsed.
 *  @return -1 on error, otherwise a time_t value that corresponds
 *          to the time the token represents.
 */
static time_t parseTime (const char *token);

/** Write a record to the output file.
 *  @param[in,out] ctx  A conversion context.
 */
static void writeRecord (ConvCtx *ctx);

/** Writes a block of data into a log file.
 *  @param[in] ctx  A conversion context.
 *  @param[in] fp  The log file.
 *  @param[in] data  The data.
 *  @param[in] length  The length of @a data in bytes.
 *  @param[in] maySplit  Whether the block may be split.
 */
static int writeBlock (ConvCtx *ctx, const void *data, size_t length,
	int maySplit);

/** Get more space in the log file by deleting records from the beginning.
 *  This function might reposition @a fp and eat your hamster.
 *  @param[in,out] ctx  A conversion context to work with.
 *  @param[in] newRecordOffset  The offset of the record for which we are
 *                              trying to get some space.
 *  @return 0 if it was successful, -1 if it was not.
 */
static int getMoreSpace (ConvCtx *ctx, long newRecordOffset);

/** Reset a record.
 *  @param[out] ctx  A conversion context.
 */
static void resetRecord (ConvCtx *ctx);


int main (int argc, char *argv[])
{
	FILE *output, *input;
	int options;

#ifdef HAVE_GETTEXT
    /* All we want is localized messages. */
    setlocale(LC_MESSAGES, "");

    bindtextdomain(GETTEXT_DOMAIN, GETTEXT_DIRNAME);
    textdomain(GETTEXT_DOMAIN);
#endif

#ifndef HAVE__MKGMTIME
	setutctimezone();
#endif

	/* TODO: An option (-i) to reindex entries. */
	options = 0;

	if (argc == 1 || argc > 3)
	{
		fputs(_("Usage: csv2evt input-file [output-file]\n"), stderr);
		exit(EXIT_FAILURE);
	}

	if (!strcmp(argv[1], "-"))
		input = stdin;
	else if (!(input = fopen(argv[1], "rb")))
	{
		fprintf(stderr, _("Failed to open %s for reading.\n"), argv[1]);
		exit(EXIT_FAILURE);
	}

	output = stdout;
	if (argc == 3 && *argv[2] && strcmp(argv[2], "-")
		&& !(output = fopen(argv[2], "w+b")))
	{
		fprintf(stderr, _("Failed to open %s for writing.\n"), argv[2]);
		exit(EXIT_FAILURE);
	}

	processFile(input, output, options);

	fclose(input);
	fclose(output);

	return 0;
}

static void processFile (FILE *__restrict input, FILE *__restrict output,
	int options)
{
	CsvReader rdr;
	EvtHeader hdr;
	EvtEOF eof;
	EvtRecord rec;
	Buffer nonFixed = BUFFER_INITIALIZER;
	ConvCtx ctx;
	int inputEOF = 0;

	hdr.headerSize = 0x30;
	hdr.endHeaderSize = 0x30;
	hdr.signature = EVT_SIGNATURE;
	hdr.majorVersion = 1;
	hdr.minorVersion = 1;
	hdr.startOffset = sizeof(hdr);
	hdr.endOffset = sizeof(hdr);
	hdr.oldestRecordNumber = 0;
	hdr.currentRecordNumber = 1;
	hdr.flags = 0;
	hdr.retention = 0;

	eof.recordSizeBeginning = 0x28;
	eof.recordSizeEnd = 0x28;
	eof.one = 0x11111111;
	eof.two = 0x22222222;
	eof.three = 0x33333333;
	eof.four = 0x44444444;
	eof.beginRecord = sizeof(hdr);
	eof.endRecord = sizeof(hdr);
	eof.oldestRecordNumber = 0;
	eof.currentRecordNumber = 1;

	/* Create a CSV reader object. */
	rdr = csvCreateReader(input);

	/* Read the output file size and set it on the output file. */
	if (readFilesizeRecord(&hdr, rdr))
		exit(EXIT_FAILURE);
	if (ftruncate(fileno(output), hdr.maxSize) == -1)
	{
		fputs(_("Error: Failed to set the size of the output file."), stderr);
		exit(EXIT_FAILURE);
	}

	/* We'll write the header in later. */
	xfseek(output, sizeof(hdr), SEEK_CUR);

	ctx.hdr = &hdr;
	ctx.eof = &eof;
	ctx.rec = &rec;
	ctx.nonFixed = &nonFixed;

	ctx.output = output;
	ctx.options = options;
	ctx.lineNo = 2;
	ctx.firstRecRead = 0;
	ctx.firstRecLength = 0;
	ctx.field = 0;
	ctx.recFlags = 0;
	ctx.tailSpace = hdr.maxSize - sizeof(EvtHeader);

	resetRecord(&ctx);

	while (!inputEOF)
	{
		switch (csvRead(rdr, &ctx.token))
		{
			const char *p;

		case CSV_FIELD:
			if (ctx.field != FIELD_IGNORE)
				processField(&ctx);

			for (p = ctx.token; *p; p++)
			{
				if (p[0] == '\r' || p[0] == '\n')
					ctx.lineNo++;
				if (p[0] == '\r' && p[1] == '\n')
					p++;
			}

			free(ctx.token);
			break;
		case CSV_EOR:
			if (~ctx.recFlags & RECORD_IGNORE)
			{
				if (ctx.field < FIELD_END)
					fprintf(stderr, _("Error at line %ld: Incomplete record."
						" I'm skipping it.\n"), ctx.lineNo);
				else
					writeRecord(&ctx);
			}

			ctx.field = 0;
			ctx.recFlags = 0;
			resetRecord(&ctx);

			ctx.lineNo++;
			break;
		case CSV_EOF:
			/* TODO: Probably move to writeBlock. */
			ctx.hdr->endOffset = ctx.eof->endRecord = xftell(output);

			/* Write the EOF record and the header. */
			writeBlock(&ctx, &eof, sizeof(EvtEOF), 0);
			xfseek(output, 0, SEEK_SET);
			fwrite(&hdr, sizeof(hdr), 1, output);

			inputEOF = 1;
			break;
		case CSV_ERROR:
			fputs(_("Error: Error reading the input file."), stderr);
			exit(EXIT_FAILURE);
		}
	}
	bufferDestroy(&nonFixed);
	csvDestroyReader(rdr);
}

static int readFilesizeRecord
	(EvtHeader *__restrict hdr, CsvReader __restrict rdr)
{
	char *p, *csvToken;

	if (csvRead(rdr, &csvToken) != CSV_FIELD)
	{
		fputs(_("Error: Failed to read the filesize record."), stderr);
		free(csvToken);
		return -1;
	}
	hdr->maxSize = strtol(csvToken, &p, 10);
	if (*p)
	{
		fputs(_("Error: Failed to parse the filesize record."), stderr);
		free(csvToken);
		return -1;
	}
	free(csvToken);

	/* Skip other fields. The field this fails on is CSV_EOR. */
	while (csvRead(rdr, NULL) == CSV_FIELD)
		continue;

	return 0;
}

#define ERROR_SKIP_RECORD(msg) \
	{ \
		fprintf(stderr, _("Error at line %ld: %s. I'm skipping it.\n"), \
			ctx->lineNo, msg); \
		ctx->field = FIELD_IGNORE; \
		ctx->recFlags |= RECORD_IGNORE; \
		return; \
	}
#define ERROR_WARNING(msg) \
	fprintf(stderr, _("Warning at line %ld: %s.\n"), ctx->lineNo, msg)

static void processField (ConvCtx *ctx)
{
	switch (ctx->field++)
	{
		/* An anonymous union would be great. */
		char *p;
		time_t myTime;
		long bytes, offset;
		uint16_t *utfStr;
		void *sid, *b64buff;
		size_t length;
		Buffer buf;
		base64_decodestate b64state;

	case FIELD_RECORD_NO:
		/* Empty lines are scanned as a single zero-length field.
		 * Therefore, if we get an empty string here,
		 * we'll wait for the next field before we error out.
		 */
		if (!*ctx->token)
		{
			ctx->recFlags |= RECORD_EMPTY_FIRST_FIELD | RECORD_IGNORE;
			break;
		}

		offset = strtol(ctx->token, &p, 10);

		if (ctx->options & CSV2EVT_REINDEX)
		{
			if (*p)
				ERROR_WARNING(_("Invalid record number"));
			ctx->rec->recordNumber = ctx->hdr->currentRecordNumber;
		}
		else
		{
			if (*p)
				ERROR_SKIP_RECORD(_("Invalid record number"));
			if (offset <= 0)
				ERROR_SKIP_RECORD(_(
					"A record with a non-positive record number"));
			if (ctx->firstRecRead)
			{
				/* TODO: It is allowed to overflow to 0 etc. */
				if (offset > ctx->hdr->currentRecordNumber)
					ERROR_WARNING(_("Discontiguous record"));
				else if (offset < ctx->hdr->currentRecordNumber)
					ERROR_SKIP_RECORD(_("A record with a record number that is"
						" less than or equal to the previous record"));
			}
			ctx->rec->recordNumber = offset;
		}
		break;
	case FIELD_TIME_GEN:
		if (ctx->recFlags & RECORD_EMPTY_FIRST_FIELD)
			ERROR_SKIP_RECORD(_("A record without a record number. You"
				" can prevent this error with the -i option"));

		if ((myTime = parseTime(ctx->token)) == -1)
			ERROR_SKIP_RECORD(
				_("Failed to parse generation time in a record"));
		ctx->rec->timeGenerated = (uint32_t) myTime;
		break;
	case FIELD_TIME_WRI:
		if ((myTime = parseTime(ctx->token)) == -1)
			ERROR_SKIP_RECORD(
				_("Failed to parse written time in a record"));
		ctx->rec->timeWritten = (uint32_t) myTime;
		break;
	case FIELD_EVENT_ID:
		ctx->rec->eventID = strtol(ctx->token, &p, 10);
		if (*p)
			ERROR_SKIP_RECORD(_("Failed to parse event ID"));
		break;
	case FIELD_EVENT_TYPE:
		if (!strcmp(ctx->token, "Information"))
			ctx->rec->eventType = EVT_INFORMATION_TYPE;
		else if (!strcmp(ctx->token, "Warning"))
			ctx->rec->eventType = EVT_WARNING_TYPE;
		else if (!strcmp(ctx->token, "Error"))
			ctx->rec->eventType = EVT_ERROR_TYPE;
		else if (!strcmp(ctx->token, "Audit Success"))
			ctx->rec->eventType = EVT_AUDIT_SUCCESS;
		else if (!strcmp(ctx->token, "Audit Failure"))
			ctx->rec->eventType = EVT_AUDIT_FAILURE;
		else
		{
			ctx->rec->eventType = strtol(ctx->token, &p, 10);
			if (*p)
				ERROR_SKIP_RECORD(_("Failed to parse event type in a record"));
		}
		break;
	case FIELD_EVENT_CAT:
		ctx->rec->eventCategory = strtol(ctx->token, &p, 10);
		if (*p)
			ERROR_SKIP_RECORD(_("Failed to parse event category"));
		break;
	case FIELD_SOURCE_NAME:
		if (!(bytes = encodeMBString(ctx->token, &utfStr)))
			ERROR_SKIP_RECORD(_("Failed to decode the event source name"));
		bufferAppend(ctx->nonFixed, utfStr, bytes, 0);
		free(utfStr);
		break;
	case FIELD_COMPUTER_NAME:
		if (!(bytes = encodeMBString(ctx->token, &utfStr)))
			ERROR_SKIP_RECORD(_("Failed to decode the computer name"));
		bufferAppend(ctx->nonFixed, utfStr, bytes, 0);
		free(utfStr);
		break;
	case FIELD_SID:
		if (!*ctx->token)
		{
			ctx->rec->userSidLength = 0;
			ctx->rec->userSidOffset = 0;
			break;
		}
		if (!(sid = sidToBinary(ctx->token, &length)))
			ERROR_SKIP_RECORD(_("Failed to decode SID"));

		/* The SID should be aligned on a DWORD (4-byte) boundary. */
		ctx->rec->userSidOffset = sizeof(EvtRecord)
			+ bufferAppend(ctx->nonFixed, sid, length, 4);
		ctx->rec->userSidLength = length;
		free(sid);
		break;
	case FIELD_STRINGS:
		bufferInit(&buf);
		for (p = ctx->token; ; p++)
		{
			int quoted = 0;

			if (*p == '\\')
			{
				quoted = 1;
				p++;
			}

			/* End of token OR unquoted end of string mark. */
			if (!*p || (p[0] == '|' && !quoted))
			{
				bufferAppendChar(&buf, '\0');

				if (!(bytes = encodeMBString(buf.data, &utfStr)))
				{
					bufferDestroy(&buf);
					ERROR_SKIP_RECORD(_("Failed to decode strings"));
				}
				offset = bufferAppend(ctx->nonFixed, utfStr, bytes, 0);
				free(utfStr);

				if (!ctx->rec->numStrings)
					ctx->rec->stringOffset = sizeof(EvtRecord) + offset;
				ctx->rec->numStrings++;

				/* End of string -> reset our current string. */
				if (*p)
					bufferEmpty(&buf);
				/* End of token  -> stop processing. */
				else
					break;
			}
			else
				bufferAppendChar(&buf, *p);
		}
		bufferDestroy(&buf);
		break;
	case FIELD_DATA:
		base64_init_decodestate(&b64state);
		b64buff = xmalloc(BASE64_DECODED_BUFFER_SIZE(strlen(ctx->token)));
		ctx->rec->dataLength = base64_decode_block(ctx->token,
			strlen(ctx->token), b64buff, &b64state);
		ctx->rec->dataOffset = sizeof(EvtRecord) + bufferAppend(ctx->nonFixed,
			b64buff, ctx->rec->dataLength, 0);
		free(b64buff);
		break;
	case FIELD_END:
		ERROR_WARNING(_("Extraneous field(s) in a record"));
		break;
	default:
		break;
	}
}

static time_t parseTime (const char *token)
{
	struct tm tm;

	/* Initialize the structure. */
	memset(&tm, 0, sizeof(struct tm));

#ifdef HAVE_STRPTIME
	strptime(token, "%Y-%m-%d %H:%M:%S", &tm);
#else /* ! HAVE_STRPTIME */
	sscanf(token, "%4d-%2d-%2d %2d:%2d:%2d",
		&tm.tm_year, &tm.tm_mon, &tm.tm_mday,
		&tm.tm_hour, &tm.tm_min, &tm.tm_sec);
#endif /* ! HAVE_STRPTIME */

#ifdef HAVE__MKGMTIME
	return _mkgmtime(&tm);
#else /* ! HAVE__MKGMTIME */
	return mktime(&tm);
#endif /* ! HAVE__MKGMTIME */
}

static void writeRecord (ConvCtx *ctx)
{
	long offset;

	/* The record has to be aligned on a DWORD (4-byte) boundary. */
	offset = bufferAppend(ctx->nonFixed, NULL, sizeof(ctx->rec->length), 4);
	*(uint32_t *) ((char *) ctx->nonFixed->data + offset) = ctx->rec->length
		= sizeof(EvtRecord) + ctx->nonFixed->used;

	offset = xftell(ctx->output);

	/* Write the record. */
	if (writeBlock(ctx, ctx->rec, sizeof(EvtRecord), 0)
		|| writeBlock(ctx, ctx->nonFixed->data, ctx->nonFixed->used, 1))
		exit(EXIT_FAILURE);

	if (!ctx->firstRecRead)
	{
		ctx->firstRecRead = 1;
		ctx->firstRecLength = ctx->rec->length;
		ctx->hdr->oldestRecordNumber = ctx->rec->recordNumber;
		ctx->eof->oldestRecordNumber = ctx->rec->recordNumber;
		ctx->hdr->startOffset = offset;
		ctx->eof->beginRecord = offset;
	}
	ctx->hdr->currentRecordNumber = ctx->rec->recordNumber + 1;
	ctx->eof->currentRecordNumber = ctx->rec->recordNumber + 1;
}

static int writeBlock (ConvCtx *ctx, const void *data, size_t length,
	int maySplit)
{
	/* Unused bytes at the end: 0x00000027 in LE. */
	static const char unused[4] = {0x27, 0x00, 0x00, 0x00};
	long offset, endSpace, reqLength, i;
	enum
	{
		/* No wrapping needs to be handled. */
		CSV2EVT_WRITEBLOCK_NO_WRAP,
		/* Split the block. */
		CSV2EVT_WRITEBLOCK_SPLIT,
		/* Fill the unused space and write the block after the header. */
		CSV2EVT_WRITEBLOCK_START
	}
	action;

	offset = xftell(ctx->output);
	endSpace = ctx->hdr->maxSize - offset;
	if (endSpace < 0)
	{
		fputs(_("Error: We've got past the end of log file."), stderr);
		return -1;
	}

	reqLength = length;
	if ((unsigned) endSpace < length)
	{
		ctx->hdr->flags |= EVT_HEADER_WRAP;

		if (maySplit)
			action = CSV2EVT_WRITEBLOCK_SPLIT;
		else
		{
			reqLength += endSpace;
			action = CSV2EVT_WRITEBLOCK_START;
		}
	}
	else
		action = CSV2EVT_WRITEBLOCK_NO_WRAP;

	while (ctx->tailSpace < reqLength)
	{
		if (getMoreSpace(ctx, offset))
		{
			fputs(_("Error: Failed to write a record; not enough space."),
				stderr);
			return -1;
		}
	}
	xfseek(ctx->output, offset, SEEK_SET);

	/* We've got enough space, we may write the block. */
	switch (action)
	{
	case CSV2EVT_WRITEBLOCK_SPLIT:
		fwrite(data, endSpace, 1, ctx->output);
		xfseek(ctx->output, sizeof(EvtHeader), SEEK_SET);
		fwrite((char *) data + endSpace, length - endSpace, 1, ctx->output);
		break;

	case CSV2EVT_WRITEBLOCK_START:
		for (i = 0; i < endSpace; i++)
			fputc(unused[i & 3], ctx->output);

		xfseek(ctx->output, sizeof(EvtHeader), SEEK_SET);

	case CSV2EVT_WRITEBLOCK_NO_WRAP:
		fwrite(data, length, 1, ctx->output);
	}

	ctx->tailSpace -= reqLength;
	return 0;
}

/* If we have wrapped, we'll likely be overwritting our previous
 * records. In that case, we have to advance startOffset and update
 * oldestRecordNumber with the recordNumber of this next item.
 */
static int getMoreSpace (ConvCtx *ctx, long newRecordOffset)
{
	EvtRecord hdr;
	long endSpace;

	/* Not possible. */
	if (!ctx->hdr->oldestRecordNumber)
		return -1;

	ctx->tailSpace += ctx->firstRecLength;

	/* We have to overwrite our only record. */
	if (ctx->hdr->oldestRecordNumber == ctx->hdr->currentRecordNumber)
	{
		ctx->firstRecRead = 0;
		ctx->firstRecLength = 0;
		ctx->hdr->oldestRecordNumber = 0;
		ctx->eof->oldestRecordNumber = 0;

		/* If it is the EOF record that has to overwrite this record,
		 * this sets the startOffset and beginRecord values to be correct.
		 */
		ctx->hdr->startOffset = ctx->eof->beginRecord = newRecordOffset;
		return 0;
	}

	/* How much space remains after the current first record. */
	endSpace = ctx->hdr->maxSize - ctx->hdr->startOffset - ctx->firstRecLength;

	/* The current first record is wrapped. */
	if (endSpace < 0)
		ctx->hdr->startOffset = sizeof(EvtHeader) - endSpace;
	/* No space for another record behind the current first record. */
	else if (endSpace < (signed) sizeof(EvtRecord))
	{
		/* We must go to the start of the file. */
		ctx->tailSpace += endSpace;
		ctx->hdr->startOffset = sizeof(EvtHeader);
	}
	/* We may simply advance the start offset. */
	else
		ctx->hdr->startOffset += ctx->firstRecLength;

	ctx->eof->beginRecord = ctx->hdr->startOffset;

	/* Read the header of the next record. */
	xfseek(ctx->output, ctx->hdr->startOffset, SEEK_SET);
	if (!fread(&hdr, sizeof(EvtRecord), 1, ctx->output))
		return -1;

	ctx->firstRecLength = hdr.length;
	ctx->hdr->oldestRecordNumber = hdr.recordNumber;
	ctx->eof->oldestRecordNumber = hdr.recordNumber;
	return 0;
}

/* NOTE: For xftell and xfseek, it is also possible to longjmp somewhere. */
static long xftell (FILE *fp)
{
	long offset;

	if ((offset = ftell(fp)) == -1)
	{
		fputs(_("Error: Failed to get position in the output file."), stderr);
		exit(EXIT_FAILURE);
	}
	return offset;
}

static int xfseek (FILE *fp, long offset, int whence)
{
	if (fseek(fp, offset, whence))
	{
		fputs(_("Error: Failed to set position in the output file."), stderr);
		exit(EXIT_FAILURE);
	}
	return 0;
}

static void resetRecord (ConvCtx *ctx)
{
	bufferEmpty(ctx->nonFixed);
	memset(ctx->rec, 0, sizeof(EvtRecord));
	ctx->rec->reserved = EVT_SIGNATURE;
}

