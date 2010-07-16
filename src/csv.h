/**
 *  @file csv.h
 *  @brief Routines for CSV files.
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 *  There's no support for using semicolons instead of commas,
 *  though it can be easily added.
 *
 */

#ifndef CSV_H_INCLUDED
#define CSV_H_INCLUDED

/** A CSV file reader. */
typedef struct CsvReader *CsvReader;

/** Creates a CSV reader for an opened file stream. */
CsvReader csvCreateReader (FILE *stream);

/** What has been read. */
typedef enum
{
	/** A normal field. */
	CSV_FIELD,
	/** End of record. */
	CSV_EOR,
	/** End of file. */
	CSV_EOF,
	/** An error has occured. */
	CSV_ERROR
}
CsvReadStatus;

/** Read a field from the file.
 *  @param[in]  rdr    A reader object.
 *  @param[out] field  The field data, if the function returns CSV_FIELD.
 *                     You may pass NULL if you don't want it. It has to be
 *                     freed with the standard system free() function.
 */
CsvReadStatus csvRead (CsvReader rdr, char **field);

/** Destroy a CSV reader.
 *  @param[in] rdr  A reader object.
 */
void csvDestroyReader (CsvReader rdr);


/** A CSV file writer. */
typedef struct CsvWriter *CsvWriter;

/** Creates a CSV reader for an opened file stream. */
CsvWriter csvCreateWriter (FILE *stream);

/** Write a field or end of record.
 *  @param[in]  stream  A file stream.
 *  @param[int] field   The field to be written or end of record if NULL.
 *  @return Zero on success, non-zero otherwise.
 */
int csvWrite (CsvWriter wrt, const char *field);

/** Destroy a CSV writer.
 *  @param[in] wrt  A writer object.
 */
void csvDestroyWriter (CsvWriter wrt);

#endif /* ! CSV_H_INCLUDED */

