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
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <xtnd/xtnd.h>

#include <config.h>
#include "fileio.h"
#include "evt.h"
#include "sid.h"
#include "widechar.h"
#include "datastruct.h"


/** Many values are aligned at this DWORD boundary. */
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
	 *  records in the event log. The value is always set to 0x11111111.
	 */
	uint32_t one;
	/** An identifier that helps to differentiate this record from other
	 *  records in the event log. The value is always set to 0x22222222.
	 */
	uint32_t two;
	/** An identifier that helps to differentiate this record from other
	 *  records in the event log. The value is always set to 0x33333333.
	 */
	uint32_t three;
	/** An identifier that helps to differentiate this record from other
	 *  records in the event log. The value is always set to 0x44444444.
	 */
	uint32_t four;
	/** The offset to the oldest record. If the event log is empty,
	 *  this is set to the start of this structure.
	 */
	uint32_t beginRecord;
	/** The offset to the start of this structure. */
	uint32_t endRecord;
	/** The record number of the next event that will be written
	 *  to the event log.
	 */
	uint32_t currentRecordNumber;
	/** The record number of the oldest record in the event log.
	 *  The record number will be 0 if the event log is empty.
	 */
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


static int evtRead (FileIO *__restrict io,
	const EvtTableItem *__restrict table, void *__restrict base,
	int itemIndex, int itemCount);
static int evtWrite (FileIO *__restrict io,
	const EvtTableItem *__restrict table, const void *__restrict base);



/** Table for EVT header records. */
static EvtTableItem evtHeaderTable[] =
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
static EvtTableItem evtRecordTable[] =
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
static EvtTableItem evtEOFTable[] =
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
static int
evtRead (FileIO *__restrict io,
	const EvtTableItem *__restrict table, void *__restrict base,
	int itemIndex, int itemCount)
{
	uint8_t buffer[8];
	void *ptr;
	const EvtTableItem *item;

	assert (io != NULL);
	assert (table != NULL);
	assert (base != NULL);
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
			return EVT_ERROR;
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
static int
evtWrite (FileIO *__restrict io,
	const EvtTableItem *__restrict table, const void *__restrict base)
{
	uint8_t buffer[8];
	const void *ptr;
	const EvtTableItem *item;

	assert (io != NULL);
	assert (table != NULL);
	assert (base != NULL);

	for (item = table; item->length; item++)
	{
		ptr = (char *) base + item->offset;
		switch (item->length)
		{
			uint16_t word;
			uint32_t dword;
			uint64_t qword;

		case 1:
			buffer[0] = *(uint8_t *)  ptr;
			break;
		case 2:
			word      = *(uint16_t *) ptr;
			buffer[0] = (word)        & 0xFF;
			buffer[1] = (word >> 8)   & 0xFF;
			break;
		case 4:
			dword     = *(uint32_t *) ptr;
			buffer[0] = (dword)       & 0xFF;
			buffer[1] = (dword >> 8)  & 0xFF;
			buffer[2] = (dword >> 16) & 0xFF;
			buffer[3] = (dword >> 24) & 0xFF;
			break;
		case 8:
			qword     = *(uint64_t *) ptr;
			buffer[0] = (qword)       & 0xFF;
			buffer[1] = (qword >> 8)  & 0xFF;
			buffer[2] = (qword >> 16) & 0xFF;
			buffer[3] = (qword >> 24) & 0xFF;
			buffer[4] = (qword >> 32) & 0xFF;
			buffer[5] = (qword >> 40) & 0xFF;
			buffer[6] = (qword >> 48) & 0xFF;
			buffer[7] = (qword >> 56) & 0xFF;
			break;
		default:
			return EVT_ERROR;
		}

		if (FILE_IO_WRITE (buffer, item->length, 1, io) < 1)
			return EVT_ERROR_IO;
	}
	return EVT_OK;
}


/* ===== Data manipulation ================================================= */

int
evtDecodeRecordData (const EvtRecordData *__restrict input,
	EvtRecordContents *__restrict output,
	enum EvtDecodeError *__restrict errors)
{
	enum EvtDecodeError errs = 0;
	int length;
	char *s;
	const EvtRecordHeader *hdr;

	assert (input != NULL);
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
	 * This value has to be converted on systems where it's not true.
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
		memcpy (output->data, input->data + hdr->dataOffset
			- EVT_RECORD_HEADER_LENGTH, hdr->dataLength);
	}

	if (EVT_READ_DWORD_LE ((char *) input->data
		+ input->dataLength - sizeof (uint32_t)) != hdr->length)
		errs |= EVT_DECODE_LENGTH_MISMATCH;

	if (errors)
		*errors = errs;
	return errs ? EVT_ERROR : EVT_OK;
}

