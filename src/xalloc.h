/**
 *  @file xalloc.h
 *  @brief malloc wrapper that terminates the program on errors.
 *
 *  Copyright Přemysl Janouch 2010. All rights reserved.
 *  See the file LICENSE for licensing information.
 *
 */

#ifndef XALLOC_H_INCLUDED
#define XALLOC_H_INCLUDED

/** A function defined elsewhere that should terminate the program when
 *  Memory allocation fails. It may also print an error message.
 *  @param[in] fun  The name of function where the error has happened.
 */
extern void xalloc_die (const char *fun) ATTRIBUTE_NORETURN;

/** A malloc wrapper that terminates the program on error. */
static inline void *xmalloc (size_t size) ATTRIBUTE_MALLOC;

/** A realloc wrapper that terminates the program on error. */
static inline void *xrealloc (void *object, size_t size) ATTRIBUTE_MALLOC;


static inline void *xmalloc (size_t size)
{
	void *p;

	p = malloc(size);
	if (!p)
		xalloc_die("malloc");
	return p;
}

static inline void *xrealloc (void *object, size_t size)
{
	void *p;

	p = realloc(object, size);
	if (!p)
		xalloc_die("realloc");
	return p;
}

#endif /* ! XALLOC_H_INCLUDED */

