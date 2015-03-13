/**
 *  @file evt.c
 *  @brief Event Log File routines.
 *
 *  Copyright Přemysl Janouch 2010, 2012. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <xtnd/xtnd.h>

#include <config.h>
#include "fileio.h"
#include "evt.h"
#include "sid.h"
#include "widechar.h"
#include "datastruct.h"


/** Many values are aligned at DWORD boundary. */
#define SIZEOF_DWORD 4


/** Size of a member of a struct. */
#define EVT_STRUCT_SIZEOF(s, m) \
	(sizeof (((s *) (0))->m))

/** Construct an item for the field table. */
#define EVT_TABLE_ITEM(s, f) \
	{offsetof (s, f), EVT_STRUCT_SIZEOF (s, f)}

/** End the field table. */
#define EVT_TABLE_END {0, 0}

/** Read a WORD value from a char buffer in little endian format.
 *  @param[in] b  The buffer.
 */
#define EVT_READ_WORD_LE(b) \
	( (uint16_t) (b)[0] \
	| (uint16_t) (b)[1] << 8 )

/** Read a DWORD value from a char buffer in little endian format.
 *  @param[in] b  The buffer.
 */
#define EVT_READ_DWORD_LE(b) \
	( (uint32_t) EVT_READ_WORD_LE (b) \
	| (uint32_t) (b)[2] << 16 \
	| (uint32_t) (b)[3] << 24 )

/** Read a QWORD value from a char buffer in little endian format.
 *  @param[in] b  The buffer.
 */
#define EVT_READ_QWORD_LE(b) \
	( (uint64_t) EVT_READ_DWORD_LE (b) \
	| (uint64_t) (b)[4] << 32 \
	| (uint64_t) (b)[5] << 40 \
	| (uint64_t) (b)[6] << 48 \
	| (uint64_t) (b)[7] << 56 )

/** Read a DWORD value from a circular char buffer in little endian format.
 *  @param[in] b  The buffer.
 *  @param[in] i  Index into the circular buffer.
 *  @param[in] m  Apply this mask to the index.
 */
#define EVT_READ_DWORD_LE_CIRCULAR(b, i, m) \
	( (b)[ (i)      & (m)] \
	| (b)[((i) + 1) & (m)] << 8 \
	| (b)[((i) + 2) & (m)] << 16 \
	| (b)[((i) + 3) & (m)] << 24 )


/** Contains information that is included immediately after the newest
 *  event log record.
 */
typedef struct
{
	/** The beginning size of the EOF record. This value is always 0x28. */
	uint32_t recordSizeBeginning;
	/** An identifier that helps to differentiate this record from other
	 *  records in the event log. The value is always set to 0x11111111. */
	uint32_t one;
	/** An identifier that helps to differentiate this record from other
	 *  records in the event log. The value is always set to 0x22222222. */
	uint32_t two;
	/** An identifier that helps to differentiate this record from other
	 *  records in the event log. The value is always set to 0x33333333. */
	uint32_t three;
	/** An identifier that helps to differentiate this record from other
	 *  records in the event log. The value is always set to 0x44444444. */
	uint32_t four;
	/** The offset to the oldest record. If the event log is empty,
	 *  this is set to the start of this structure. */
	uint32_t beginRecord;
	/** The offset to the start of this structure. */
	uint32_t endRecord;
	/** The record number of the next event that will be written
	 *  to the event log. */
	uint32_t currentRecordNumber;
	/** The record number of the oldest record in the event log.
	 *  The record number will be 0 if the event log is empty. */
	uint32_t oldestRecordNumber;
	/** The ending size of the EvtEOF. This value is always 0x28. */
	uint32_t recordSizeEnd;
}
EvtEOF;

/** An item decriptor for the field table. */
typedef struct
{
	/** Offset of the item in a struct. */
	size_t offset;
	/** The length of the item. */
	size_t length;
}
EvtTableItem;


/** Table for EVT header records. */
static const EvtTableItem evtHeaderTable[] =
{
	EVT_TABLE_ITEM (EvtHeader, headerSize),
	EVT_TABLE_ITEM (EvtHeader, signature),
	EVT_TABLE_ITEM (EvtHeader, majorVersion),
	EVT_TABLE_ITEM (EvtHeader, minorVersion),
	EVT_TABLE_ITEM (EvtHeader, startOffset),
	EVT_TABLE_ITEM (EvtHeader, endOffset),
	EVT_TABLE_ITEM (EvtHeader, currentRecordNumber),
	EVT_TABLE_ITEM (EvtHeader, oldestRecordNumber),
	EVT_TABLE_ITEM (EvtHeader, maxSize),
	EVT_TABLE_ITEM (EvtHeader, flags),
	EVT_TABLE_ITEM (EvtHeader, retention),
	EVT_TABLE_ITEM (EvtHeader, endHeaderSize),
	EVT_TABLE_END
};