int
evtEncodeRecordData (const EvtRecordContents *__restrict input,
	EvtRecordData *__restrict output,
	enum EvtEncodeError *__restrict errors)
{
	enum EvtEncodeError errs = 0;
	Buffer data = BUFFER_INITIALIZER;

	assert (input != NULL);
	assert (output != NULL);

	/* See the comment in evtDecodeRecordData(). */
	output->header.timeGenerated = input->timeGenerated;
	output->header.timeWritten   = input->timeWritten;

	/* The first two strings. */
	uint16_t *wideStr;
	int length;

	if ((length = encodeMBString (input->sourceName,   &wideStr)))
		bufferAppend (&data, wideStr, length, 0);
	else
		errs |= EVT_ENCODE_SOURCE_NAME_FAILED;

	if ((length = encodeMBString (input->computerName, &wideStr)))
		bufferAppend (&data, wideStr, length, 0);
	else
		errs |= EVT_ENCODE_COMPUTER_NAME_FAILED;

	/* The SID. */
	void *sid;
	size_t sid_length;

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
	int i;
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
	uint8_t length_le[4];
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

	if (input->userSid)
		free (input->userSid);
	if (input->sourceName)
		free (input->sourceName);
	if (input->computerName)
		free (input->computerName);
	if (input->data)
		free (input->data);

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
	}
}


/* ===== Low-level FileIO Interface ======================================== */

int
evtIOSearch (FileIO *io, off_t searchMax)
{
	uint8_t buffer[8];
	off_t searched = 8;
	uint32_t length;

	assert (io != NULL);

	if (searchMax < 8)
		return EVT_SEARCH_FAIL;
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
				return EVT_SEARCH_HEADER;
			}
			if (length >= EVT_RECORD_MIN_LENGTH)
			{
				if (FILE_IO_SEEK (io, -8, SEEK_CUR))
					return EVT_ERROR_IO;
				return EVT_SEARCH_RECORD;
			}
		}

		/* XXX: This should probably make use of the DWORD alignment. */
		if (FILE_IO_READ (buffer + (searched++ & 7), 1, 1, io) < 1)
			return EVT_ERROR_IO;
	}
	return EVT_SEARCH_FAIL;
}

int
evtIOReadHeader (FileIO *__restrict io, EvtHeader *__restrict hdr,
	enum EvtHeaderError *__restrict errors)
{
	enum EvtHeaderError errs = 0;
	off_t offset;

	assert (io != NULL);
	assert (hdr != NULL);

	if ((offset = FILE_IO_TELL (io)) == -1)
		return EVT_ERROR_IO;
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

/** Position in the log file. */
enum EvtReposition
{
	EVT_REPOSITION_HEADER,        /**< Before the header. */
	EVT_REPOSITION_PAST_HEADER,   /**< Past the header. */
	EVT_REPOSITION_FIRST,         /**< First record. */
	EVT_REPOSITION_EOF            /**< Where the EOF record should be. */
};

/** Move to another location within the log file. */
static int
evtReposition (EvtLog *log, enum EvtReposition where)
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
		return EVT_ERROR;
	}

	if (FILE_IO_SEEK (log->io, offset, SEEK_SET))
		return EVT_ERROR_IO;
	return EVT_OK;
}

/** Writes out the header at the current position.
 *  @param[in] log  A log file object.
 *  @return EVT_OK, EVT_ERROR, EVT_ERROR_IO
 */
static int
evtWriteHeader (EvtLog *log)
{
	assert (log != NULL);

	evtReposition (log, EVT_REPOSITION_HEADER);
	return evtWrite (log->io, evtHeaderTable, &log->header);
}

/** Initialize the header for an empty log. */
static void
evtInitializeHeader (EvtHeader *header, uint32_t size)
{
	assert (header != NULL);

	header->headerSize = EVT_HEADER_LENGTH;
	header->signature = EVT_SIGNATURE;
	header->majorVersion = 1;
	header->minorVersion = 1;
	header->startOffset = EVT_HEADER_LENGTH;
	header->endOffset = EVT_HEADER_LENGTH;
	header->currentRecordNumber = 1;
	header->oldestRecordNumber = 0;
	header->maxSize = size;
	header->flags = 0;
	header->retention = 0;
	header->endHeaderSize = EVT_HEADER_LENGTH;
}


#if 0

/** Write the EOF record according to information in the header.
 *  @param[in] log  A log file object.
 *  @return EVT_ERROR_IO
 */
