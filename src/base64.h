/**
 *  @file base64.h
 *  @brief C source to base64 encoding and decoding algorithms implementation.
 *
 *  This is part of the libb64 project,
 *  and has been placed in the public domain.
 *  For details, see http://sourceforge.net/projects/libb64
 *
 *  Somewhat modified to fit in my project.
 *
 */

#ifndef __BASE64_H__
#define __BASE64_H__

/** Compute the required size of a buffer for base64 decoding.
 *  @param[in] encoded  The length of base64 encoded data.
 *  @return The minimal size of a buffer that can hold the decoded data.
 */
#define BASE64_DECODED_BUFFER_SIZE(encoded) ((((encoded) >> 2) + 1) * 3)

/** Compute the required size of a buffer for base64 encoding.
 *  @param[in] decoded  The length of data to encode using base64.
 *  @return The minimal size of a buffer that can hold the encoded data.
 */
#define BASE64_ENCODED_BUFFER_SIZE(decoded) ((((decoded) / 3 + 1) << 2) + 1)


/** base64 decode state data. */
typedef struct
{
	/** Decoder state. */
	enum
	{
		base64_step_a,
		base64_step_b,
		base64_step_c,
		base64_step_d
	}
	step;
	/** Internal variable. */
	char plainchar;
}
base64_decodestate;

/** base64 encode state data. */
typedef struct
{
	/** Encoder state. */
	enum
	{
		base64_step_A,
		base64_step_B,
		base64_step_C
	}
	step;
	/** Internal variable. */
	char result;
}
base64_encodestate;


/** Initialize the decode state. */
void base64_init_decodestate (base64_decodestate *state_in);

/** Initialize the encode state. */
void base64_init_encodestate (base64_encodestate *state_in);

/** Decode a block of base64 encoded data. */
int base64_decode_block (const char *__restrict code_in, const int length_in,
	char *__restrict plaintext_out, base64_decodestate *__restrict state_in);

/** Encode a block of data into base64. */
int base64_encode_block (const char *__restrict plaintext_in, int length_in,
	char *__restrict code_out, base64_encodestate *__restrict state_in);

/** Finish base64 encoding. */
int base64_encode_blockend
	(char *__restrict code_out, base64_encodestate *__restrict state_in);

#endif /* ! __BASE64_H__ */

