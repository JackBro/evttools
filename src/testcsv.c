/**
 *  @file testcsv.c
 *  @brief Test the CSV file implementation.
 *
 *  This file is in the public domain.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "configure.h"
#include "xalloc.h"
#include "csv.h"

/** Write some CSV data to a temporary file and try to read it back again. */
int src_testcsv (int argc, char *argv[])
{
	const char *fields[] =
	{
		"1970", "", "ścięśliwy", NULL,
		"", NULL,
		",.-", "\"czeł\n\"owiek\"", NULL
	};
	const int n_fields = sizeof(fields) / sizeof(fields[0]);

	CsvWriter wrt;
	CsvReader rdr;
	FILE *fp;
	int i, fail = 0;
	char *field;

	/* NOTE: On Windows, this tries to create a file in the root. */
	fp = tmpfile();

	wrt = csvCreateWriter(fp);
	for (i = 0; !fail && i < n_fields; i++)
	{
		if (csvWrite(wrt, fields[i]))
			fail = 1;
	}
	csvDestroyWriter(wrt);

	if (fail)
		goto src_testcsv_end;

	rewind(fp);
	rdr = csvCreateReader(fp);
	for (i = 0; !fail && i < n_fields; i++)
	{
		switch (csvRead(rdr, &field))
		{
		case CSV_FIELD:
			if (!fields[i] || strcmp(fields[i], field))
				fail = 1;
			free(field);
			break;
		case CSV_EOR:
			if (fields[i])
				fail = 1;
			break;
		case CSV_EOF:
		case CSV_ERROR:
			fail = 1;
		}
	}

src_testcsv_end:
	fclose(fp);
	puts(fail ? "CSV test failed" : "CSV test passed");
	return fail;
}