int evtWriteEOF (EvtLog *log);


EvtLog *
evtNew (FileIO *io)
{
	EvtLog *log;

	assert (io != NULL);

	log = xmalloc (sizeof *log);
	log->io = io;
//	log->offset = 0;
//	log->state = 0;
	return log;
}

int
evtAppendRecord (EvtLog *log, const EvtRecordData *record)
{
	assert (log != NULL);
	assert (record != NULL);

	if (evtWrite (log->io, evtRecordTable, &record->header))
		return EVT_ERROR;

#ifdef HAKUNAMATATA
	long offset;

	/* The record has to be aligned on a DWORD (4-byte) boundary. */
	offset = bufferAppend (ctx->nonFixed, NULL, sizeof (ctx->rec->length), 4);
	*(uint32_t *) ((char *) ctx->nonFixed->data + offset) = ctx->rec->length
		= sizeof (EvtRecord) + ctx->nonFixed->used;

	offset = xftell (ctx->output);

	/* Write the record. */
	if (writeBlock (ctx, ctx->rec, sizeof (EvtRecord), 0)
		|| writeBlock (ctx, ctx->nonFixed->data, ctx->nonFixed->used, 1))
		exit (EXIT_FAILURE);

	if (!ctx->firstRecRead)
	{
		ctx->firstRecRead = 1;
		ctx->firstRecLength = ctx->rec->length;
		ctx->hdr->oldestRecordNumber = ctx->rec->recordNumber;
		ctx->eof->oldestRecordNumber = ctx->rec->recordNumber;
		ctx->hdr->startOffset = offset;
		ctx->eof->beginRecord = offset;
	}
	ctx->hdr->currentRecordNumber = ctx->rec->recordNumber + 1;
	ctx->eof->currentRecordNumber = ctx->rec->recordNumber + 1;
#endif /* HAKUNAMATATA */

	/* TODO */
	return -1;
}

int
evtWriteEOF (EvtLog *log)
{
	EvtEOF eof;

	assert (log != NULL);

	eof.recordSizeBeginning = EVT_EOF_LENGTH;
	eof.one = 0x11111111;
	eof.two = 0x22222222;
	eof.three = 0x33333333;
	eof.four = 0x44444444;
	eof.beginRecord = log->header.startOffset;
	eof.endRecord = log->header.endOffset;
	eof.currentRecordNumber = log->header.currentRecordNumber;
	eof.oldestRecordNumber = log->header.oldestRecordNumber;
	eof.recordSizeEnd = EVT_EOF_LENGTH;

	/* TODO: */
	if (evtWrite (log->io, evtEOFTable, &eof))
		return EVT_ERROR;

#ifdef HAKUNAMATATA
			/* TODO: Probably move to writeBlock. */
			ctx.hdr->endOffset = ctx.eof->endRecord = xftell (output);

			/* Write the EOF record and the header. */
			writeBlock (&ctx, &eof, sizeof (EvtEOF), 0);
			xfseek (output, 0, SEEK_SET);
			fwrite (&hdr, sizeof hdr, 1, output);
#endif /* HAKUNAMATATA */

	return -1;
}

#ifdef HAKUNAMATATA

/** Get more space in the log file by deleting records from the beginning.
 *  This function might reposition @a fp and eat your hamster.
 *  @param[in,out] ctx  A conversion context to work with.
 *  @param[in] newRecordOffset  The offset of the record for which we are
 *                              trying to get some space.
 *  @return 0 if it was successful, -1 if it was not.
 */
/* If we have wrapped, we'll likely be overwritting our previous
 * records. In that case, we have to advance startOffset and update
 * oldestRecordNumber with the recordNumber of this next item.
 */
