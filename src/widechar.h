/**
 *  @file widechar.h
 *  @brief Conversion between Windows WCHAR's (UTF-16LE) and UTF-8
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#ifndef __WIDECHAR_H__
#define __WIDECHAR_H__

/** Windows WCHAR (UTF-16LE) to UTF-8 conversion.
 *  @param[in]  in         A UTF-16 string.
 *  @param[in]  maxLength  Maximal length of the input string in bytes.
 *  @param[out] out        Where the pointer to the result will be saved.
 *  @return On success, the length of the input wide string in bytes,
 *  	including the NULL char. On failure the function returns 0.
 */
int decodeWideString (const uint16_t *restrict in, int maxLength,
	char **restrict out);

/** UTF-8 to Windows WCHAR (UTF-16LE) conversion.
 *  @param[in]  in   A UTF-8 string.
 *  @param[out] out  Where the pointer to the result will be saved.
 *  @return On success, the length of the output wide string in bytes,
 *  	including the NULL char. On failure the function returns 0.
 */
int encodeMBString (char *restrict in, uint16_t **restrict out);


#endif /* ! __WIDECHAR_H__ */
