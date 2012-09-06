/**
 *  @file fileio.h
 *  @brief File IO wrapper
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#ifndef __FILE_IO_H__
#define __FILE_IO_H__

/** Read a block of data, advance the position.
 *  The parameters have the same meaning as those for fread().
 */
typedef size_t (*FileIORead)
	(void *ptr, size_t size, size_t nmemb, void *handle);

#define FILE_IO_READ(ptr, size, nmemb, h) \
	((h)->read ((ptr), (size), (nmemb), (h)->handle))


/** Write a block of data, advance the position.
 *  The parameters have the same meaning as those for fwrite().
 */
typedef size_t (*FileIOWrite)
	(const void *ptr, size_t size, size_t nmemb, void *handle);

#define FILE_IO_WRITE(ptr, size, nmemb, h) \
	((h)->write ((ptr), (size), (nmemb), (h)->handle))


/** Get the position in the file.
 *  @param[in] handle  A handle for the file object.
 */
typedef off_t (*FileIOTell) (void *handle);

#define FILE_IO_TELL(h) \
	((h)->tell ((h)->handle))


/** Set the position in the file.
 *  @param[in] offset  The new position value.
 *  @param[in] whence  SEEK_CUR, SEEK_SET or SEEK_END
 */
typedef int (*FileIOSeek) (void *handle, off_t offset, int whence);

#define FILE_IO_SEEK(h, offset, whence) \
	((h)->seek ((h)->handle, (offset), (whence)))


/** Get the length of the file.
 *  @param[in] handle  A handle for the file object.
 */
typedef int64_t (*FileIOLength) (void *handle);

#define FILE_IO_LENGTH(h) \
	((h)->length ((h)->handle))


/** Set the length of the file.
 *  @param[in] handle  A handle for the file object.
 *  @param[in] length  The new length.
 */
typedef int (*FileIOTruncate) (void *handle, int64_t length);

#define FILE_IO_TRUNCATE(h, length) \
	((h)->truncate ((h)->handle, (length)))


/** IO functions. */
typedef struct
{
	void *handle;             /** A pointer to be passed to IO functions. */

	FileIORead     read;      /**< Read some data, advance the position. */
	FileIOWrite    write;     /**< Write some data, advance the position. */
	FileIOTell     tell;      /**< Get the position in the file. */
	FileIOSeek     seek;      /**< Set the position in the file. */
	FileIOLength   length;    /**< Get the length of the file. */
	FileIOTruncate truncate;  /**< Set the length of the file. */
}
FileIO;


/** Create a new FileIO for a FILE.
 *  This object can be destroyed by calling fileIOFree().
 *  @param[in] handle  The handle.
 */
/* Calling code:
 *    fstat -> S_ISREG should be true, otherwise reject the file.
 *    Windows has _fstat since 98 but also has GetFileType.
 */
FileIO *fileIONewForHandle (FILE *handle);

/** Destroy an IO object.
 *  @param[in] io  The object to destroy.
 */
void fileIOFree (FileIO *io);


#endif /* ! __FILE_IO_H__ */