static int
getMoreSpace (ConvCtx *ctx, long newRecordOffset)
{
	EvtRecord hdr;
	long endSpace;

	/* Not possible. */
	if (!ctx->hdr->oldestRecordNumber)
		return -1;

	ctx->tailSpace += ctx->firstRecLength;

	/* We have to overwrite our only record. */
	if (ctx->hdr->oldestRecordNumber == ctx->hdr->currentRecordNumber)
	{
		ctx->firstRecRead = 0;
		ctx->firstRecLength = 0;
		ctx->hdr->oldestRecordNumber = 0;
		ctx->eof->oldestRecordNumber = 0;

		/* If it is the EOF record that has to overwrite this record,
		 * this sets the startOffset and beginRecord values to be correct.
		 */
		ctx->hdr->startOffset = ctx->eof->beginRecord = newRecordOffset;
		return 0;
	}

	/* How much space remains after the current first record. */
	endSpace = ctx->hdr->maxSize - ctx->hdr->startOffset - ctx->firstRecLength;

	/* The current first record is wrapped. */
	if (endSpace < 0)
		ctx->hdr->startOffset = sizeof (EvtHeader) - endSpace;
	/* No space for another record behind the current first record. */
	else if (endSpace < (signed) sizeof (EvtRecord))
	{
		/* We must go to the start of the file. */
		ctx->tailSpace += endSpace;
		ctx->hdr->startOffset = sizeof (EvtHeader);
	}
	/* We may simply advance the start offset. */
	else
		ctx->hdr->startOffset += ctx->firstRecLength;

	ctx->eof->beginRecord = ctx->hdr->startOffset;

	/* Read the header of the next record. */
	xfseek (ctx->output, ctx->hdr->startOffset, SEEK_SET);
	if (!fread (&hdr, sizeof (EvtRecord), 1, ctx->output))
		return -1;

	ctx->firstRecLength = hdr.length;
	ctx->hdr->oldestRecordNumber = hdr.recordNumber;
	ctx->eof->oldestRecordNumber = hdr.recordNumber;
	return 0;
}


/** Writes a block of data into a log file.
 *  @param[in] ctx  A conversion context.
 *  @param[in] fp  The log file.
 *  @param[in] data  The data.
 *  @param[in] length  The length of @a data in bytes.
 *  @param[in] maySplit  Whether the block may be split.
 */
static int writeBlock (ConvCtx *ctx, const void *data, size_t length,
	int maySplit)
{
	/* Unused bytes at the end: 0x00000027 in LE. */
	static const char unused[4] = {0x27, 0x00, 0x00, 0x00};
	long offset, endSpace, reqLength, i;
	enum
	{
		/* No wrapping needs to be handled. */
		CSV2EVT_WRITEBLOCK_NO_WRAP,
		/* Split the block. */
		CSV2EVT_WRITEBLOCK_SPLIT,
		/* Fill the unused space and write the block after the header. */
		CSV2EVT_WRITEBLOCK_START
	}
	action;

	offset = xftell (ctx->output);
	endSpace = ctx->hdr->maxSize - offset;
	if (endSpace < 0)
	{
		fputs (_("Error: We've got past the end of log file."), stderr);
		return -1;
	}

	reqLength = length;
	if ((unsigned) endSpace < length)
	{
		ctx->hdr->flags |= EVT_HEADER_WRAP;

		if (maySplit)
			action = CSV2EVT_WRITEBLOCK_SPLIT;
		else
		{
			reqLength += endSpace;
			action = CSV2EVT_WRITEBLOCK_START;
		}
	}
	else
		action = CSV2EVT_WRITEBLOCK_NO_WRAP;

	while (ctx->tailSpace < reqLength)
	{
		if (getMoreSpace (ctx, offset))
		{
			fputs (_("Error: Failed to write a record; not enough space."),
				stderr);
			return -1;
		}
	}
	xfseek (ctx->output, offset, SEEK_SET);

	/* We've got enough space, we may write the block. */
	switch (action)
	{
	case CSV2EVT_WRITEBLOCK_SPLIT:
		fwrite (data, endSpace, 1, ctx->output);
		xfseek (ctx->output, sizeof (EvtHeader), SEEK_SET);
		fwrite ((char *) data + endSpace, length - endSpace, 1, ctx->output);
		break;

	case CSV2EVT_WRITEBLOCK_START:
		for (i = 0; i < endSpace; i++)
			fputc (unused[i & 3], ctx->output);

		xfseek (ctx->output, sizeof (EvtHeader), SEEK_SET);

	case CSV2EVT_WRITEBLOCK_NO_WRAP:
		fwrite (data, length, 1, ctx->output);
	}

	ctx->tailSpace -= reqLength;
	return 0;
}

#endif /* HAKUNAMATATA */


#ifdef HAKUNAMATATA
	if (hdr.flags & EVT_HEADER_DIRTY)
		fputs (_("Warning: The log file is marked dirty.\n"), stderr);
	wraps = hdr.flags & EVT_HEADER_WRAP;

	if (fseek (input, hdr.startOffset, SEEK_SET))
	{
		fprintf (stderr, _("Error: fseek: %s.\n"), strerror(errno));
		return -1;
	}

#endif /* HAKUNAMATATA */

#endif


int
evtOpen (EvtLog *__restrict *log, FileIO *__restrict io,
	enum EvtHeaderError *errorInfo)
{
	EvtLog *new_log;
	int err;

	assert (log != NULL);
	assert (io != NULL);