/** Table for EVT records. */
static const EvtTableItem evtRecordTable[] =
{
	EVT_TABLE_ITEM (EvtRecordHeader, length),
	EVT_TABLE_ITEM (EvtRecordHeader, reserved),
	EVT_TABLE_ITEM (EvtRecordHeader, recordNumber),
	EVT_TABLE_ITEM (EvtRecordHeader, timeGenerated),
	EVT_TABLE_ITEM (EvtRecordHeader, timeWritten),
	EVT_TABLE_ITEM (EvtRecordHeader, eventID),
	EVT_TABLE_ITEM (EvtRecordHeader, eventType),
	EVT_TABLE_ITEM (EvtRecordHeader, numStrings),
	EVT_TABLE_ITEM (EvtRecordHeader, eventCategory),
	EVT_TABLE_ITEM (EvtRecordHeader, reservedFlags),
	EVT_TABLE_ITEM (EvtRecordHeader, closingRecordNumber),
	EVT_TABLE_ITEM (EvtRecordHeader, stringOffset),
	EVT_TABLE_ITEM (EvtRecordHeader, userSidLength),
	EVT_TABLE_ITEM (EvtRecordHeader, userSidOffset),
	EVT_TABLE_ITEM (EvtRecordHeader, dataLength),
	EVT_TABLE_ITEM (EvtRecordHeader, dataOffset),
	EVT_TABLE_END
};

/** Table for EVT EOF record. */
static const EvtTableItem evtEOFTable[] =
{
	EVT_TABLE_ITEM (EvtEOF, recordSizeBeginning),
	EVT_TABLE_ITEM (EvtEOF, one),
	EVT_TABLE_ITEM (EvtEOF, two),
	EVT_TABLE_ITEM (EvtEOF, three),
	EVT_TABLE_ITEM (EvtEOF, four),
	EVT_TABLE_ITEM (EvtEOF, beginRecord),
	EVT_TABLE_ITEM (EvtEOF, endRecord),
	EVT_TABLE_ITEM (EvtEOF, currentRecordNumber),
	EVT_TABLE_ITEM (EvtEOF, oldestRecordNumber),
	EVT_TABLE_ITEM (EvtEOF, recordSizeEnd),
	EVT_TABLE_END
};


/** Read data into a structure.
 *  @param[in] io         A FileIO object.
 *  @param[in] table      A table that specifies what data should be read.
 *  @param[out] base      Points to the beginning of a structure
 *                        the data should be stored into.
 *  @param[in] itemIndex  The index of the first item to read.
 *  @param[in] itemCount  Number of items to read. Use -1 for all items.
 */
static EvtError
evtRead (FileIO *restrict io,
	const EvtTableItem *restrict table, void *restrict base,
	int itemIndex, int itemCount)
{
	uint8_t buffer[8];
	void *ptr;
	const EvtTableItem *item;

	assert (io    != NULL);
	assert (table != NULL);
	assert (base  != NULL);
	assert (itemIndex >= 0);

	for (item = table + itemIndex; item->length && itemCount--; item++)
	{
		if (FILE_IO_READ (buffer, item->length, 1, io) < 1)
			return EVT_ERROR_IO;

		ptr = (char *) base + item->offset;
		switch (item->length)
		{
		case 1:
			*((uint8_t *)  ptr) = *buffer;
			break;
		case 2:
			*((uint16_t *) ptr) = EVT_READ_WORD_LE  (buffer);
			break;
		case 4:
			*((uint32_t *) ptr) = EVT_READ_DWORD_LE (buffer);
			break;
		case 8:
			*((uint64_t *) ptr) = EVT_READ_QWORD_LE (buffer);
			break;
		default:
			assert (0);
		}
	}
	return EVT_OK;
}

/** Write data from a structure.
 *  @param[in] io     A FileIO object.
 *  @param[in] table  A table that specifies what data should be written.
 *  @param[out] base  Points to the beginning of a structure
 *                    that contains the data to be written.
 */
