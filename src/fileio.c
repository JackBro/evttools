/**
 *  @file fileio.c
 *  @brief File IO wrapper
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <xtnd/xtnd.h>

#include <config.h>
#include "fileio.h"


/* XXX: These -o functions are not available everywhere. */
/* TODO: Maybe move the corresponding parts of config.h in here. */

/** A portable handler to get the position in a stdio FILE. */
static off_t
fileIOTell (void *handle)
{
	assert (handle != NULL);
	return ftello ((FILE *) handle);
}

/** A portable handler to set the position in a stdio FILE. */
static int
fileIOSeek (void *handle, off_t offset, int whence)
{
	assert (handle != NULL);
	return fseeko ((FILE *) handle, offset, whence);
}

/** A portable handler to get the length of a stdio FILE. */
static int64_t
fileIOLength (void *handle)
{
	assert (handle != NULL);
	return filelength (fileno ((FILE *) handle));
}

/** A portable handler to set the length of a stdio FILE. */
static int
fileIOTruncate (void *handle, int64_t length)
{
	assert (handle != NULL);
	return ftruncate (fileno ((FILE *) handle), length);
}


FileIO *
fileIONewForHandle (FILE *file)
{
	FileIO *io;

	assert (file != NULL);

	io = xmalloc (sizeof (FileIO));
	io->handle = file;
	io->tell = fileIOTell;
	io->seek = fileIOSeek;
	io->read = (FileIORead) fread;
	io->write = (FileIOWrite) fwrite;
	io->length = fileIOLength;
	io->truncate = fileIOTruncate;
	return io;
}
