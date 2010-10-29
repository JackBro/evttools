/**
 *  @file evt.h
 *  @brief Event Log File structures
 *
 *  Based on MSDN documentation:
 *   - http://msdn.microsoft.com/en-us/library/bb309026(VS.85).aspx
 *   - http://msdn.microsoft.com/en-us/library/bb309024(VS.85).aspx
 *   - http://msdn.microsoft.com/en-us/library/aa363646(VS.85).aspx
 *   - http://msdn.microsoft.com/en-us/library/bb309022(VS.85).aspx
 *
 *  Copyright Přemysl Janouch 2010, 2012. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#ifndef __EVT_H__
#define __EVT_H__

/* ===== File Format Specific Information ================================== */

/** The signature is 0x654c664c, which is ASCII for eLfL. */
#define EVT_SIGNATURE              0x654c664c

/** The length of the log header structure. */
#define EVT_HEADER_LENGTH          0x30
/** The minimal possible length of a regular record.
 *  2 bytes for end of sourceName,
 *  2 bytes for end of computerName,
 *  4 bytes for the end length.
 */
#define EVT_RECORD_MIN_LENGTH      0x40
/** The length of a regular record header. */
#define EVT_RECORD_HEADER_LENGTH   0x38
/** The length of the log EOF record. */
#define EVT_EOF_LENGTH             0x28

/** Indicates that records have been written to an event log,
 *  but the event log file has not been properly closed.
 */
#define EVT_HEADER_DIRTY           0x0001
/** Indicates that records in the event log have wrapped. */
#define EVT_HEADER_WRAP            0x0002
/** Indicates that the most recent write attempt failed
 *  due to insufficient space. */
#define EVT_HEADER_LOGFULL_WRITTEN 0x0004
/** Indicates that the archive attribute has been set for the file. */
#define EVT_HEADER_ARCHIVE_SET     0x0008

/** Contains information that is included at the beginning of an event log. */
typedef struct
{
	/** The size of the header structure. The size is always 0x30. */
	uint32_t headerSize;
	/** Always the value of @a EVT_SIGNATURE.  */
	uint32_t signature;
	/** The major version number of the event log, always set to 1. */
	uint32_t majorVersion;
	/** The minor version number of the event log, always set to 1. */
	uint32_t minorVersion;
	/** The offset to the oldest record in the event log. */
	uint32_t startOffset;
	/** The offset to the EvtEOF in the event log. */
	uint32_t endOffset;
	/** The number of the next record that will be added to the event log. */
	uint32_t currentRecordNumber;
	/** The number of the oldest record in the event log.
	 *  For an empty file, the oldest record number is set to 0.
	 */
	uint32_t oldestRecordNumber;
	/** The maximum size, in bytes, of the event log. The maximum size
	 *  is defined when the event log is created. The event-logging service
	 *  does not typically update this value, it relies on the registry
	 *  configuration. The reader of the event log can use normal file APIs
	 *  to determine the size of the file.
	 */
	uint32_t maxSize;
	/** Status of the event log. See the EVT_HEADER_* definitions. */
	uint32_t flags;
	/** The retention value of the file when it is created. The event
	 *  logging service does not typically update this value, it relies
	 *  on the registry configuration.
	 */
	uint32_t retention;
	/** The ending size of the header structure. The size is always 0x30. */
	uint32_t endHeaderSize;
}
EvtHeader;


/** The type of an event. */
typedef enum
{
	/** Error event. */
	EVT_ERROR_TYPE       = 0x0001,
	/** Warning event. */
	EVT_WARNING_TYPE     = 0x0002,
	/** Information event. */
	EVT_INFORMATION_TYPE = 0x0004,
	/** Success Audit event. */
	EVT_AUDIT_SUCCESS    = 0x0008,
	/** Failure Audit event. */
	EVT_AUDIT_FAILURE    = 0x0010
}
EvtEventType;