static EvtError
evtWrite (FileIO *restrict io,
	const EvtTableItem *restrict table, const void *restrict base)
{
	uint8_t buffer[8];
	const void *ptr;
	const EvtTableItem *item;

	assert (io    != NULL);
	assert (table != NULL);
	assert (base  != NULL);

	for (item = table; item->length; item++)
	{
		ptr = (char *) base + item->offset;
		switch (item->length)
		{
			uint16_t  word;
			uint32_t dword;
			uint64_t qword;

		case 1:
			buffer[0] = *(uint8_t *)  ptr;
			break;
		case 2:
			word      = *(uint16_t *) ptr;
			buffer[0] = (word)        & 0xFF;
			buffer[1] = (word  >>  8) & 0xFF;
			break;
		case 4:
			dword     = *(uint32_t *) ptr;
			buffer[0] = (dword)       & 0xFF;
			buffer[1] = (dword >>  8) & 0xFF;
			buffer[2] = (dword >> 16) & 0xFF;
			buffer[3] = (dword >> 24) & 0xFF;
			break;
		case 8:
			qword     = *(uint64_t *) ptr;
			buffer[0] = (qword)       & 0xFF;
			buffer[1] = (qword >>  8) & 0xFF;
			buffer[2] = (qword >> 16) & 0xFF;
			buffer[3] = (qword >> 24) & 0xFF;
			buffer[4] = (qword >> 32) & 0xFF;
			buffer[5] = (qword >> 40) & 0xFF;
			buffer[6] = (qword >> 48) & 0xFF;
			buffer[7] = (qword >> 56) & 0xFF;
			break;
		default:
			assert (0);
		}

		if (FILE_IO_WRITE (buffer, item->length, 1, io) < 1)
			return EVT_ERROR_IO;
	}
	return EVT_OK;
}


/* ===== Errors ============================================================ */

const char *evtXlateError (EvtError error)
{
	switch (error)
	{
	case EVT_OK:              return _("No error");
	default:
	case EVT_ERROR:           return _("General error");
	case EVT_ERROR_IO:        return _("Input/output error");
	case EVT_ERROR_EOF:       return _("End of file reached");
	case EVT_ERROR_LOG_FULL:  return _("The log is full");
	}
}


/* ===== Data manipulation ================================================= */

EvtError
evtDecodeRecordData (const EvtRecordData *restrict input,
	EvtRecordContents *restrict output, EvtDecodeError *restrict errors)
{
	EvtDecodeError errs = 0;
	const EvtRecordHeader *hdr;
	int length;
	char *s;

	assert (input  != NULL);
	assert (output != NULL);

	if (!input->data || input->dataLength
		< EVT_RECORD_MIN_LENGTH - EVT_RECORD_HEADER_LENGTH)
	{
		memset (output, 0, sizeof *output);
		if (errors)
			*errors = EVT_DECODE_INVALID;
		return EVT_ERROR;
	}

	hdr = &input->header;

	/* time_t should represent the number of seconds elapsed since 1970.
	 * This value has to be converted on systems where that's not true.
	 */
	output->timeGenerated = (time_t) hdr->timeGenerated;
	output->timeWritten   = (time_t) hdr->timeWritten;

	length = decodeWideString ((uint16_t *) input->data, input->dataLength, &s);
	if (length)
	{
		output->sourceName = s;

		if (decodeWideString ((uint16_t *) ((char *) input->data + length),
			input->dataLength - length, &s))
			output->computerName = s;
		else
		{
			output->computerName = NULL;
			errs |= EVT_DECODE_COMPUTER_NAME_FAILED;
		}
	}
	else
	{
		output->sourceName = NULL;
		errs |= EVT_DECODE_SOURCE_NAME_FAILED;
	}

	output->numStrings = 0;
	if (hdr->numStrings)
	{
		int offset, len;

		output->strings = xmalloc (sizeof (char *) * hdr->numStrings);
		offset = hdr->stringOffset - EVT_RECORD_HEADER_LENGTH;

		while (output->numStrings < hdr->numStrings)
		{
			if (!(len = decodeWideString ((uint16_t *)
				((char *) input->data + offset), input->dataLength - offset,
				&output->strings[output->numStrings])))
			{
				errs |= EVT_DECODE_STRINGS_FAILED;
				break;
			}
			output->numStrings++;
			offset += len;
		}
	}
	else
		output->strings = NULL;

	output->userSid = NULL;
	if (hdr->userSidOffset + hdr->userSidLength
		> input->dataLength - EVT_RECORD_HEADER_LENGTH - sizeof (uint32_t))
		errs |= EVT_DECODE_SID_OVERFLOW;
	else if (hdr->userSidLength)
	{
		if (!(output->userSid = sidToString ((char *) input->data
			+ hdr->userSidOffset - EVT_RECORD_HEADER_LENGTH,
			hdr->userSidLength)))
			errs |= EVT_DECODE_SID_FAILED;
	}

	if (hdr->dataOffset + hdr->dataLength
		> input->dataLength - EVT_RECORD_HEADER_LENGTH - sizeof (uint32_t))
	{
		errs |= EVT_DECODE_DATA_OVERFLOW;
		output->data = NULL;
		output->dataLength = 0;
	}
	else
	{
		output->data = xmalloc (hdr->dataLength);
		output->dataLength = hdr->dataLength;
		memcpy (output->data, (const char *) input->data + hdr->dataOffset
			- EVT_RECORD_HEADER_LENGTH, hdr->dataLength);
	}

	if (EVT_READ_DWORD_LE ((char *) input->data
		+ input->dataLength - sizeof (uint32_t)) != hdr->length)
		errs |= EVT_DECODE_LENGTH_MISMATCH;

	if (errors)
		*errors = errs;
	return errs ? EVT_ERROR : EVT_OK;
}

