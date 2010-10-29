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

#include "config.h"

#include <xtnd/xtnd.h>

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
	/** The output file. */
	EvtLog *output;
	/** The current record being processed. */
	EvtRecordContents rec;

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
	ConvCtx ctx;
	CsvReader rdr;
	int inputEOF = 0;
	EvtLog *log;

	rdr = csvCreateReader(input);

	/* Read the output file size and set it on the output file. */
	if (readFilesizeRecord(&hdr, rdr))
		exit(EXIT_FAILURE);
	if (ftruncate(fileno(output), hdr.maxSize) == -1)
	{
		fputs(_("Error: Failed to set the size of the output file."), stderr);
		exit(EXIT_FAILURE);
	}

	log = evtNew(output);

	evtResetHeader(log);
	evtWriteHeader(log, EVT_WRITE_HEADER_MARK);

	ctx.output = log;
	ctx.options = options;
	ctx.lineNo = 2;
	ctx.firstRecRead = 0;
	ctx.firstRecLength = 0;
	ctx.field = 0;
	ctx.recFlags = 0;
	ctx.tailSpace = hdr.maxSize - sizeof(EvtHeader);


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
					evtWriteRecord(log, 0, &ctx.rec);
			}

			ctx.field = 0;
			ctx.recFlags = 0;
			resetRecord(&ctx);

			ctx.lineNo++;
			break;
		case CSV_EOF:
			evtWriteEOF(log);
			evtReposition(log, EVT_REPOSITION_HEADER);
			evtWriteHeader(log, 0);

			inputEOF = 1;
			break;
		case CSV_ERROR:
			fputs(_("Error: Error reading the input file."), stderr);
			exit(EXIT_FAILURE);
		}
	}
	evtDestroy(log);
	csvDestroyReader(rdr);
}

static int readFilesizeRecord
	(uint32_t *fileSize, CsvReader __restrict rdr)
{
	char *p, *csvToken;

	if (csvRead(rdr, &csvToken) != CSV_FIELD)
	{
		fputs(_("Error: Failed to read the filesize record."), stderr);
		free(csvToken);
		return -1;
	}
	*fileSize = strtol(csvToken, &p, 10);
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
		void *b64buff;
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

		/* FIXME: This is probably not able to hold UINT_MAX.
		 * Solution: strtoll
		 */
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
				if ((unsigned) offset > ctx->hdr->currentRecordNumber)
					ERROR_WARNING(_("Discontiguous record"));
				else if ((unsigned) offset < ctx->hdr->currentRecordNumber)
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
		ctx->rec.timeGenerated = myTime;
		break;
	case FIELD_TIME_WRI:
		if ((myTime = parseTime(ctx->token)) == -1)
			ERROR_SKIP_RECORD(
				_("Failed to parse written time in a record"));
		ctx->rec.timeWritten = myTime;
		break;
	case FIELD_EVENT_ID:
		ctx->rec.eventID = strtol(ctx->token, &p, 10);
		if (*p)
			ERROR_SKIP_RECORD(_("Failed to parse event ID"));
		break;
	case FIELD_EVENT_TYPE:
		if (!strcmp(ctx->token, "Information"))
			ctx->rec.eventType = EVT_INFORMATION_TYPE;
		else if (!strcmp(ctx->token, "Warning"))
			ctx->rec.eventType = EVT_WARNING_TYPE;
		else if (!strcmp(ctx->token, "Error"))
			ctx->rec.eventType = EVT_ERROR_TYPE;
		else if (!strcmp(ctx->token, "Audit Success"))
			ctx->rec.eventType = EVT_AUDIT_SUCCESS;
		else if (!strcmp(ctx->token, "Audit Failure"))
			ctx->rec.eventType = EVT_AUDIT_FAILURE;
		else
		{
			/* TODO: Handle possible overflow. */
			ctx->rec.eventType = strtol(ctx->token, &p, 10);
			if (*p)
				ERROR_SKIP_RECORD(_("Failed to parse event type in a record"));
		}
		break;
	case FIELD_EVENT_CAT:
		/* TODO: Handle possible overflow. */
		ctx->rec.eventCategory = strtol(ctx->token, &p, 10);
		if (*p)
			ERROR_SKIP_RECORD(_("Failed to parse event category"));
		break;
	case FIELD_SOURCE_NAME:
		ctx->rec.sourceName = strdup(ctx->token);
		break;
	case FIELD_COMPUTER_NAME:
		ctx->rec.computerName = strdup(ctx->token);
		break;
	case FIELD_SID:
		if (!*ctx->token)
			ctx->rec.userSid = NULL;
		else
			ctx->rec.userSid = strdup(ctx->token);
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
		ctx->rec.data = b64buff
			= xmalloc(BASE64_DECODED_BUFFER_SIZE(strlen(ctx->token)));
		ctx->rec->dataLength = base64_decode_block(ctx->token,
			strlen(ctx->token), b64buff, &b64state);
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
