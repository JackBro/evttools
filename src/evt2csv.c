/**
 *  @file evt2csv.c
 *  @brief .evt to CSV convertor
 *
 *  Copyright Přemysl Janouch 2010, 2012. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <xtnd/xtnd.h>

#include <config.h>
#include "fileio.h"
#include "evt.h"
#include "base64.h"
#include "datastruct.h"


/** Write a CSV field in base64. */
static int
writeFieldBase64 (CsvWriter restrict wrt,
	const void *restrict field, size_t length)
{
	base64_encodestate state;
	char *buff;
	int offset, ret;

	base64_init_encodestate (&state);
	buff = xmalloc (BASE64_ENCODED_BUFFER_SIZE (length));

	offset = base64_encode_block (field, length, buff, &state);
	base64_encode_blockend (buff + offset, &state);
	ret = csvWrite (wrt, buff);
	free (buff);
	return ret;
}

/** Process a record. */
static int
processRecord (const EvtRecordData *restrict data, CsvWriter restrict writer)
{
	/* Yes, the buffer is large enough. */
	char buff[40];
	const char *eventType;
	Buffer final = BUFFER_INITIALIZER;
	EvtRecordContents contents;
	EvtDecodeError errorInfo;
	EvtError error;
	unsigned i;

	if ((error = evtDecodeRecordData (data, &contents, &errorInfo)))
	{
		/* TODO: Show some information. */
		return error;
	}

	/* 1st field: record number. */
	snprintf (buff, sizeof buff, "%d", data->header.recordNumber);
	csvWrite (writer, buff);

	/* 2nd field: time generated (GMT). */
	strftime (buff, sizeof buff, "%Y-%m-%d %H:%M:%S",
		gmtime (&contents.timeGenerated));
	csvWrite (writer, buff);

	/* 3rd field: time written (GMT). */
	strftime (buff, sizeof buff, "%Y-%m-%d %H:%M:%S",
		gmtime (&contents.timeWritten));
	csvWrite (writer, buff);

	/* 4th field: event ID. */
	snprintf (buff, sizeof buff, "%u", data->header.eventID);
	csvWrite (writer, buff);

	/* 5th field: event type. */
	switch (data->header.eventType)
	{
		case EVT_INFORMATION_TYPE:  eventType = "Information";    break;
		case EVT_WARNING_TYPE:      eventType = "Warning";        break;
		case EVT_ERROR_TYPE:        eventType = "Error";          break;
		case EVT_AUDIT_SUCCESS:     eventType = "Audit Success";  break;
		case EVT_AUDIT_FAILURE:     eventType = "Audit Failure";  break;

		default:
			/* Unknown type: express with a number. */
			snprintf (buff, sizeof buff, "%lu",
				(unsigned long) data->header.eventType);
			eventType = buff;
	}
	csvWrite (writer, eventType);

	/* 6th field: event category. */
	snprintf (buff, sizeof buff, "%d", data->header.eventCategory);
	csvWrite (writer, buff);

	/* TODO: Are these gonna be guaranteed to be non-null? */
	/* 7th field: source name (in UTF-8). */
	csvWrite (writer, contents.sourceName);

	/* 8th field: computer name (in UTF-8). */
	csvWrite (writer, contents.computerName);

	/* 9th field: SID. */
	csvWrite (writer, contents.userSid);

	/* 10th field: strings (in UTF-8). */
	for (i = 0; i < contents.numStrings; i++)
	{
		char *cur;

		if (i)
			bufferAppendChar (&final, '|');

		/* The strings are separated by '|' chars.
		 * This separator may be escaped with '\'.
		 */
		for (cur = contents.strings[i]; *cur; cur++)
		{
			if (*cur == '|' || *cur == '\\')
				bufferAppendChar (&final, '\\');
			bufferAppendChar (&final, *cur);
		}
	}
	bufferAppendChar (&final, '\0');
	csvWrite (writer, final.data);
	bufferDestroy (&final);

	/* 11th field: data (in base64). */
	if (writeFieldBase64 (writer, contents.data, contents.dataLength))
		csvWrite (writer, "");

	/* End of record. */
	csvWrite (writer, NULL);

	evtDestroyRecordContents (&contents);
	return EVT_OK;
}