EvtError
evtEncodeRecordData (const EvtRecordContents *restrict input,
	EvtRecordData *restrict output,
	EvtEncodeError *restrict errors)
{
	EvtEncodeError errs = 0;
	Buffer data = BUFFER_INITIALIZER;

	uint16_t *wideStr;
	int length;
	unsigned i;

	void *sid;
	size_t sid_length;

	uint8_t length_le[4];

	assert (input  != NULL);
	assert (output != NULL);

	/* See the comment in evtDecodeRecordData(). */
	output->header.timeGenerated = input->timeGenerated;
	output->header.timeWritten   = input->timeWritten;

	/* The first two strings. */
	if ((length = encodeMBString (input->sourceName,   &wideStr)))
		bufferAppend (&data, wideStr, length, 0);
	else
		errs |= EVT_ENCODE_SOURCE_NAME_FAILED;

	if ((length = encodeMBString (input->computerName, &wideStr)))
		bufferAppend (&data, wideStr, length, 0);
	else
		errs |= EVT_ENCODE_COMPUTER_NAME_FAILED;

	/* The SID. */
	if (!input->userSid)
	{
		output->header.userSidLength = 0;
		output->header.userSidOffset = 0;
	}
	else if ((sid = sidToBinary (input->userSid, &sid_length)))
	{
		/* The SID should be aligned on a DWORD boundary. */
		output->header.userSidOffset = EVT_RECORD_HEADER_LENGTH
			+ bufferAppend (&data, sid, sid_length, SIZEOF_DWORD);
		output->header.userSidLength = sid_length;
		free (sid);
	}
	else
		errs |= EVT_ENCODE_SID_FAILED;

	/* The strings. */
	output->header.stringOffset = EVT_RECORD_HEADER_LENGTH + data.used;
	output->header.numStrings = input->numStrings;
	for (i = 0; i < input->numStrings; i++)
	{
		if ((length = encodeMBString (input->strings[i], &wideStr)))
			bufferAppend (&data, wideStr, length, 0);
		else
			errs |= EVT_ENCODE_STRINGS_FAILED;
	}

	if (errs)
	{
		bufferDestroy (&data);

		if (errors)
			*errors = errs;
		return EVT_ERROR;
	}

	/* Unspecified data. */
	output->header.dataLength = input->dataLength;
	output->header.dataOffset = EVT_RECORD_HEADER_LENGTH
		+ bufferAppend (&data, input->data, input->dataLength, 0);

	/* The total length equals the total count of bytes in this record
	 * so far plus a uint32_t variable to store it and any padding
	 * to make the whole record aligned on DWORD boundary.
	 */
	output->header.length = (EVT_RECORD_HEADER_LENGTH + data.used
		+ sizeof (uint32_t) + SIZEOF_DWORD - 1) / SIZEOF_DWORD * SIZEOF_DWORD;

	/* Append that value in little endian. */
	length_le[0] =  output->header.length        & 0xFF;
	length_le[1] = (output->header.length >>  8) & 0xFF;
	length_le[2] = (output->header.length >> 16) & 0xFF;
	length_le[3] = (output->header.length >> 24) & 0xFF;
	bufferAppend (&data, length_le, sizeof length_le, SIZEOF_DWORD);

	output->dataLength = data.used;
	output->data = data.data;
	return EVT_OK;
}

void
evtDestroyRecordContents (EvtRecordContents *input)
{
	assert (input != NULL);

	if (input->strings)
	{
		while (input->numStrings--)
			if (*input->strings)
				free (*(input->strings++));
		free (input->strings);
	}

	if (input->userSid)       free (input->userSid);
	if (input->sourceName)    free (input->sourceName);
	if (input->computerName)  free (input->computerName);
	if (input->data)          free (input->data);

	memset (input, 0, sizeof *input);
}

void
evtDestroyRecordData (EvtRecordData *input)
{
	assert (input != NULL);

	if (input->data)
	{
		free (input->data);
		input->data = NULL;
		input->dataLength = 0;
	}
}


/* ===== Low-level FileIO Interface ======================================== */

