/**
 *  @file base64.h
 *  @brief C source to base64 encoding and decoding algorithms implementation.
 *
 *  This is part of the libb64 project, and has been placed in the public domain.
 *  For details, see http://sourceforge.net/projects/libb64
 *
 */

#ifndef BASE64_H_INCLUDED
#define BASE64_H_INCLUDED

/** Compute the required size of a buffer for base64 decoding. */
#define BASE64_DECODED_BUFFER_SIZE(encoded) ((((encoded) >> 2) + 1) * 3)
/** Compute the required size of a buffer for base64 encoding. */
#define BASE64_ENCODED_BUFFER_SIZE(decoded) ((((decoded) / 3 + 1) << 2) + 1)


typedef enum
{
	base64_step_a,
	base64_step_b,
	base64_step_c,
	base64_step_d
}
base64_decodestep;

typedef enum
{
	base64_step_A,
	base64_step_B,
	base64_step_C
}
base64_encodestep;

typedef struct
{
	base64_decodestep step;
	char plainchar;
}
base64_decodestate;

typedef struct
{
	base64_encodestep step;
	char result;
	int stepcount;
}
base64_encodestate;


void base64_init_decodestate (base64_decodestate *state_in);

void base64_init_encodestate (base64_encodestate *state_in);

int base64_decode_value (char value_in);

char base64_encode_value (char value_in);

int base64_decode_block (const char *__restrict code_in, const int length_in,
	char *__restrict plaintext_out, base64_decodestate *__restrict state_in);

int base64_encode_block (const char *__restrict plaintext_in, int length_in,
	char *__restrict code_out, base64_encodestate *__restrict state_in);

int base64_encode_blockend
	(char *__restrict code_out, base64_encodestate *__restrict state_in);

#endif /* ! BASE64_H_INCLUDED */

