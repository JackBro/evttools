/**
 *  @file evt.h
 *  @brief Event Log File structures
 *
 *  Based on MSDN documentation:
 *    http://msdn.microsoft.com/en-us/library/bb309026(VS.85).aspx
 *    http://msdn.microsoft.com/en-us/library/bb309024(VS.85).aspx
 *    http://msdn.microsoft.com/en-us/library/aa363646(VS.85).aspx
 *    http://msdn.microsoft.com/en-us/library/bb309022(VS.85).aspx
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#ifndef EVT_H_INCLUDED
#define EVT_H_INCLUDED

/** The signature is 0x654c664c, which is ASCII for eLfL. */
#define EVT_SIGNATURE 0x654c664c

/** Contains information that is included at the beginning of an event log.
 */
typedef struct
{
	/** The size of the header structure. The size is always 0x30. */
	uint32_t headerSize;
	/** Always the value of @a EVT_SIGNATURE.  */
	uint32_t signature;
	/** The major version number of the event log.
	 *  The major version number is always set to 1. */
	uint32_t majorVersion;
	/** The minor version number of the event log.
	 *  The minor version number is always set to 1. */
	uint32_t minorVersion;
	/** The offset to the oldest record in the event log. */
	uint32_t startOffset;
	/** The offset to the EvtEOF in the event log. */
	uint32_t endOffset;
	/** The number of the next record that will be added to the event log. */
	uint32_t currentRecordNumber;
	/** The number of the oldest record in the event log.
	 *  For an empty file, the oldest record number is set to 0. */
	uint32_t oldestRecordNumber;
	/** The maximum size, in bytes, of the event log. The maximum size
	 *  is defined when the event log is created. The event-logging service
	 *  does not typically update this value, it relies on the registry
	 *  configuration. The reader of the event log can use normal file APIs
	 *  to determine the size of the file. */
	uint32_t maxSize;
	/** The status of the event log. See the #defines below. */
	uint32_t flags;
	/** The retention value of the file when it is created. The event
	 *  logging service does not typically update this value, it relies
	 *  on the registry configuration. */
	uint32_t retention;
	/** The ending size of the header structure. The size is always 0x30. */
	uint32_t endHeaderSize;
}
ATTRIBUTE_PACKED EvtHeader;

/** Indicates that records have been written to an event log,
 *  but the event log file has not been properly closed. */
#define EVT_HEADER_DIRTY 0x0001
/** Indicates that records in the event log have wrapped. */
#define EVT_HEADER_WRAP 0x0002
/** Indicates that the most recent write attempt failed
 *  due to insufficient space. */
#define EVT_LOGFULL_WRITTEN 0x0004
/** Indicates that the archive attribute has been set for the file. */
#define EVT_ARCHIVE_SET 0x0008

/** Contains information about an event record. */
typedef struct
{
	/** The size of this event record, in bytes. Note that this value
	 *  is stored at both ends of the entry to ease moving forward
	 *  or backward through the log. The length includes any pad bytes
	 *  inserted at the end of the record. */
	uint32_t length;
	/** A value that is always set to EVT_SIGNATURE
	 *  (the value is 0x654c664c), which is ASCII for eLfL. */
	uint32_t reserved;
	/** The number of the record. */
	uint32_t recordNumber;
	/** The time at which this entry was submitted. This time is measured
	 *  in the number of seconds elapsed since 00:00:00 January 1, 1970,
	 *  Universal Coordinated Time. */
	uint32_t timeGenerated;
	/** The time at which this entry was received by the service
	 *  to be written to the log. This time is measured in the number
	 *  of seconds elapsed since 00:00:00 January 1, 1970,
	 *  Universal Coordinated Time. */
	uint32_t timeWritten;
	/** The event identifier. The value is specific to the event source
	 *  for the event, and is used with source name to locate a description
	 *  string in the message file for the event source. */
	uint32_t eventID;
	/** The type of event.
	 *
	 *  @see EvtEventType
	 */
	uint16_t eventType;
	/** The number of strings present in the log (at the position indicated
	 *  by @a stringOffset). These strings are merged into the message
	 *  before it is displayed to the user. */
	uint16_t numStrings;
	/** The category for this event. The meaning of this value depends
	 *  on the event source. */
	uint16_t eventCategory;
	/** Reserved. */
	uint16_t reservedFlags;
	/** Reserved. */
	uint32_t closingRecordNumber;
	/** The offset of the description strings within this event log record. */
	uint32_t stringOffset;
	/** The size of the UserSid member, in bytes. This value can be zero
	 *  if no security identifier was provided. */
	uint32_t userSidLength;
	/** The offset of the security identifier (SID) within this
	 *  event log record. */
	uint32_t userSidOffset;
	/** The size of the event-specific data (at the position indicated
	 *  by dataOffset), in bytes. */
	uint32_t dataLength;
	/** The offset of the event-specific information within this event log
	 *  record, in bytes. This information could be something specific
	 *  (a disk driver might log the number of retries, for example),
	 *  followed by binary information specific to the event being logged
	 *  and to the source that generated the entry. */
	uint32_t dataOffset;
/*
	uint16_t sourceName[];
	uint16_t computername[];
	uint8_t  userSid[];
	uint16_t strings[];
	uint8_t  data[];
	uint8_t  padding[];
	uint32_t length;
 */
}
ATTRIBUTE_PACKED EvtRecord;

/** The type of event. */
typedef enum
{
	/** Error event. */
	EVT_ERROR_TYPE = 0x0001,
	/** Failure Audit event. */
	EVT_AUDIT_FAILURE = 0x0010,
	/** Success Audit event. */
	EVT_AUDIT_SUCCESS = 0x0008,
	/** Information event. */
	EVT_INFORMATION_TYPE = 0x0004,
	/** Warning event. */
	EVT_WARNING_TYPE = 0x0002
}
EvtEventType;

/** Contains information that is included immediately after the newest
 *  event log record. */
typedef struct
{
	/** The beginning size of the EvtEOF.
	 *  The beginning size is always 0x28. */
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
	/** The ending size of the EvtEOF.
	 *  The ending size is always 0x28. */
	uint32_t recordSizeEnd;
}
ATTRIBUTE_PACKED EvtEOF;

#endif /* ! EVT_H_INCLUDED */

