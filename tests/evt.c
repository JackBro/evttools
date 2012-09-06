/**
 *  @file testevt.c
 *  @brief Test the EVT log file operations.
 *
 *  This file is in the public domain.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <xtnd/xtnd.h>

#include <config.h>
#include <src/fileio.h>
#include <src/evt.h>


static void fail (const char *msg)
{
	puts (msg);
	exit (1);
}

/** The most basic test of record processing: encode some data,
 *  decode them back, and compare the results.
 */
static void
test_conversion (void)
{
	EvtRecordContents original, reconstructed;
	EvtRecordData encoded = {{0}};
	unsigned i;

	original.timeWritten   = time (NULL);
	original.timeGenerated = time (NULL);

	original.strings = xmalloc ((original.numStrings = 4)
		* sizeof *original.strings);
	original.strings[0]   = xstrdup ("Grzegorz");
	original.strings[1]   = xstrdup ("Brzęczyszczykiewicz");
	original.strings[2]   = xstrdup ("powiąt");
	original.strings[3]   = xstrdup ("Łękołody");

	original.userSid      = xstrdup ("S-1-5-32-544");
	original.sourceName   = xstrdup ("Ты можешь подождать ещё год?");
	original.computerName = xstrdup ("Они сказали, чтобы я не волновался.");

	original.data = xmalloc (original.dataLength = rand () % 512);
	for (i = 0; i < original.dataLength; i++)
		((char *) original.data)[i] = rand () % 256;

	if (evtEncodeRecordData (&original, &encoded, NULL))
		fail ("Encoding record data failed");
	if (evtDecodeRecordData (&encoded, &reconstructed, NULL))
		fail ("Decoding record data failed");

	/* Check if we've got what we put in at the beginning. */
	if (original.timeWritten   != reconstructed.timeWritten
	 || original.timeGenerated != reconstructed.timeGenerated)
		fail ("Time information didn't make it");

	for (i = 0; i < original.numStrings; i++)
		if (strcmp (original.strings[i], reconstructed.strings[i]))
			fail ("Message strings didn't make it");

	if (strcmp (original.userSid,      reconstructed.userSid))
		fail ("The SID didn't make it");
	if (strcmp (original.sourceName,   reconstructed.sourceName))
		fail ("The source name didn't make it");
	if (strcmp (original.computerName, reconstructed.computerName))
		fail ("The computer name didn't make it");

	if (original.dataLength != reconstructed.dataLength
	 || memcmp (original.data, reconstructed.data, original.dataLength))
		fail ("Attached data didn't make it");

	evtDestroyRecordContents (&original);
	evtDestroyRecordContents (&reconstructed);
	evtDestroyRecordData (&encoded);
}

/** Test the EVT log interface. */
int
tests_evt (int argc, char *argv[])
{
	EvtLog *log;
	FILE *fp;
	FileIO *io;

	puts ("-- Trying record data conversion...");
	test_conversion ();

	puts ("-- Trying to create a log file...");

	/* NOTE: On Windows, this tries to create a file in the root. */
	fp = tmpfile ();
	if (!fp)
		fail ("Failed to create a temporary file");

	io = fileIONewForHandle (fp);
	if (evtOpenCreate (&log, io, 0x20000))
		fail ("Initializing the log failed");

	/* TODO: More. */

	if (evtClose (log))
		fail ("Closing the log failed");

	fileIOFree (io);
	fclose (fp);

	puts ("-- EVT test successful");
	return 0;
}