EvtError
evtIOSearch (FileIO *io, off_t searchMax, EvtSearchResult *result)
{
	uint8_t buffer[8];
	off_t searched = 8;
	uint32_t length;

	assert (io     != NULL);
	assert (result != NULL);

	*result = EVT_SEARCH_FAIL;

	if (searchMax < 8)
		return EVT_OK;
	if (FILE_IO_READ (buffer, 8, 1, io) < 1)
		return EVT_ERROR_IO;

	while (searched < searchMax)
	{
		/* Search for the signature present in the header and records. */
		if (EVT_READ_DWORD_LE_CIRCULAR (buffer, searched - 4, 7)
			== EVT_SIGNATURE)
		{
			/* Right before the signature, there's a DWORD that tells us
			 * about the length of this record.
			 */
			length = EVT_READ_DWORD_LE_CIRCULAR (buffer, searched - 8, 7);

			if (length == EVT_HEADER_LENGTH)
			{
				if (FILE_IO_SEEK (io, -8, SEEK_CUR))
					return EVT_ERROR_IO;
				*result = EVT_SEARCH_HEADER;
				return EVT_OK;
			}
			if (length >= EVT_RECORD_MIN_LENGTH)
			{
				if (FILE_IO_SEEK (io, -8, SEEK_CUR))
					return EVT_ERROR_IO;
				*result = EVT_SEARCH_RECORD;
				return EVT_OK;
			}
		}

		/* XXX: This should probably make use of the DWORD alignment. */
		if (FILE_IO_READ (buffer + (searched++ & 7), 1, 1, io) < 1)
			return EVT_ERROR_IO;
	}
	return EVT_OK;
}

EvtError
evtIOReadHeader (FileIO *restrict io, EvtHeader *restrict hdr,
	enum EvtHeaderError *restrict errors)
{
	enum EvtHeaderError errs = 0;
	off_t offset;

	assert (io  != NULL);
	assert (hdr != NULL);

	if ((offset = FILE_IO_TELL (io)) == -1)
		return EVT_ERROR_IO;
	/* FIXME: Return ERROR_IO? */
	if (evtRead (io, evtHeaderTable, hdr, 0, -1))
		return EVT_ERROR;

	if (hdr->headerSize != EVT_HEADER_LENGTH
	 || hdr->endHeaderSize != EVT_HEADER_LENGTH)
		errs |= EVT_HEADER_ERROR_WRONG_LENGTH;
	if (hdr->signature != EVT_SIGNATURE)
		errs |= EVT_HEADER_ERROR_WRONG_SIGNATURE;
	if (hdr->majorVersion != 1 || hdr->minorVersion != 1)
		errs |= EVT_HEADER_ERROR_WRONG_VERSION;

	if (errs)
	{
		if (errors)
			*errors = errs;
		return EVT_ERROR;
	}

	return EVT_OK;
}

/* ===== High-level Interface ============================================== */

#define GET_OFFSET                                                            \
	do {                                                                      \
		offset = FILE_IO_TELL (log->io);                                      \
		if (offset == -1)                                                     \
			return EVT_ERROR_IO;                                              \
	} while (0)

/** Position in the log file. */
typedef enum
{
	EVT_REPOSITION_HEADER,        /**< Before the header. */
	EVT_REPOSITION_PAST_HEADER,   /**< Past the header. */
	EVT_REPOSITION_FIRST,         /**< First record. */
	EVT_REPOSITION_EOF            /**< Where the EOF record should be. */
}
EvtReposition;

/** Move to another location within the log file. */
static EvtError
evtReposition (EvtLog *log, EvtReposition where)
{
	off_t offset;

	assert (log != NULL);

	switch (where)
	{
	case EVT_REPOSITION_HEADER:
		offset = 0;
		break;
	case EVT_REPOSITION_PAST_HEADER:
		offset = EVT_HEADER_LENGTH;
		break;
	case EVT_REPOSITION_FIRST:
		offset = log->header.startOffset;
		break;
	case EVT_REPOSITION_EOF:
		offset = log->header.endOffset;
		break;
	default:
		assert (0);
	}

	if (FILE_IO_SEEK (log->io, offset, SEEK_SET))
		return EVT_ERROR_IO;
	return EVT_OK;
}

/** Initialize the header for an empty log. */
static void
evtInitializeHeader (EvtHeader *header, uint32_t size)
{
	assert (header != NULL);

	header->headerSize    = EVT_HEADER_LENGTH;
	header->endHeaderSize = EVT_HEADER_LENGTH;
	header->signature     = EVT_SIGNATURE;
	header->majorVersion  = 1;
	header->minorVersion  = 1;
	header->startOffset   = EVT_HEADER_LENGTH;
	header->endOffset     = EVT_HEADER_LENGTH;
	header->currentRecordNumber = 1;
	header->oldestRecordNumber  = 0;
	header->maxSize       = size;
	header->flags         = 0;
	header->retention     = 0;
}

/** Write out the header. */
static EvtError
evtWriteHeader (EvtLog *log)
{
	assert (log != NULL);

	evtReposition (log, EVT_REPOSITION_HEADER);
	return evtWrite (log->io, evtHeaderTable, &log->header);
}

