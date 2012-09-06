/**
 *  @file datastruct.h
 *  @brief Some basic data structures.
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#ifndef __DATASTRUCT_H__
#define __DATASTRUCT_H__

/** The minimal size of a buffer. */
#define BUFFER_BLOCK_SIZE 128

/** You can initialize the Buffer structure with this. */
#define BUFFER_INITIALIZER {NULL, 0, 0, 0}


/** A simple buffer. The fields are not private, but take
 *  care of what you're doing with them.
 */
typedef struct
{
	/** A pointer to the buffer data. */
	void *data;
	/** How much memory has been allocated for the buffer. */
	size_t allocated;
	/** How much memory is currently used for the buffer. */
	size_t used;
	/** Where we currently are. */
	unsigned int cursor;
}
Buffer;


/** Initialize a @a Buffer object.
 *  @param[out] buf  A Buffer object.
 */
static inline void bufferInit (Buffer *buf)
{
	/* Might be better to allocate it here instead of letting
	 * bufferAppend do it and check for NULL pointers all the time.
	 */
	buf->data = NULL;

	/* At the beginning of a 0-sized buffer. */
	buf->allocated = buf->used = buf->cursor = 0;
}

/** Append some data to the non-fixed Buffer. This function cannot
 *  return failure. Either it returns or it exits the program.
 *  @param[in,out] buf  A buffer object.
 *  @param[in] data  The data to append to the buffer. If this pointer is NULL,
 *                   the function will only reserve space for the data.
 *  @param[in] length  The length of @a data in bytes.
 *  @param[in] align  If non-zero, align the data in the buffer
 *                    on the specified boundary. Any padding will be zeroed.
 *  @return The offset at which the data have been pasted into the buffer.
 */
int bufferAppend (Buffer *restrict buf,
	const void *restrict data, size_t length, size_t align);

/** Append a character to a @a Buffer object.
 *  @param[in,out] buf  A buffer object.
 *  @param[in] c  The character to append.
 *  @return The offset at which the character has been pasted into the buffer.
 */
int bufferAppendChar (Buffer *buf, char c);

/** Destroy a @a Buffer object.
 *  @param[in,out] buf  A buffer object.
 */
static inline void bufferDestroy (Buffer *buf)
{
	if (buf->data)
		free (buf->data);
}

/** Empty a @a Buffer object.
 *  @param[in,out] buf  A buffer object.
 */
static inline void bufferEmpty (Buffer *buf)
{
	bufferDestroy (buf);
	bufferInit (buf);
}


#endif /* ! __DATASTRUCT_H__ */