	new_log = xmalloc (sizeof *new_log);
	new_log->io = io;
	new_log->changed = 0;

	if ((err = evtIOReadHeader (io, &new_log->header, errorInfo)))
	{
		free (new_log);
		return err;
	}

	*log = new_log;
	return EVT_OK;
}

int
evtOpenCreate (EvtLog *__restrict *log, FileIO *__restrict io, uint32_t size)
{
	EvtLog *new_log;
	int err;

	assert (log != NULL);
	assert (io != NULL);

	if (FILE_IO_TRUNCATE (io, size))
		return EVT_ERROR_IO;

	new_log = xmalloc (sizeof *new_log);
	new_log->io = io;
	new_log->changed = 1;

	evtInitializeHeader (&new_log->header, size);
	new_log->header.flags = EVT_HEADER_DIRTY;

	if ((err = evtWriteHeader (new_log)))
	{
		free (new_log);
		return err;
	}

	*log = new_log;
	return EVT_OK;
}

int
evtClose (EvtLog *log)
{
	int err = EVT_OK;

	assert (log != NULL);

	if (log->changed)
	{
		/* TODO: Write the EOF record. */

		log->header.flags &= ~EVT_HEADER_DIRTY;
		err = evtWriteHeader (log);
	}

	free (log);
	return err;
}

const EvtHeader *
evtGetHeader (EvtLog *log)
{
	assert (log != NULL);
	return &log->header;
}

int
evtRewind (EvtLog *log)
{
	assert (log != NULL);
	return evtReposition (log, EVT_REPOSITION_FIRST);
}

int
evtReadRecord (EvtLog *log, EvtRecordData *record)
{
	int err;
	off_t offset;
	int64_t length;

	assert (log != NULL);
	assert (record != NULL);

	offset = FILE_IO_TELL   (log->io);
	length = FILE_IO_LENGTH (log->io);

	if (offset == -1 || length == -1)
		return EVT_ERROR_IO;

	/* XXX: How about EOF records? How are _they_ wrapped? */
	if (length - offset < EVT_RECORD_HEADER_LENGTH)
		if (evtReposition (log, EVT_REPOSITION_PAST_HEADER))
			return EVT_ERROR_IO;

	/* Read the length of the following record. */
	if ((err = evtRead (log->io, evtRecordTable, &record->header, 0, 1)))
		return err;

	/* It looks like an EOF record, let's verify it. */
	if (record->header.length == EVT_EOF_LENGTH)
	{
		EvtEOF *eof;

		if (evtRead (log->io, evtEOFTable, &record->header, 1, 4))
			return EVT_ERROR;

		eof = (EvtEOF *) &record->header;
		if (eof->one   == 0x11111111 && eof->two  == 0x22222222 &&
			eof->three == 0x33333333 && eof->four == 0x44444444 &&
			eof->recordSizeEnd == EVT_EOF_LENGTH)
			return EVT_READ_EOF;
		else
			return EVT_ERROR;
	}
	/* Shorter than expected for a record. */
	else if (record->header.length < EVT_RECORD_MIN_LENGTH)
		return EVT_ERROR;
	/* The record overflows the file. */
	else
	{
		offset = FILE_IO_TELL (log->io);
		if (offset + record->header.length > length)
			return EVT_ERROR;
	}

	/* Looks alright, let's read the rest of the header. */
	if ((err = evtRead (log->io, evtRecordTable, &record->header, 1, -1)))
		return err;

	/* TODO: Check the record information, read the rest etc. */

	record->dataLength = record->header.length - EVT_RECORD_HEADER_LENGTH;
	record->data = xmalloc (record->dataLength);

#if 0
	/* FIXME: These functions are not checked for errors. */
	if (wraps)
	{
		offset = FILE_IO_TELL (log->io);
		if (offset + record->dataLength > length)
		{
			log->io->read (record->data, length - offset, 1, log->io->handle);
			/* Wrap around the end of file and read the rest. */
			evtReposition (log, EVT_REPOSITION_PAST_HEADER);
			log->io->read (record->data, record->dataLength
				- (length - offset), 1, log->io->handle);
		}
		else
			log->io->read (record->data, record->dataLength, 1, log->io->handle);
	}
	else
		log->io->read (record->data, record->dataLength, 1, log->io->handle);

	if (err)
		free (record->data);
#endif

	return EVT_OK;
}

int
evtAppendRecord (EvtLog *log, const EvtRecordData *record, unsigned overwrite)
{
	assert (log != NULL);
	assert (record != NULL);

	/* TODO */
	return EVT_OK;
}