EvtError
evtOpen (EvtLog *restrict *log, FileIO *restrict io,
	enum EvtHeaderError *errorInfo)
{
	EvtLog *new_log;
	off_t length;
	EvtError err;

	assert (log != NULL);
	assert (io  != NULL);

	length = FILE_IO_LENGTH (io);
	if (length == -1)
		return EVT_ERROR_IO;
	if (length < EVT_HEADER_LENGTH)
		return EVT_ERROR;

	new_log = xmalloc (sizeof *new_log);
	new_log->io = io;
	new_log->changed = 0;
	new_log->firstRecordRead = 0;
	new_log->length = length;

	/* FIXME: Should probably also mark the log as dirty.
	 *        And if not here, then in evtAppendRecord() etc.
	 */
	if ((err = evtIOReadHeader (io, &new_log->header, errorInfo))
	 || (err = evtReposition (new_log, EVT_REPOSITION_FIRST)))
	{
		free (new_log);
		return err;
	}

	*log = new_log;
	return EVT_OK;
}

EvtError
evtOpenCreate (EvtLog *restrict *log, FileIO *restrict io, uint32_t size)
{
	EvtLog *new_log;
	EvtError err;

	assert (log != NULL);
	assert (io  != NULL);

	if (size < EVT_HEADER_LENGTH)
		return EVT_ERROR;
	if (FILE_IO_TRUNCATE (io, size))
		return EVT_ERROR_IO;

	new_log = xmalloc (sizeof *new_log);
	new_log->io = io;
	new_log->changed = 1;
	new_log->firstRecordRead = 0;
	new_log->length = size;

	evtInitializeHeader (&new_log->header, size);
	new_log->header.flags = EVT_HEADER_DIRTY;

	if ((err = evtWriteHeader (new_log))
	 || (err = evtReposition (new_log, EVT_REPOSITION_PAST_HEADER)))
	{
		free (new_log);
		return err;
	}

	*log = new_log;
	return EVT_OK;
}

const EvtHeader *
evtGetHeader (EvtLog *log)
{
	assert (log != NULL);
	return &log->header;
}

EvtError
evtRewind (EvtLog *log)
{
	assert (log != NULL);
	return evtReposition (log, EVT_REPOSITION_FIRST);
}

EvtError
evtReadRecord (EvtLog *restrict log, EvtRecordData *restrict record)
{
	EvtError err;
	off_t offset;
	unsigned isFirst;

	assert (log != NULL);
	assert (record != NULL);

	GET_OFFSET;

	/* XXX: How about EOF records? How are _they_ wrapped? */
	if (log->length - offset < EVT_RECORD_HEADER_LENGTH)
	{
		if ((err = evtReposition (log, EVT_REPOSITION_PAST_HEADER)))
			return err;

		GET_OFFSET;
	}

	if (offset == log->header.endOffset)
		return EVT_ERROR_EOF;

	isFirst = (offset == log->header.startOffset);

	/* Read the length of the following record. */
	if ((err = evtRead (log->io, evtRecordTable, &record->header, 0, 1)))
		return err;

	/* It looks like an EOF record, let's verify it. */
	if (record->header.length == EVT_EOF_LENGTH)
	{
		EvtEOF *eof;

		if ((err = evtRead (log->io, evtEOFTable, &record->header, 1, 4)))
			return err;

		eof = (EvtEOF *) &record->header;
		if (eof->one   == 0x11111111 && eof->two  == 0x22222222 &&
			eof->three == 0x33333333 && eof->four == 0x44444444 &&
			eof->recordSizeEnd == EVT_EOF_LENGTH)
			return EVT_ERROR_EOF;
		else
			return EVT_ERROR;
	}
	/* Shorter than expected for a record. */
	else if (record->header.length < EVT_RECORD_MIN_LENGTH)
		return EVT_ERROR;
	/* The record is bigger than the whole log. */
	else if (record->header.length > log->length - EVT_HEADER_LENGTH)
		return EVT_ERROR;

	/* Looks alright, let's read the rest of the header. */
	if ((err = evtRead (log->io, evtRecordTable, &record->header, 1, -1)))
		return err;

	GET_OFFSET;

	record->dataLength = record->header.length - EVT_RECORD_HEADER_LENGTH;
	record->data = xmalloc (record->dataLength);

	/* The record goes beyond the end of the log file. */
	if (offset + record->dataLength > log->length)
	{
		if (log->header.flags & EVT_HEADER_WRAP)
		{
			if (FILE_IO_READ (record->data,
				log->length - offset, 1, log->io) < 1)
				goto evtReadRecord_io_fail;
			/* Wrap around the end of file and read the rest. */
			if ((err = evtReposition (log, EVT_REPOSITION_PAST_HEADER)))
				goto evtReadRecord_fail;
			if (FILE_IO_READ (record->data,
				record->dataLength - (log->length - offset), 1, log->io) < 1)
				goto evtReadRecord_io_fail;
		}
		else
		{
			/* The log file is probably damaged. */
			err = EVT_ERROR;
			goto evtReadRecord_fail;
		}
	}
	else if (FILE_IO_READ (record->data, record->dataLength, 1, log->io) < 1)
		goto evtReadRecord_io_fail;

	if (isFirst)
	{
		log->firstRecordRead = 1;
		log->firstRecordLen = record->header.length;
	}

	return EVT_OK;

evtReadRecord_io_fail:
	err = EVT_ERROR_IO;

evtReadRecord_fail:
	free (record->data);
	record->data = NULL;
	record->dataLength = 0;

	return err;
}