/** Process an .evt file.
 *  @param[in] inpath  Path to the input file.
 *  @param[out] outpath  Path to the output file or NULL for stdout.
 *  @param[in] append  Whether the records should be just appended.
 *  @return 0 on success, -1 otherwise.
 */
static int
processFile (const char *restrict inpath, const char *restrict outpath,
	unsigned append)
{
	FILE *in, *out;
	FileIO *inIO;
	struct stat s;
	CsvWriter writer;
	EvtLog *log;
	EvtRecordData data;
	enum EvtHeaderError errorInfo;
	const EvtHeader *header;
	int error, ret = FALSE;

	if (!(in = fopen (inpath, "rb")))
	{
		fprintf (stderr,
			_("Error: Failed to open `%s' for reading.\n"), inpath);
		goto processFile_error_input;
	}
	if (fstat (fileno (in), &s))
	{
		fprintf (stderr, _("Error: `%s'\n"), strerror (errno));
		goto processFile_error_output;
	}
	if (!S_ISREG (s.st_mode))
	{
		fprintf (stderr, _("Error: `%s' is not a regular file.\n"), inpath);
		goto processFile_error_output;
	}

	if (!outpath)
		out = stdout;
	else if (!(out = fopen (outpath, append ? "ab" : "wb")))
	{
		fprintf (stderr, append
			? _("Error: Failed to open `%s' for appending.\n")
			: _("Error: Failed to open `%s' for writing.\n"), outpath);
		goto processFile_error_output;
	}

	inIO = fileIONewForHandle (in);
	if (evtOpen (&log, inIO, &errorInfo))
	{
		/* TODO: Show some information. */
		fputs (_("Error: Opening the log file failed."), stderr);
		goto processFile_error_log;
	}

	/* Write out a special header record with file size.
	 * (The only non-record value that is really useful
	 * for reconstructing the .evt.)
	 */
	writer = csvWriterNew (out);
	if (!append)
		fprintf (out, "%lu\n", (unsigned long) log->length);

	header = evtGetHeader (log);
	if (header->flags & EVT_HEADER_DIRTY)
		fputs (_("Warning: The log file is marked dirty."), stderr);

	while (!(error = evtReadRecord (log, &data)))
	{
		error = processRecord (&data, writer);
		evtDestroyRecordData (&data);

		if (error)
			break;
	}

	switch (error)
	{
	case EVT_ERROR_EOF:
		break;
	default:
		/* TODO: More information. */
		goto processFile_error_content;
	}

	ret = TRUE;

processFile_error_content:
	if (evtClose (log))
	{
		fputs (_("Error: Failed to close the log file properly."), stderr);
		ret = FALSE;
	}
	csvWriterDestroy (writer);
processFile_error_log:
	fclose (out);
	fileIOFree (inIO);
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
	"Usage: %s [OPTION]... input-file [output-file]\n"
	"\n"
	"Options:\n"
	"  -a    Append to the output file rather than create a new one.\n"
	"  -h    Show this help.\n"
	"\n"), name);
}

int
main (int argc, char *argv[])
{
	const char *name, *inpath, *outpath;
	int opt, append = 0;

#ifdef HAVE_GETTEXT
    /* All we want is localized messages. */
	setlocale (LC_MESSAGES, "");

	bindtextdomain (GETTEXT_DOMAIN, GETTEXT_DIRNAME);
	textdomain (GETTEXT_DOMAIN);
#endif /* HAVE_GETTEXT */

	name = argv[0];
	while ((opt = getopt (argc, argv, "ah")) != -1)
	{
		switch (opt)
		{
		case 'a':
			append = 1;
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

	if (argc < 1 || argc > 2)
	{
		showUsage (name);
		exit (EXIT_FAILURE);
	}

	inpath = argv[0];
	if (argc == 2 && strcmp (argv[1], "-"))
		outpath = argv[1];
	else
		outpath = NULL;

	if (!processFile (inpath, outpath, append))
		exit (EXIT_FAILURE);

	return 0;
}

