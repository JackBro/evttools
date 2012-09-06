/**
 *  @file sid.h
 *  @brief Conversion routines for Windows SID's.
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#ifndef __SID_H__
#define __SID_H__

/** Convert a binary SID to a string SID.
 *  @param[in] sid     A SID in the binary format.
 *  @param[in] length  The length of data pointed to by @a sid.
 *  @return The string SID on success (dynamically allocated
 *  	by malloc), NULL on failure.
 */
char *sidToString (const void *sid, size_t length);

/** Convert a string SID to a binary SID.
 *  @param[in] sid  A SID in the string format.
 *  @param[out] length  The length of the binary SID.
 *  @return The binary SID on success (dynamically allocated
 *  	by malloc), NULL on failure.
 */
void *sidToBinary (const char *restrict sid, size_t *restrict length);


#endif /* ! __SID_H__ */
