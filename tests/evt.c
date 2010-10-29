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

#include <xtnd/xtnd.h>

#include <config.h>
#include <src/fileio.h>
#include <src/evt.h>

/* TODO: Describe the purpose. */
int tests_evt (int argc, char *argv[])
{
	EvtLog *log;
	FILE *fp;
	int i, fail = 0;

	/* NOTE: On Windows, this tries to create a file in the root. */
	fp = tmpfile ();

	/* TODO: Some actual testing. */
	log = NULL;

src_testevt_end:
	fclose (fp);
	puts (fail ? "EVT test failed" : "EVT test passed");
	return fail;
}