/** Contains information about an event record. */
typedef struct
{
	/** The size of this event record, in bytes. Note that this value
	 *  is stored at both ends of the entry to ease moving forward
	 *  or backward through the log. The length includes any pad bytes
	 *  inserted at the end of the record.
	 */
	uint32_t length;
	/** A value that is always set to @a EVT_SIGNATURE. */
	uint32_t reserved;
	/** The number of the record. */
	uint32_t recordNumber;
	/** The time at which this entry was submitted. This time is measured
	 *  in the number of seconds elapsed since 00:00:00 January 1, 1970,
	 *  Universal Coordinated Time.
	 */
	uint32_t timeGenerated;
	/** The time at which this entry was received by the service
	 *  to be written to the log. This time is measured in the number
	 *  of seconds elapsed since 00:00:00 January 1, 1970,
	 *  Universal Coordinated Time.
	 */
	uint32_t timeWritten;
	/** The event identifier. The value is specific to the event source
	 *  for the event, and is used with source name to locate a description
	 *  string in the message file for the event source.
	 */
	uint32_t eventID;
	/** The type of event.
	 *  @see EvtEventType
	 */
	uint16_t eventType;
	/** The number of strings present in the log (at the position indicated
	 *  by @a stringOffset). These strings are merged into the message
	 *  before it is displayed to the user.
	 */
	uint16_t numStrings;
	/** The category for this event. The meaning of this value depends
	 *  on the event source.
	 */
	uint16_t eventCategory;
	/** Reserved. */
	uint16_t reservedFlags;
	/** Reserved. */
	uint32_t closingRecordNumber;
	/** The offset of the description strings within this event log record. */
	uint32_t stringOffset;
	/** The size of the UserSid member, in bytes. This value can be zero
	 *  if no security identifier was provided.
	 */
	uint32_t userSidLength;
	/** The offset of the security identifier (SID) within this
	 *  event log record.
	 */
	uint32_t userSidOffset;
	/** The size of the event-specific data (at the position indicated
	 *  by dataOffset), in bytes.
	 */
	uint32_t dataLength;
	/** The offset of the event-specific information within this event log
	 *  record, in bytes. This information could be something specific
	 *  (a disk driver might log the number of retries, for example),
	 *  followed by binary information specific to the event being logged
	 *  and to the source that generated the entry.
	 */
	uint32_t dataOffset;
/*
	uint16_t sourceName[];         Event source
	uint16_t computerName[];       Computer name
	uint8_t  userSid[];            SID of the user
	uint16_t strings[];            Message strings
	uint8_t  data[];               Event data
	uint8_t  padding[];            DWORD padding
	uint32_t length;               Length of the record
 */
}
EvtRecordHeader;


/* ===== Interface ========================================================= */

#define EVT_OK         0  /** Everything's OK. */
#define EVT_ERROR     -1  /** General error. */
#define EVT_ERROR_IO  -2  /** Input/output error. */


/* ===== Data manipulation ================================================= */

/** Record data in a more practical format. */
typedef struct
{
	/** The time at which this entry was submitted. */
	time_t timeGenerated;
	/** The time at which this entry was received by the service. */
	time_t timeWritten;

	/** The number of strings. */
	int numStrings;
	/** The strings in UTF-8. */
	char **strings;
	/** User security identifier. */
	char *userSid;
	/** Event source name in UTF-8. */
	char *sourceName;
	/** Computer name in UTF-8. */
	char *computerName;

	/** The size of event-specific data. */
	size_t dataLength;
	/** The event-specific information. */
	void *data;
}
EvtRecordContents;

/** Raw record data. */
typedef struct
{
	/** The header of the record. */
	EvtRecordHeader header;
	/** The length of the following data. */
	size_t dataLength;
	/** The following data. */
	void *data;
}
EvtRecordData;

/** Error while decoding record. */
enum EvtDecodeError
{
	/** The record is invalid. */
	EVT_DECODE_INVALID              = (1 << 0),
	/** Failed to decode the event source name. */
	EVT_DECODE_SOURCE_NAME_FAILED   = (1 << 1),
	/** Failed to decode the computer name. */
	EVT_DECODE_COMPUTER_NAME_FAILED = (1 << 2),
	/** Failed to decode event strings. */
	EVT_DECODE_STRINGS_FAILED       = (1 << 3),
	/** The SID field of the record is overflowing. */
	EVT_DECODE_SID_OVERFLOW         = (1 << 4),
	/** Failed to decode the SID field. */
	EVT_DECODE_SID_FAILED           = (1 << 5),
	/** The data field of the record is overflowing. */
	EVT_DECODE_DATA_OVERFLOW        = (1 << 6),
	/** The length field at the end of a record does not match
	 *  the length field present in the beginning of it's header.
	 */
	EVT_DECODE_LENGTH_MISMATCH      = (1 << 7)
};

/** Decode record data from the raw format. If processing of any fields fails,
 *  appropriate bits in @a errors will be set and the function will return
 *  EVT_ERROR. If the record is invalid (too short), @a errors will be set
 *  to EVT_DECODE_INVALID, @a output will be initialized with zeros
 *  and the function will return EVT_ERROR.
 *  @param[in] input  Input data in the raw format.
 *  @param[out] output  Where the result will be stored.
 *  @param[out] errors  Informs about what fields have failed. May be NULL.
 *  @return EVT_OK, EVT_ERROR
 */
int evtDecodeRecordData (const EvtRecordData *__restrict input,
	EvtRecordContents *__restrict output,
	enum EvtDecodeError *__restrict errors);

/** Error while encoding record. */
enum EvtEncodeError
{
	/** Failed to encode the event source name. */
	EVT_ENCODE_SOURCE_NAME_FAILED   = (1 << 0),
	/** Failed to encode the computer name. */
	EVT_ENCODE_COMPUTER_NAME_FAILED = (1 << 1),
	/** Failed to encode event strings. */
	EVT_ENCODE_STRINGS_FAILED       = (1 << 2),
	/** Failed to encode SID string. */
	EVT_ENCODE_SID_FAILED           = (1 << 3)
};

