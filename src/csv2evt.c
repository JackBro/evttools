/**
 *  @file csv2evt.c
 *  @brief CSV to .evt convertor
 *
 *  Copyright Přemysl Janouch 2010, 2012. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

/* strptime, ftruncate */
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <xtnd/xtnd.h>

#include <config.h>
#include "fileio.h"
#include "evt.h"
#include "base64.h"
#include "sid.h"
#include "widechar.h"
#include "datastruct.h"


#define CSV2EVT_RENUMBER     (1 << 0)  /**< Renumber log records. */
#define CSV2EVT_APPEND       (1 << 1)  /**< Append to the log. */
#define CSV2EVT_NOOVERWRITE  (1 << 2)  /**< Prevent log overwriting. */

/** The first field in a record was empty. */
#define RECORD_EMPTY_FIRST_FIELD 1

/** An error happened, don't output the record. */
#define RECORD_IGNORE 2


/** A conversion context to be passed to various functions. */
typedef struct
{
	/** Output log file. */
	EvtLog *output;
	/** Log file header. */
	const EvtHeader *hdr;
	/** The current record being processed. */
	EvtRecordData rec;
	EvtRecordContents recContents;

	/** Options related to log processing. See CSV2EVT_*. */
	int options;
	/** Current line number. */
	long lineNo;

	/** Whether old records may be overwritten. */
	unsigned overwrite       : 1;
	/** The first record has been successfully written. */
	unsigned firstRecWritten : 1;

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


/** Parse a time token.
 *  @param[in] token  The string to be parsed.
 *  @return -1 on error, otherwise a time_t value that corresponds
 *          to the time the token represents.
 */
static time_t
parseTime (const char *token)
{
	struct tm tm;

	memset (&tm, 0, sizeof tm);

#ifdef HAVE_STRPTIME
	strptime (token, "%Y-%m-%d %H:%M:%S", &tm);
#else /* ! HAVE_STRPTIME */
	sscanf (token, "%4d-%2d-%2d %2d:%2d:%2d",
		&tm.tm_year, &tm.tm_mon, &tm.tm_mday,
		&tm.tm_hour, &tm.tm_min, &tm.tm_sec);
#endif /* ! HAVE_STRPTIME */

#ifdef HAVE__MKGMTIME
	return _mkgmtime (&tm);
#else /* ! HAVE__MKGMTIME */
	return mktime (&tm);
#endif /* ! HAVE__MKGMTIME */
}

/** Read the filesize record. */
static int
readFilesizeRecord (uint32_t *fileSize, CsvReader restrict reader)
{
	char *p, *csvToken = NULL;

	if (csvRead (reader, &csvToken) != CSV_FIELD)
	{
		fputs (_("Error: Failed to read the filesize record."), stderr);
		free (csvToken);
		return FALSE;
	}
	*fileSize = strtol (csvToken, &p, 10);
	if (*p)
	{
		fputs (_("Error: Failed to parse the filesize record."), stderr);
		free (csvToken);
		return FALSE;
	}
	free (csvToken);

	/* Skip other fields. The field this fails on is CSV_EOR. */
	while (csvRead (reader, NULL) == CSV_FIELD)
		continue;

	return TRUE;
}

static void
splitMessageString (ConvCtx *ctx)
{
	const char *p;
	char **strings;
	size_t strings_alloc, n_strings;
	Buffer buf;

	bufferInit (&buf);
	strings = xmalloc (sizeof *strings * (strings_alloc = 4));
	n_strings = 0;

	for (p = ctx->token; ; p++)
	{
		int quoted = 0;

		if (*p == '\\')
		{
			quoted = 1;
			p++;
		}

		/* End of token OR unquoted end of string mark. */
		/* XXX: This always outputs at least one string. */
		if (!*p || (p[0] == '|' && !quoted))
		{
			bufferAppendChar (&buf, '\0');

			if (n_strings >= strings_alloc)
				strings = xrealloc (strings,
					sizeof *strings * (strings_alloc <<= 1));
			strings[n_strings++] = xstrdup (buf.data);

			/* End of token  -> stop processing. */
			if (!*p)
				break;

			/* End of string -> reset our current string. */
			bufferEmpty (&buf);
		}
		else
			bufferAppendChar (&buf, *p);
	}

	if (!n_strings)
	{
		ctx->recContents.numStrings = 0;
		ctx->recContents.strings = NULL;
		free (strings);
	}
	else
	{
		ctx->recContents.numStrings = n_strings;
		ctx->recContents.strings = strings;
	}

	bufferDestroy (&buf);
}

#define ERROR_SKIP_RECORD(m)                                                  \
	do {                                                                      \
		msg = m;                                                              \
		goto processField_error;                                              \
	} while (0)

#define ERROR_WARNING(m) \
	fprintf (stderr, _("Warning at line %ld: %s.\n"), ctx->lineNo, m)

#define READ_UINT32(m)                                                        \
	do {                                                                      \
		number = strtoll (ctx->token, &p, 10);                                \
		if (*p)                                                               \
			ERROR_SKIP_RECORD (m);                                            \
		if (number < 0 || number > UINT32_MAX)                                \
			ERROR_SKIP_RECORD (_("Integer out of uint32_t range"));           \
	} while (0)

/** Process a field from the input file. */
static void
processField (ConvCtx *ctx)
{
	const char *msg;
	switch (ctx->field++)
	{
		/* An anonymous union would be great. */
		char *p;
		time_t myTime;
		long long number;
		void *b64buff;
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

		number = strtoll (ctx->token, &p, 10);
		msg = NULL;

		if (*p)
			msg = _("Invalid record number");
		else if (number < 0 || number > UINT32_MAX)
			msg = _("Integer out of uint32_t range");
		else if (!number)
			msg = _("Record numbers can't be zero");

		if (ctx->options & CSV2EVT_RENUMBER)
		{
			if (msg)
				ERROR_WARNING (msg);
			ctx->rec.header.recordNumber = ctx->hdr->currentRecordNumber;
		}
		else
		{
			if (msg)
				ERROR_SKIP_RECORD (msg);
			if (ctx->firstRecWritten)
			{
				/* TODO: It is allowed to overflow to 0 etc. */
				if ((unsigned) number > ctx->hdr->currentRecordNumber)
					ERROR_WARNING (_("Discontiguous record"));
				else if ((unsigned) number < ctx->hdr->currentRecordNumber)
					ERROR_SKIP_RECORD (_("A record with a record number"
						" less than or equal to the previous record"));
			}
			ctx->rec.header.recordNumber = number;
		}
		break;
	case FIELD_TIME_GEN:
		if (ctx->recFlags & RECORD_EMPTY_FIRST_FIELD)
			ERROR_SKIP_RECORD (_("A record without a record number. You"
				" can prevent this error with the -i option"));

		if ((myTime = parseTime (ctx->token)) == -1)
			ERROR_SKIP_RECORD (
				_("Failed to parse generation time in a record"));
		ctx->recContents.timeGenerated = myTime;
		break;
	case FIELD_TIME_WRI:
		if ((myTime = parseTime (ctx->token)) == -1)
			ERROR_SKIP_RECORD (
				_("Failed to parse written time in a record"));
		ctx->recContents.timeWritten = myTime;
		break;
	case FIELD_EVENT_ID:
		READ_UINT32 (_("Failed to parse event ID"));
		ctx->rec.header.eventID = number;
		break;
	case FIELD_EVENT_TYPE:
		if (!strcmp (ctx->token, "Information"))
			ctx->rec.header.eventType = EVT_INFORMATION_TYPE;
		else if (!strcmp (ctx->token, "Warning"))
			ctx->rec.header.eventType = EVT_WARNING_TYPE;
		else if (!strcmp (ctx->token, "Error"))
			ctx->rec.header.eventType = EVT_ERROR_TYPE;
		else if (!strcmp (ctx->token, "Audit Success"))
			ctx->rec.header.eventType = EVT_AUDIT_SUCCESS;
		else if (!strcmp (ctx->token, "Audit Failure"))
			ctx->rec.header.eventType = EVT_AUDIT_FAILURE;
		else
		{
			READ_UINT32 (_("Failed to parse event type in a record"));
			ctx->rec.header.eventType = number;
		}
		break;
	case FIELD_EVENT_CAT:
		READ_UINT32 (_("Failed to parse event category"));
		ctx->rec.header.eventCategory = number;
		break;
	case FIELD_SOURCE_NAME:
		ctx->recContents.sourceName   = xstrdup (ctx->token);
		break;
	case FIELD_COMPUTER_NAME:
		ctx->recContents.computerName = xstrdup (ctx->token);
		break;
	case FIELD_SID:
		if (!*ctx->token)
			ctx->recContents.userSid  = NULL;
		else
			ctx->recContents.userSid  = xstrdup (ctx->token);
		break;
	case FIELD_STRINGS:
		splitMessageString (ctx);
		break;
	case FIELD_DATA:
		base64_init_decodestate (&b64state);
		ctx->recContents.data = b64buff
			= xmalloc (BASE64_DECODED_BUFFER_SIZE (strlen (ctx->token)));
		ctx->recContents.dataLength = base64_decode_block (ctx->token,
			strlen (ctx->token), b64buff, &b64state);
		break;
	case FIELD_END:
		ERROR_WARNING (_("Extraneous field(s) in a record"));
	default:
		break;
	}
	return;

processField_error:
	fprintf (stderr, _("Error at line %ld: %s. I'm skipping it.\n"),
		ctx->lineNo, msg);
	ctx->field = FIELD_IGNORE;
	ctx->recFlags |= RECORD_IGNORE;
}

static int
processRecord (ConvCtx *ctx)
{
	int error, ret = FALSE;
	EvtEncodeError encErrors;

	if (evtEncodeRecordData (&ctx->recContents, &ctx->rec, &encErrors))
	{
		fprintf (stderr, _("Error at line %ld: Data conversion failed,"
			" skipping record.\n"), ctx->lineNo);
		if (encErrors & EVT_ENCODE_SOURCE_NAME_FAILED)
			fputs (_("Failed to encode the event source name."), stderr);
		if (encErrors & EVT_ENCODE_COMPUTER_NAME_FAILED)
			fputs (_("Failed to encode the computer name."), stderr);
		if (encErrors & EVT_ENCODE_STRINGS_FAILED)
			fputs (_("Failed to encode event strings."), stderr);
		if (encErrors & EVT_ENCODE_SID_FAILED)
			fputs (_("Failed to encode SID string."), stderr);
		goto processRecord_error_encode;
	}

	error = evtAppendRecord (ctx->output, &ctx->rec, ctx->overwrite);
	if (error == EVT_ERROR_LOG_FULL)
	{
		if (ctx->options & CSV2EVT_NOOVERWRITE)
		{
			fputs (_("Error: The log is full."), stderr);
			goto processRecord_error_log;
		}

		fputs (_("Warning: The log is full, removing old records."), stderr);
		ctx->overwrite = 1;
		error = evtAppendRecord (ctx->output, &ctx->rec, 1);
	}

	if (error)
	{
		/* TODO: More information. */
		fputs (_("Error: Log write failed."), stderr);
		goto processRecord_error_log;
	}

	ret = TRUE;
	ctx->firstRecWritten = 1;

processRecord_error_log:
	evtDestroyRecordData (&ctx->rec);
processRecord_error_encode:
	return ret;
}

static void
resetRecord (ConvCtx *ctx)
{
	memset (&ctx->rec, 0, sizeof ctx->rec);
	ctx->rec.header.reserved = EVT_SIGNATURE;

	ctx->field = 0;
	ctx->recFlags = 0;
}

/** Process the input file and output the result into the output file. */
static int
processFile (const char *restrict inpath, const char *restrict outpath,
	int options)
{
	FILE *out, *in;
	ConvCtx ctx;
	CsvReader reader;
	int inputEOF = 0;
	EvtLog *log;
	uint32_t fileSize;
	FileIO *outIO;
	enum EvtHeaderError errInfo = 0;
	int error, ret = FALSE;

	if (!inpath)
		in = stdin;
	else if (!(in = fopen (inpath, "rb")))
	{
		fprintf (stderr,
			_("Error: Failed to open `%s' for reading.\n"), inpath);
		goto processFile_error_input;
	}

	if (!(out = fopen (outpath, "w+b")))
	{
		fprintf (stderr, options & CSV2EVT_APPEND
			? _("Error: Failed to open `%s' for appending.\n")
			: _("Error: Failed to open `%s' for writing.\n"), outpath);
		goto processFile_error_output;
	}

	reader = csvReaderNew (in);

	/* Read the output file size and set it on the output file. */
	if (!readFilesizeRecord (&fileSize, reader))
		goto processFile_error_header;

	outIO = fileIONewForHandle (out);
	if (options & CSV2EVT_APPEND)
		error = evtOpen       (&log, outIO, &errInfo);
	else
		error = evtOpenCreate (&log, outIO, fileSize);

	if (error)
	{
		/* TODO: Use errInfo, ... */
		fputs (_("Error: Failed to open the output log."), stderr);
		goto processFile_error_log;
	}

	memset (&ctx, 0, sizeof ctx);
	ctx.output = log;
	ctx.hdr = evtGetHeader (log);
	ctx.options = options;
	ctx.lineNo = 2;
	ctx.overwrite = 0;
	ctx.firstRecWritten = 0;
	resetRecord (&ctx);

	while (!inputEOF)
	{
		switch (csvRead (reader, &ctx.token))
		{
			const char *p;

		case CSV_FIELD:
			if (ctx.field != FIELD_IGNORE)
				processField (&ctx);

			for (p = ctx.token; *p; p++)
			{
				if (p[0] == '\r' || p[0] == '\n')
					ctx.lineNo++;
				if (p[0] == '\r' && p[1] == '\n')
					p++;
			}

			free (ctx.token);
			break;
		case CSV_EOR:
			if (~ctx.recFlags & RECORD_IGNORE)
			{
				if (ctx.field < FIELD_END)
					fprintf (stderr, _("Error at line %ld: Incomplete record."
						" I'm skipping it.\n"), ctx.lineNo);
				else if (processRecord (&ctx))
					goto processFile_error_content;
			}

			ctx.lineNo++;
			resetRecord (&ctx);
			evtDestroyRecordContents (&ctx.recContents);
			break;
		case CSV_EOF:
			inputEOF = 1;
			break;
		case CSV_ERROR:
			fputs (_("Error: Error reading the input file."), stderr);
			goto processFile_error_content;
		}
	}

	ret = TRUE;

processFile_error_content:
	evtDestroyRecordContents (&ctx.recContents);
	if (evtClose (log))
	{
		fputs (_("Error: Failed to close the log file properly."), stderr);
		ret = FALSE;
	}
processFile_error_log:
	fileIOFree (outIO);
processFile_error_header:
	csvReaderDestroy (reader);
	fclose (out);
processFile_error_output:
	fclose (in);
processFile_error_input:
	return ret;
}

/** Show program usage. */
static void
showUsage (const char *name)
{
	fprintf (stderr, _(
	"Usage: %s [OPTION]... [input-file] output-file\n"
	"\n"
	"Options:\n"
	"  -r    Renumber the records to form a sequence.\n"
	"  -a    Append to the output file rather than create a new one.\n"
	"        Implies -r, so that the result is not just garbage.\n"
	"  -w    Forbid overwriting old records.\n"
	"  -h    Show this help.\n"
	"\n"), name);
}

int
main (int argc, char *argv[])
{
	const char *name, *inpath, *outpath;
	int opt, options;

#ifdef HAVE_GETTEXT
	/* All we want is localized messages. */
	setlocale (LC_MESSAGES, "");

	bindtextdomain (GETTEXT_DOMAIN, GETTEXT_DIRNAME);
	textdomain (GETTEXT_DOMAIN);
#endif /* HAVE_GETTEXT */

#ifndef HAVE__MKGMTIME
	setutctimezone ();
#endif /* ! HAVE__MKGMTIME */

	name = argv[0];
	options = 0;
	while ((opt = getopt (argc, argv, "rawh")) != -1)
	{
		switch (opt)
		{
		case 'a':
			options |= CSV2EVT_APPEND;
		case 'r':
			options |= CSV2EVT_RENUMBER;
			break;
		case 'w':
			options |= CSV2EVT_NOOVERWRITE;
			break;
		case 'h':
			showUsage (name);
			exit (EXIT_SUCCESS);
		default:
			showUsage (name);
			exit (EXIT_FAILURE);
		}
	}

	argc -= optind;
	argv += optind;

	inpath = NULL;
	if (argc == 1)
		outpath = argv[0];
	else if (argc == 2)
	{
		if (strcmp (argv[0], "-"))
			inpath = argv[0];
		outpath = argv[1];
	}
	else
	{
		showUsage (name);
		exit (EXIT_FAILURE);
	}

	if (!processFile (inpath, outpath, options))
		exit (EXIT_FAILURE);

	return 0;
}