/** Delete the first record in the log. */
static EvtError
evtDeleteFirst (EvtLog *log)
{
	EvtRecordHeader hdr;
	int64_t endSpace;
	EvtError err;

	/* Not possible. */
	/* XXX: Possibly rather check startOffset against endOffset. */
	if (!log->header.oldestRecordNumber)
		return EVT_ERROR;

	if (!log->firstRecordRead)
	{
		/* Read the header of the first record. */
		if ((err = evtReposition (log, EVT_REPOSITION_FIRST))
		 || (err = evtRead (log->io, evtRecordTable, &hdr, 0, -1)))
			return err;

		log->firstRecordLen = hdr.length;
	}

	/* How much space remains after the current first record. */
	endSpace = log->length - log->header.startOffset - log->firstRecordLen;

	if (endSpace < 0)
		/* The current first record is wrapped. */
		log->header.startOffset = EVT_HEADER_LENGTH - endSpace;
	else if (endSpace < EVT_RECORD_HEADER_LENGTH)
		/* No space for another record behind the current first record.
		 * We must go to the start of the file.
		 * XXX: May there be an EOF record, which fits in this smaller space?
		 */
		log->header.startOffset = EVT_HEADER_LENGTH;
	else
		/* We may simply advance the start offset. */
		log->header.startOffset += log->firstRecordLen;

	/* Reconstruct header information. */
	if (log->header.startOffset == log->header.endOffset)
	{
		log->header.oldestRecordNumber = 0;
		log->firstRecordRead = 0;
	}
	else
	{
		/* Read the header of the new first record. */
		if ((err = evtReposition (log, EVT_REPOSITION_FIRST))
		 || (err = evtRead (log->io, evtRecordTable, &hdr, 0, -1)))
			return err;

		log->header.oldestRecordNumber = hdr.recordNumber;
		log->firstRecordLen = hdr.length;
		log->firstRecordRead = 1;
	}

	return EVT_OK;
}

/** Prepare the log for a write of size @a size. */
static EvtError
evtPrepareWrite (EvtLog *log, uint32_t size)
{
	EvtError err;

	if (log->header.endOffset >= log->length - EVT_RECORD_HEADER_LENGTH)
		size += log->length - log->header.endOffset;

	while (1)
	{
		uint32_t space;

		/* Compute how much free space we effectively have right now. */
		if (log->header.startOffset > log->header.endOffset)
			space = log->header.startOffset - log->header.endOffset;
		else
			space = (log->header.startOffset - EVT_HEADER_LENGTH)
				+ (log->length - log->header.endOffset);

		if (space >= size)
			break;
		if ((err = evtDeleteFirst (log)))
			return err;
	}

	/* The log is empty, makes no sense to write from the middle of it. */
	if (!log->header.oldestRecordNumber)
	{
		log->header.startOffset =
		log->header.endOffset = EVT_HEADER_LENGTH;
		log->header.flags &= ~EVT_HEADER_WRAP;
	}
	else if (log->header.endOffset >= log->length - EVT_RECORD_HEADER_LENGTH)
	{
		/* Unused bytes at the end: 0x00000027 in LE. */
		static const char pat[4] = {0x27, 0x00, 0x00, 0x00};
		off_t i, offset, endSpace;

		assert (log->header.startOffset <= log->header.endOffset);

		/* Fill the end of the log file with the pattern. */
		GET_OFFSET;
		endSpace = log->length - offset;
		for (i = 0; i < endSpace; i++)
			if (FILE_IO_WRITE (pat + (i & 3), 1, 1, log->io) < 1)
				return EVT_ERROR_IO;

		log->header.endOffset = EVT_HEADER_LENGTH;
		log->header.flags |= EVT_HEADER_WRAP;
	}

	if ((err = evtReposition (log, EVT_REPOSITION_EOF)))
		return err;

	return EVT_OK;
}