/** Encode record data into the raw format and build new @a output->data.
 *  @param[in] input  Input data.
 *  @param[out] output  Where the result will be stored.
 *  @param[out] errors  Informs about what fields have failed. May be NULL.
 *  @return EVT_OK, EVT_ERROR
 */
int evtEncodeRecordData (const EvtRecordContents *__restrict input,
	EvtRecordData *__restrict output,
	enum EvtEncodeError *__restrict errors);

/** Free up the memory pointed to by members of the EvtRecordContents
 *  structure and reinitialize the structure with zeros.
 *  If you don't want some members to be freed, set them to NULL first.
 */
void evtDestroyRecordContents (EvtRecordContents *input);

/** Free up the memory pointed to by the @a data member of the EvtRecordData
 *  structure. If you don't want it to be freed, set it to NULL.
 */
void evtDestroyRecordData (EvtRecordData *input);


/* ===== Low-level FileIO Interface ======================================== */

#define EVT_SEARCH_FAIL    0  /** Found nothing. */
#define EVT_SEARCH_HEADER  1  /** Found a header record. */
#define EVT_SEARCH_RECORD  2  /** Found a regular record. */

/** Searches for EVT signature and guesses the record type from the length.
 *  After finding a match, it resets the file position to the beginning
 *  of the record found.
 *  @param[in] io  A file object.
 *  @param[in] searchMax  How many bytes to search.
 *  @return EVT_SEARCH_HEADER, EVT_SEARCH_RECORD, EVT_SEARCH_FAIL, EVT_ERROR_IO
 */
int evtIOSearch (FileIO *io, off_t searchMax);

/** Basic errors within the EVT header. */
enum EvtHeaderError
{
	EVT_HEADER_ERROR_WRONG_LENGTH     = (1 << 0),
	EVT_HEADER_ERROR_WRONG_SIGNATURE  = (1 << 1),
	EVT_HEADER_ERROR_WRONG_VERSION    = (1 << 2)
};

/** Reads a header record.
 *  @param[in] io  A file object.
 *  @param[out] hdr  An output header structure.
 *  @param[out] errors  Any concrete errors for EVT_ERROR.
 *  @return EVT_OK, EVT_ERROR, EVT_ERROR_IO
 */
int evtIOReadHeader (FileIO *__restrict io, EvtHeader *__restrict hdr,
	enum EvtHeaderError *__restrict errors);


/* ===== High-level Interface ============================================== */

/* TODO: Pro funkce nahoře s enumama udělat nějakej překlad na text. */
/* TODO: Udělat dobře celej tenhle návrh. Implementovat to je pak hračka. */

/** Event log file object. */
typedef struct
{
	/** Interface for the underlying file. */
	FileIO *io;
	/** Information about the log. */
	EvtHeader header;
	/** Some changes took place. */
	unsigned changed : 1;

#if 0
	/** Information on the last EVT_ERROR. */
	char *lastError;
#endif
}
EvtLog;

/** Create a new EVT file object, use the present header.
 *  @param[out] log  The log object.
 *  @param[in] io  Underlying file.
 *  @param[out] errorInfo  Describes errors in the header if failed.
 *  @return EVT_OK, EVT_ERROR, EVT_ERROR_IO
 */
int evtOpen (EvtLog *__restrict *log, FileIO *__restrict io,
	enum EvtHeaderError *errorInfo);

/** Create a new EVT file object, initialize a new header and reset the file.
 *  @param[out] log  The log object.
 *  @param[in] io  Underlying file.
 *  @param[out] logSize  Size of the log file.
 *  @return EVT_OK, EVT_ERROR_IO
 */
int evtOpenCreate (EvtLog *__restrict *log, FileIO *__restrict io,
	uint32_t size);

/** Write anything we have to write and destroy the object.
 *  @param[in] log  A log file object.
 *  @return EVT_OK, EVT_ERROR_IO
 */
int evtClose (EvtLog *log);

/** Return the header for the log.
 *  @param[in] log  A log file object.
 */
const EvtHeader *evtGetHeader (EvtLog *log);

/** Set the position in the file to the beginning,
 *  so data in the log can be read again.
 *  @param[in] log  A log file object.
 *  @return EVT_OK, EVT_ERROR_IO
 */
int evtRewind (EvtLog *log);

/** Reading has reached the end of log. */
#define EVT_READ_EOF 1

/** Read a record and advance to the next one.
 *  @param[in] log  A log file object.
 *  @param[out] record  Data related to the record.
 *  @return EVT_READ_EOF, EVT_OK, EVT_ERROR, EVT_ERROR_IO
 */
int evtReadRecord (EvtLog *log, EvtRecordData *record);

/** There's no free place to write the record. */
#define EVT_APPEND_FULL 1

/** Set the position in the file to the end and try to write a record.
 *  @param[in] log  A log file object.
 *  @param[in] record  A record to be written.
 *  @param[in] overwrite  Try to overwrite old records if not enough space.
 *  @return EVT_APPEND_FULL, EVT_OK, EVT_ERROR_IO
 */
int evtAppendRecord (EvtLog *log, const EvtRecordData *record,
	unsigned overwrite);


#endif /* ! __EVT_H__ */
