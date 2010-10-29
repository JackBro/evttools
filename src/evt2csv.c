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


static int processFile (const char *__restrict inpath,
	const char *__restrict outpath, unsigned append);
static void processRecord
	(const EvtRecordData *__restrict data, CsvWriter __restrict wrt);


/** Write a CSV field in base64. */
static int
writeFieldBase64 (CsvWriter __restrict wrt,
	const void *__restrict field, size_t length)
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
static void
processRecord
	(const EvtRecordData *__restrict data, CsvWriter __restrict wrt)
{
#if 0
	/* Yes, the buffer is large enough. */
	char buff[40], *cur;
	Buffer final = BUFFER_INITIALIZER;
	EvtRecordContents contents;
	int i;

	/* TODO: The error parameter. */
	if (evtDecodeRecordData (data, &contents, NULL))
		/* TODO: handle this. */;

	/* First field: record number. */
	snprintf (buff, sizeof buff, "%d", data->header.recordNumber);
	csvWrite (wrt, buff);

	/* Second field: time generated (GMT). */
	strftime (buff, sizeof buff, "%Y-%m-%d %H:%M:%S",
		gmtime (&contents.timeGenerated));
	csvWrite (wrt, buff);

	/* Third field: time written (GMT). */
	strftime (buff, sizeof buff, "%Y-%m-%d %H:%M:%S",
		gmtime (&contents.timeWritten));
	csvWrite (wrt, buff);

	/* Fourth field: event ID. */
	snprintf (buff, sizeof buff, "%u", data->header.eventID);
	csvWrite (wrt, buff);

	/* Fifth field: event type. */
	switch (data->header.eventType)
	{
		case EVT_INFORMATION_TYPE:
			csvWrite (wrt, "Information");
			break;
		case EVT_WARNING_TYPE:
			csvWrite (wrt, "Warning");
			break;
		case EVT_ERROR_TYPE:
			csvWrite (wrt, "Error");
			break;
		case EVT_AUDIT_SUCCESS:
			csvWrite (wrt, "Audit Success");
			break;
		case EVT_AUDIT_FAILURE:
			csvWrite (wrt, "Audit Failure");
			break;
		default:
			/* Unknown type: express with a number. */
			snprintf (buff, sizeof buff, "%u", data->header.eventType);
			csvWrite (wrt, buff);
	}

	/* Sixth field: event category. */
	snprintf (buff, sizeof buff, "%d", data->header.eventCategory);
	csvWrite (wrt, buff);

	/* TODO: Are these gonna be guaranteed to be non-null? */
	/* Seventh field: source name (in UTF-8). */
	csvWrite (wrt, contents.sourceName);

	/* Eighth field: computer name (in UTF-8). */
	csvWrite (wrt, contents.computerName);

	/* Nineth field: SID. */
	csvWrite (wrt, contents.userSid);

	/* Tenth field: strings (in UTF-8). */
	for (i = 0; i < contents.numStrings; i++)
	{
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
	csvWrite (wrt, final.data);
	bufferDestroy (&final);

	/* Eleventh field: data (in base64). */
	if (writeFieldBase64 (wrt, contents.data, contents.dataLength))
		csvWrite (wrt, "");

	/* End of record. */
	csvWrite (wrt, NULL);

	evtDestroyRecordContents (&contents);
#endif
}

/** Process an .evt file.
 *  @param[in] inpath  Path to the input file.
 *  @param[out] outpath  Path to the output file or NULL for stdout.
 *  @param[in] append  Whether the records should be just appended.
 *  @return 0 on success, -1 otherwise.
 */
static int
processFile (const char *__restrict inpath, const char *__restrict outpath,
	unsigned append)
{
	FILE *in, *out;
	FileIO *inio;
	struct stat s;
	CsvWriter wrt;
//	EvtLog *log;
	EvtRecordData data;
	int ret = 0, error;

	if (!(in = fopen (inpath, "rb")))
	{
		fprintf(stderr, _("Error: Failed to open %s for reading.\n"), inpath);
		return -1;
	}
	if (fstat (fileno (in), &s))
	{
		fprintf (stderr, _("Error: %s\n"), strerror (errno));
		fclose (in);
		return -1;
	}
	if (!S_ISREG (s.st_mode))
	{
		fprintf (stderr, _("Error: %s is not a regular file.\n"), inpath);
		fclose (in);
		return -1;
	}

#if 0
	if (!outpath)
		out = stdout;
	else if (!(out = fopen (outpath, "wb")))
	{
		fprintf (stderr, _("Error: Failed to open %s for writing.\n"), outpath);
		fclose (in);
		return -1;
	}

	inio = fileIONewForHandle (in);
	log = evtCreate (inio);

	wrt = csvWriterNew (out);

#ifdef HAKUNAMATATA
	if ((fileSize = filelength (fileno (input))) == -1)
	{
		fputs (_("Error: Failed to get file size.\n"), stderr);
		return -1;
	}
	/* Write out a special header record with file size.
	 * (The only non-record value that is really useful
	 * for reconstructing the .evt.)
	 */
	fprintf (output, "%lu\n", fileSize);

	/* XXX: Where is the header processed?! */
	if (evtReadHeader (log))
	{
		fprintf (stderr, _("Error: Failed to read the header.\n"));
		ret = -1;
		goto processFile_end;
	}
#endif /* HAKUNAMATATA */
	if (evtReposition (log, EVT_REPOSITION_FIRST))
	{
		fprintf (stderr, _("Error: Failed to reposition in the file.\n"));
		ret = -1;
		goto processFile_end;
	}

	while (!(error = evtReadRecord (log, &data)))
	{
		processRecord (&data, wrt);
		evtDestroyRecordData (&data);
	}

	switch (error)
	{
	case EVT_READ_EOF:
		break;
	default:
		/* TODO: handle this. */
		ret = -1;
		break;
	}

processFile_end:
	csvWriterDestroy (wrt);
	evtDestroy (log);

	fclose (in);
	fclose (out);
#endif
	return ret;
}

/** Show program usage. */
static void
showUsage (void)
{
	fprintf (stderr, _(
	"Usage: %s [OPTION]... input-file [output-file]\n"
	"\n"
	"Options:\n"
//	"  -r    Renumber the records to form a sequence.\n"
	"  -a    Append to the output file rather than create a new one.\n"
//	"        Implies -r, so that the result is not just garbage.\n"
	"  -h    Show this help.\n"
	"\n"), "evt2csv");
}

int
main (int argc, char *argv[])
{
	unsigned append = 0;
	const char *inpath, *outpath;
	int opt;

#ifdef HAVE_GETTEXT
    /* All we want is localized messages. */
	setlocale (LC_MESSAGES, "");

	bindtextdomain (GETTEXT_DOMAIN, GETTEXT_DIRNAME);
	textdomain (GETTEXT_DOMAIN);
#endif /* HAVE_GETTEXT */

	while ((opt = getopt (argc, argv, "ah")) != -1)
	{
		switch (opt)
		{
		case 'a':
			append = 1;
			break;
		case 'h':
			showUsage ();
			exit (EXIT_SUCCESS);
		default:
			showUsage ();
			exit (EXIT_FAILURE);
		}
	}

	if (argc - optind < 1 || argc - optind > 2)
	{
		showUsage ();
		exit (EXIT_FAILURE);
	}

	inpath = argv[optind++];
	if (argc - optind == 1 && *argv[optind] && strcmp (argv[optind], "-"))
		outpath = argv[optind];
	else
		outpath = NULL;

	if (processFile (inpath, outpath, append))
		exit (EXIT_FAILURE);

	return 0;
}