/** Simulate a write to the log for the purpose of checking free space. */
static EvtError
evtSimulateWrite (uint32_t startOffset, uint32_t *endOffset,
	off_t length, uint32_t size)
{
	if (*endOffset >= length - EVT_RECORD_HEADER_LENGTH)
	{
		if (startOffset > *endOffset)
			return EVT_ERROR;
		*endOffset = EVT_HEADER_LENGTH;
	}

	/* There's just a single contiguous block available. */
	if (startOffset > *endOffset)
	{
		if (startOffset - *endOffset < size)
			return EVT_ERROR;
	}
	/* Otherwise handle the case where we don't fit into the first
	 * block going from endOffset to the end of the file.
	 */
	else if (length - *endOffset >= size)
	{
		/* Write as much as we can to that block and reduce to the
		 * case of writing from the beginning of the file.
		 */
		size -= length - *endOffset;
		*endOffset = EVT_HEADER_LENGTH;

		if (startOffset - EVT_HEADER_LENGTH < size)
			return EVT_ERROR;
	}

	*endOffset += size;
	return EVT_OK;
}

EvtError
evtAppendRecord (EvtLog *log, const EvtRecordData *record, unsigned overwrite)
{
	off_t offset, recordOffset, endSpace;
	EvtError err;

	assert (log    != NULL);
	assert (record != NULL);

	assert (record->header.length
		== EVT_RECORD_HEADER_LENGTH + record->dataLength);

	log->header.flags &= ~EVT_HEADER_LOGFULL_WRITTEN;

	if (!overwrite)
	{
		uint32_t startOffset, endOffset;

		startOffset = log->header.startOffset;
		endOffset   = log->header.endOffset;

		if (evtSimulateWrite (startOffset, &endOffset, log->length,
			EVT_RECORD_HEADER_LENGTH + record->dataLength)
		 || evtSimulateWrite (startOffset, &endOffset, log->length,
			EVT_EOF_LENGTH))
		{
			log->header.flags |= EVT_HEADER_LOGFULL_WRITTEN;
			return EVT_ERROR_LOG_FULL;
		}
	}

	if ((err = evtPrepareWrite (log,
		EVT_RECORD_HEADER_LENGTH + record->dataLength)))
		return err;

	GET_OFFSET;
	recordOffset = offset;

	if ((err = evtWrite (log->io, evtRecordTable, &record->header)))
		return err;

	/* Write the rest of the record. */
	GET_OFFSET;
	endSpace = log->length - offset;
	if (endSpace >= record->dataLength)
	{
		if (FILE_IO_WRITE (record->data, record->dataLength, 1, log->io) < 1)
			return EVT_ERROR_IO;
	}
	else
	{
		if (FILE_IO_WRITE (record->data, endSpace, 1, log->io) < 1)
			return EVT_ERROR_IO;
		if ((err = evtReposition (log, EVT_REPOSITION_PAST_HEADER)))
			return err;
		if (FILE_IO_WRITE ((char *) record->data + endSpace,
			record->dataLength - endSpace, 1, log->io) < 1)
			return EVT_ERROR_IO;
	}

	if (!log->header.oldestRecordNumber)
	{
		log->header.oldestRecordNumber = record->header.recordNumber;
		log->header.startOffset = recordOffset;
		log->firstRecordRead = 1;
		log->firstRecordLen = record->header.length;
	}

	GET_OFFSET;
	log->header.currentRecordNumber = record->header.recordNumber + 1;
	log->header.endOffset = offset;
	log->changed = 1;

	return EVT_OK;
}

/** Write the EOF record according to information in the header. */
static EvtError
evtWriteEOF (EvtLog *log)
{
	EvtEOF eof;
	EvtError err;

	assert (log != NULL);

	if ((err = evtPrepareWrite (log, EVT_EOF_LENGTH)))
		return err;

	/* To fix any mess evtDeleteFirst() might have made. */
	if (!log->header.oldestRecordNumber)
		log->header.startOffset = log->header.endOffset;

	eof.recordSizeBeginning = EVT_EOF_LENGTH;
	eof.recordSizeEnd       = EVT_EOF_LENGTH;
	eof.one                 = 0x11111111;
	eof.two                 = 0x22222222;
	eof.three               = 0x33333333;
	eof.four                = 0x44444444;
	eof.beginRecord         = log->header.startOffset;
	eof.endRecord           = log->header.endOffset;
	eof.currentRecordNumber = log->header.currentRecordNumber;
	eof.oldestRecordNumber  = log->header.oldestRecordNumber;

	if ((err = evtWrite (log->io, evtEOFTable, &eof)))
		return err;

	return EVT_OK;
}

EvtError
evtClose (EvtLog *log)
{
	EvtError err = EVT_OK;

	assert (log != NULL);

	if (log->changed && !(err = evtWriteEOF (log)))
	{
		log->header.flags &= ~EVT_HEADER_DIRTY;
		err = evtWriteHeader (log);
	}

	free (log);
	return err;
}

