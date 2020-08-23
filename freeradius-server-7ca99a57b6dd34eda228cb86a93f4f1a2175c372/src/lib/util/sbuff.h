#pragma once
/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/** A generic buffer structure for string printing and parsing strings
 *
 * Because doing manual length checks is error prone and a waste of everyones time.
 *
 * @file src/lib/util/sbuff.h
 *
 * @copyright 2020 Arran Cudbard-Bell <a.cudbardb@freeradius.org>
 */
RCSIDH(sbuff_h, "$Id$")

#  ifdef __cplusplus
extern "C" {
#  endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <talloc.h>

#include <freeradius-devel/util/strerror.h>
#include <freeradius-devel/util/table.h>

typedef struct fr_sbuff_s fr_sbuff_t;
typedef struct fr_sbuff_ptr_s fr_sbuff_marker_t;

typedef size_t(*fr_sbuff_extend_t)(fr_sbuff_t *sbuff, size_t req_extenison);

struct fr_sbuff_ptr_s {
	union {
		char const *p_i;				//!< Immutable position pointer.
		char *p;					//!< Mutable position pointer.
	};
	fr_sbuff_marker_t	*next;			//!< Next m in the list.
	fr_sbuff_t		*parent;		//!< Owner of the marker
};

struct fr_sbuff_s {
	union {
		char const *buff_i;				//!< Immutable buffer pointer.
		char *buff;					//!< Mutable buffer pointer.
	};

	union {
		char const *start_i;				//!< Immutable start pointer.
		char *start;					//!< Mutable start pointer.
	};

	union {
		char const *end_i;				//!< Immutable end pointer.
		char *end;					//!< Mutable end pointer.
	};

	union {
		char const *p_i;				//!< Immutable position pointer.
		char *p;					//!< Mutable position pointer.
	};

	uint8_t			is_const:1;		//!< Can't be modified.
	uint8_t			adv_parent:1;		//!< If true, advance the parent.

	size_t			shifted;		//!< How many bytes this sbuff has been
							///< shifted since its creation.

	fr_sbuff_extend_t	extend;			//!< Function to re-populate or extend
							///< the buffer.
	void			*uctx;			//!< Extend uctx data.

	fr_sbuff_t		*parent;		//!< sbuff this sbuff was copied from.

	fr_sbuff_marker_t	*m;			//!< Pointers to update if the underlying
							///< buffer changes.
};

/** Talloc sbuff extension structure
 *
 * Holds the data necessary for creating dynamically
 * extensible buffers.
 */
typedef struct {
	TALLOC_CTX		*ctx;			//!< Context to alloc new buffers in.
	size_t			init;			//!< How much to allocate initially.
	size_t			max;			//!< Maximum size of the buffer.
} fr_sbuff_uctx_talloc_t;

/** File sbuff extension structure
 *
 * Holds the data necessary for creating dynamically
 * extensible file buffers.
 */
typedef struct {
	FILE			*file;			//!< FILE * we're reading from.
	char			*buff_end;		//!< The true end of the buffer.
	size_t			max;			//!< Maximum number of bytes to read.
} fr_sbuff_uctx_file_t;

/** Terminal element with pre-calculated lengths
 *
 */
typedef struct {
	char const		*str;			//!< Terminal string
	size_t			len;			//!< Length of string
} fr_sbuff_term_elem_t;

/** Set of terminal elements
 *
 */
typedef struct {
	size_t			len;			//!< Length of the list.
	fr_sbuff_term_elem_t	*elem;			//!< A sorted list of terminal strings.
} fr_sbuff_term_t;

/** Initialise a terminal structure with a single string
 *
 * @param[in] _str	terminal string.
 */
#define FR_SBUFF_TERM(_str) \
(fr_sbuff_term_t){ \
	.len = 1, \
	.elem = (fr_sbuff_term_elem_t[]){ L(_str) }, \
}

/** Initialise a terminal structure with a list of sorted strings
 *
 * Strings must be lexicographically sorted.
 *
 * @param[in] ...	Lexicographically sorted list of terminal strings.
 */
#define FR_SBUFF_TERMS(...) \
(fr_sbuff_term_t){ \
	.len = (sizeof((fr_sbuff_term_elem_t[]){ __VA_ARGS__ }) / sizeof(fr_sbuff_term_elem_t)), \
	.elem = (fr_sbuff_term_elem_t[]){ __VA_ARGS__ }, \
}

/** Set of parsing rules for *unescape_until functions
 *
 */
typedef struct {
	char		chr;				//!< Character at the start of an escape sequence.
	char const	subs[UINT8_MAX + 1];		//!< Special characters and their substitutions.
	bool		skip[UINT8_MAX + 1];		//!< Characters that are escaped, but left in the
							///< output along with the escape character.
							///< This is useful where we need to interpret escape
							///< sequences for parsing, but where the string will
							///< be passed off to a 3rd party library which will
							///< need to interpret the same sequences.
	bool		do_hex;				//!< Process hex sequences i.e. \x<hex><hex>.
	bool		do_oct;				//!< Process oct sequences i.e. \<oct><oct><oct>.
} fr_sbuff_escape_rules_t;

/** A set of terminal sequences, and escape rules
 *
 */
typedef struct {
	fr_sbuff_escape_rules_t const	*escapes;	//!< Escape characters

	fr_sbuff_term_t const		*terminals;	//!< Terminal characters used as a hint
							///< that a token is not complete.
} fr_sbuff_parse_rules_t;

typedef enum {
	FR_SBUFF_PARSE_OK			= 0,		//!< No error.
	FR_SBUFF_PARSE_ERROR_NOT_FOUND		= -1,		//!< String does not contain a token
								///< matching the output type.
	FR_SBUFF_PARSE_ERROR_TRAILING		= -2,		//!< Trailing characters found.
	FR_SBUFF_PARSE_ERROR_NUM_OVERFLOW	= -3,		//!< Integer type would overflow.
	FR_SBUFF_PARSE_ERROR_NUM_UNDERFLOW	= -4		//!< Integer type would underflow.
} fr_sbuff_parse_error_t;

extern fr_table_num_ordered_t const sbuff_parse_error_table[];
extern size_t sbuff_parse_error_table_len;

extern bool const sbuff_char_class_uint[UINT8_MAX + 1];
extern bool const sbuff_char_class_int[UINT8_MAX + 1];
extern bool const sbuff_char_class_float[UINT8_MAX + 1];
extern bool const sbuff_char_class_hex[UINT8_MAX + 1];
extern bool const sbuff_char_alpha_num[UINT8_MAX + 1];

/** Matches a-z,A-Z
 */
#define SBUFF_CHAR_CLASS_ALPHA \
	['a'] = true, ['b'] = true, ['c'] = true, ['d'] = true, ['e'] = true, \
	['f'] = true, ['g'] = true, ['h'] = true, ['i'] = true, ['j'] = true, \
	['k'] = true, ['l'] = true, ['m'] = true, ['n'] = true, ['o'] = true, \
	['p'] = true, ['q'] = true, ['r'] = true, ['s'] = true, ['t'] = true, \
	['u'] = true, ['v'] = true, ['w'] = true, ['x'] = true, ['y'] = true, \
	['z'] = true, \
	['A'] = true, ['B'] = true, ['C'] = true, ['D'] = true,	['E'] = true, \
	['F'] = true, ['G'] = true, ['H'] = true, ['I'] = true,	['J'] = true, \
	['K'] = true, ['L'] = true, ['M'] = true, ['N'] = true,	['O'] = true, \
	['P'] = true, ['Q'] = true, ['R'] = true, ['S'] = true,	['T'] = true, \
	['U'] = true, ['V'] = true, ['W'] = true, ['X'] = true,	['Y'] = true, \
	['Z'] = true

/** Matches 0-9
 */
#define SBUFF_CHAR_CLASS_NUM \
	['0'] = true, ['1'] = true, ['2'] = true, ['3'] = true, ['4'] = true, \
	['5'] = true, ['6'] = true, ['7'] = true, ['8'] = true, ['9'] = true

/** Matches 0-9,a-z,A-Z
 */
#define SBUFF_CHAR_CLASS_ALPHA_NUM \
	SBUFF_CHAR_CLASS_ALPHA, \
	SBUFF_CHAR_CLASS_NUM

/** Matches 0-9,a-f,A-F
 */
#define SBUFF_CHAR_CLASS_HEX \
	SBUFF_CHAR_CLASS_NUM, \
	['a'] = true, ['b'] = true, ['c'] = true, ['d'] = true, ['e'] = true, \
	['f'] = true, \
	['A'] = true, ['B'] = true, ['C'] = true, ['D'] = true,	['E'] = true, \
	['F'] = true


/** Generic wrapper macro to return if there's insufficient memory to satisfy the request on the sbuff
 *
 */
#define FR_SBUFF_RETURN(_func, _sbuff, ...) \
do { \
	ssize_t _slen; \
	_slen = _func(_sbuff, ## __VA_ARGS__ ); \
	if (_slen < 0) return _slen; \
} while (0)

/** @name Ephemeral copying macros
 * @{
 */

/** Prevent an sbuff being advanced as it's passed into a printing or parsing function
 *
 * @param[in] _sbuff	to make an ephemeral copy of.
 */
#define FR_SBUFF_NO_ADVANCE(_sbuff) \
((fr_sbuff_t){ \
	.buff		= (_sbuff)->buff, \
	.start		= (_sbuff)->p, \
	.end		= (_sbuff)->end, \
	.p		= (_sbuff)->p, \
	.is_const	= (_sbuff)->is_const, \
	.extend		= (_sbuff)->extend, \
	.uctx		= (_sbuff)->uctx, \
	.parent		= (_sbuff) \
})

/** Copy all fields in an sbuff
 *
 * @param[in] _sbuff	to make an ephemeral copy of.
 */
#define FR_SBUFF_COPY(_sbuff) \
((fr_sbuff_t){ \
	.buff		= (_sbuff)->buff, \
	.start		= (_sbuff)->p, \
	.end		= (_sbuff)->end, \
	.p		= (_sbuff)->p, \
	.is_const	= (_sbuff)->is_const, \
	.adv_parent	= 1, \
	.extend		= (_sbuff)->extend, \
	.uctx		= (_sbuff)->uctx, \
	.parent		= (_sbuff) \
})

/** Creates a compound literal to pass into functions which accept a sbuff
 *
 * @note The return value of the function should be used to determine how much
 *	 data was written to the buffer.
 *
 * @param[in] _start		of the buffer.
 * @param[in] _len_or_end	Length of the buffer or the end pointer.
 */
#define FR_SBUFF_OUT(_start, _len_or_end) \
((fr_sbuff_t){ \
	.buff_i		= _start, \
	.start_i	= _start, \
	.end_i		= _Generic((_len_or_end), \
				size_t		: (char const *)(_start) + ((size_t)(_len_or_end) - 1), \
				long		: (char const *)(_start) + ((size_t)(_len_or_end) - 1), \
				int		: (char const *)(_start) + ((size_t)(_len_or_end) - 1), \
				unsigned int	: (char const *)(_start) + ((size_t)(_len_or_end) - 1), \
				char *		: (char const *)(_len_or_end), \
				char const *	: (char const *)(_len_or_end) \
			), \
	.p_i		= _start, \
	.is_const	= _Generic((_start), \
				char *		: false, \
				char const *	: true \
	       		) \
})

/** Creates a compound literal to pass into functions which accept a sbuff
 *
 * @note The return value of the function should be used to determine how much
 *	 data was written to the buffer.
 *
 * @param[in] _start		of the buffer.
 * @param[in] _len_or_end	Length of the buffer or the end pointer.
 */
#define FR_SBUFF_IN(_start, _len_or_end) \
((fr_sbuff_t){ \
	.buff_i		= _start, \
	.start_i	= _start, \
	.end_i		= _Generic((_len_or_end), \
				size_t		: (char const *)(_start) + (size_t)(_len_or_end), \
				long		: (char const *)(_start) + (size_t)(_len_or_end), \
				int		: (char const *)(_start) + (size_t)(_len_or_end), \
				unsigned int	: (char const *)(_start) + (size_t)(_len_or_end), \
				char *		: (char const *)(_len_or_end), \
				char const *	: (char const *)(_len_or_end) \
			), \
	.p_i		= _start, \
	.is_const	= _Generic((_start), \
				char *		: false, \
				char const *	: true \
	       		) \
})


void	fr_sbuff_update(fr_sbuff_t *sbuff, char *new_buff, size_t new_len);

size_t	fr_sbuff_shift(fr_sbuff_t *sbuff, size_t shift);

size_t	fr_sbuff_extend_file(fr_sbuff_t *sbuff, size_t extension);

size_t	fr_sbuff_extend_talloc(fr_sbuff_t *sbuff, size_t extenison);

int	fr_sbuff_trim_talloc(fr_sbuff_t *sbuff, size_t len);

static inline void fr_sbuff_terminate(fr_sbuff_t *sbuff)
{
	*sbuff->p = '\0';
}

static inline void _fr_sbuff_init(fr_sbuff_t *out, char const *start, char const *end, bool is_const)
{
	if (unlikely(end < start)) end = start;	/* Could be an assert? */

	*out = (fr_sbuff_t){
		.buff_i = start,
		.start_i = start,
		.p_i = start,
		.end_i = end,
		.is_const = is_const
	};
}

/** Initialise an sbuff around a stack allocated buffer for printing or parsing
 *
 * @param[out] _out		Pointer to buffer.
 * @param[in] _start		Start of the buffer.
 * @param[in] _len_or_end	Either an end pointer or the length
 *				of the buffer.
 */
#define fr_sbuff_init(_out, _start, _len_or_end) \
_Generic((_len_or_end), \
	size_t		: _fr_sbuff_init(_out, _start, (char const *)(_start) + ((size_t)(_len_or_end) - 1), true), \
	long		: _fr_sbuff_init(_out, _start, (char const *)(_start) + ((size_t)(_len_or_end) - 1), true), \
	int		: _fr_sbuff_init(_out, _start, (char const *)(_start) + ((size_t)(_len_or_end) - 1), true), \
	char *		: _fr_sbuff_init(_out, _start, (char const *)(_len_or_end), false), \
	char const *	: _fr_sbuff_init(_out, _start, (char const *)(_len_or_end), true) \
)

/** Initialise a special sbuff which automatically reads in more data as the buffer is exhausted
 *
 * @param[out] sbuff	to initialise.
 * @param[out] fctx	to initialise.  Must have a lifetime >= to the sbuff.
 * @param[in] buff	Temporary buffer to use for storing file contents.
 * @param[in] len	Length of the temporary buffer.
 * @param[in] max	The maximum length of data to read from the file.
 * @return
 *	- The passed sbuff on success.
 *	- NULL on failure.
 */
static inline fr_sbuff_t *fr_sbuff_init_file(fr_sbuff_t *sbuff, fr_sbuff_uctx_file_t *fctx,
					     char *buff, size_t len, FILE *file, size_t max)
{
	*fctx = (fr_sbuff_uctx_file_t){
		.file = file,
		.max = max,
		.buff_end = buff + len		//!< Store the real end
	};

	*sbuff = (fr_sbuff_t){
		.buff = buff,
		.start = buff,
		.p = buff,
		.end = buff,			//!< Starts with 0 bytes available
		.extend = fr_sbuff_extend_file,
		.uctx = fctx
	};

	return sbuff;
}

/** Initialise a special sbuff which automatically extends as additional data is written
 *
 * @param[in] ctx	to allocate buffer in.
 * @param[out] sbuff	to initialise.
 * @param[out] tctx	to initialise.  Must have a lifetime >= to the sbuff.
 * @param[in] init	The length of the initial buffer.
 * @param[in] max	The maximum length of the buffer.
 * @return
 *	- The passed sbuff on success.
 *	- NULL on failure.
 */
static inline fr_sbuff_t *fr_sbuff_init_talloc(TALLOC_CTX *ctx,
					       fr_sbuff_t *sbuff, fr_sbuff_uctx_talloc_t *tctx,
					       size_t init, size_t max)
{
	char *buff;

	*tctx = (fr_sbuff_uctx_talloc_t){
		.ctx = ctx,
		.init = init,
		.max = max
	};

	/*
	 *	Allocate the initial buffer
	 *
	 *	We always allocate a buffer so we don't
	 *	trigger ubsan errors by performing
	 *	arithmetic on NULL pointers.
	 */
	buff = talloc_zero_array(ctx, char, init + 1);
	if (!buff) {
		fr_strerror_printf("Failed allocating buffer of %zu bytes", init + 1);
		memset(sbuff, 0, sizeof(*sbuff));	/* clang scan */
		return NULL;
	}

	*sbuff = (fr_sbuff_t){
		.buff = buff,
		.start = buff,
		.p = buff,
		.end = buff + init,
		.extend = fr_sbuff_extend_talloc,
		.uctx = tctx
	};

	return sbuff;
}
/** @} */

/** @name Accessors
 *
 * Caching the values of these pointers or the pointer values from the sbuff
 * directly is strongly discouraged as they can become invalidated during
 * stream parsing or when printing to an auto-expanding buffer.
 *
 * These functions should only be used to pass sbuff pointers into 3rd party
 * APIs.
 */

/** Return a pointer to the start of the underlying buffer in an sbuff or one of its markers
 *
 * @param[in] _sbuff_or_marker	to return the buffer for.
 * @return A pointer to the start of the buffer.
 */
#define fr_sbuff_buff(_sbuff_or_marker) \
	_Generic((_sbuff_or_marker), \
		 fr_sbuff_t *			: ((fr_sbuff_t const *)(_sbuff_or_marker))->buff, \
		 fr_sbuff_t const *		: ((fr_sbuff_t const *)(_sbuff_or_marker))->buff, \
		 fr_sbuff_marker_t *		: ((fr_sbuff_marker_t const *)(_sbuff_or_marker))->parent->buff, \
		 fr_sbuff_marker_t const *	: ((fr_sbuff_marker_t const *)(_sbuff_or_marker))->parent->buff \
	)

/** Return a pointer to the 'start' position of an sbuff or one of its markers
 *
 * The start position is not necessarily the start of the buffer, and is
 * advanced every time an sbuff is copied.
 *
 * @param[in] _sbuff_or_marker	to return the start position of.
 * @return A pointer to the start position of the buffer.
 */
#define fr_sbuff_start(_sbuff_or_marker) \
	(_Generic((_sbuff_or_marker), \
		  fr_sbuff_t *			: ((fr_sbuff_t const *)(_sbuff_or_marker))->start, \
		  fr_sbuff_t const *		: ((fr_sbuff_t const *)(_sbuff_or_marker))->start, \
		  fr_sbuff_marker_t *		: ((fr_sbuff_marker_t const *)(_sbuff_or_marker))->parent->start, \
		  fr_sbuff_marker_t const *	: ((fr_sbuff_marker_t const *)(_sbuff_or_marker))->parent->start \
	))

/** Return a pointer to the 'current' position of an sbuff or one of its markers
 *
 * @param[in] _sbuff_or_marker	to return the current position of.
 * @return A pointer to the current position of the buffer or marker.
 */
#define fr_sbuff_current(_sbuff_or_marker) \
	(_Generic((_sbuff_or_marker), \
		  fr_sbuff_t *			: ((fr_sbuff_t const *)(_sbuff_or_marker))->p, \
		  fr_sbuff_t const *		: ((fr_sbuff_t const *)(_sbuff_or_marker))->p, \
		  fr_sbuff_marker_t *		: ((fr_sbuff_marker_t const *)(_sbuff_or_marker))->p, \
		  fr_sbuff_marker_t const *	: ((fr_sbuff_marker_t const *)(_sbuff_or_marker))->p \
	))

/** Return a pointer to the 'end' position of an sbuff or one of its markers
 *
 * @param[in] _sbuff_or_marker	to return the end position of.
 * @return A pointer to the end position of the buffer or marker.
 */
#define fr_sbuff_end(_sbuff_or_marker) \
	(_Generic((_sbuff_or_marker), \
		  fr_sbuff_t *			: ((fr_sbuff_t const *)(_sbuff_or_marker))->end, \
		  fr_sbuff_t const *		: ((fr_sbuff_t const *)(_sbuff_or_marker))->end, \
		  fr_sbuff_marker_t *		: ((fr_sbuff_marker_t const *)(_sbuff_or_marker))->parent->end, \
		  fr_sbuff_marker_t const *	: ((fr_sbuff_marker_t const *)(_sbuff_or_marker))->parent->end \
	))

/** Return the value of the shifted field
 *
 * @param[in] _sbuff_or_marker	to return the position of.
 * @return the number of bytes the buffer has been shifted.
 */
#define fr_sbuff_shifted(_sbuff_or_marker) \
	(_Generic((_sbuff_or_marker), \
		  fr_sbuff_t *			: ((fr_sbuff_t const *)(_sbuff_or_marker))->shifted, \
		  fr_sbuff_t const *		: ((fr_sbuff_t const *)(_sbuff_or_marker))->shifted, \
		  fr_sbuff_marker_t *		: ((fr_sbuff_marker_t const *)(_sbuff_or_marker))->parent->shifted, \
		  fr_sbuff_marker_t const *	: ((fr_sbuff_marker_t const *)(_sbuff_or_marker))->parent->shifted \
	))
/** @} */

/** @name Length calculations
 * @{
 */

/** Return the difference in position between the two sbuffs or markers
 *
 * @param[in] _a	The first sbuff or marker.
 * @param[in] _b	The second sbuff or marker.
 * @return
 *	- >0 the number of bytes _a is ahead of _b.
 *	- 0 _a and _b are the same position.
 *	- <0 the number of bytes _a is behind of _b.
 */
#define fr_sbuff_diff(_a, _b) \
	((ssize_t)(fr_sbuff_current(_a) - fr_sbuff_current(_b)))

/** Return the number of bytes remaining between the sbuff or marker and the end of the buffer
 *
 * @note Do not use this in functions that may be used for stream parsing
 *	 unless you're sure you know what you're doing.
 *	 The value return does not reflect the number of bytes that may
 *	 be potentially read from the stream, only the number of bytes
 *	 until the end of the current chunk.
 *
 * @param[in] _sbuff_or_marker	to return the number of bytes remaining for.
 * @return
 *	- >0 the number of bytes remaining before we reach the end of the buffer.
 *	- -0 we're at the end of the buffer.
 */
#define fr_sbuff_remaining(_sbuff_or_marker) \
	((size_t)(fr_sbuff_end(_sbuff_or_marker) < fr_sbuff_current(_sbuff_or_marker) ? \
		0 : (fr_sbuff_end(_sbuff_or_marker) - fr_sbuff_current(_sbuff_or_marker))))

/** Return the number of bytes remaining between the start of the sbuff or marker and the current position
 *
 * @param[in] _sbuff_or_marker	to return the number of bytes used for.
 * @return
 *	- >0 the number of bytes the current position has advanced past the start.
 *	- -0 the current position is at the start of the buffer.
 */
#define fr_sbuff_used(_sbuff_or_marker) \
	((size_t)(fr_sbuff_start(_sbuff_or_marker) > fr_sbuff_current(_sbuff_or_marker) ? \
		0 : (fr_sbuff_current(_sbuff_or_marker) - fr_sbuff_start(_sbuff_or_marker))))

/** Like fr_sbuff_used, but adjusts for the value returned for the amount shifted
 *
 * @param[in] _sbuff_or_marker	to return the number of bytes used for.
 * @return
 *	- >0 the number of bytes the current position has advanced past the start +
 *	     the amount the buffer has shifted.
 *	- -0 the current position is at the start of the buffer (and hasn't been shifted).
 */
#define fr_sbuff_used_total(_sbuff_or_marker) \
	((size_t)((fr_sbuff_current(_sbuff_or_marker) + fr_sbuff_shifted(_sbuff_or_marker)) - fr_sbuff_start(_sbuff_or_marker)))

/** The length of the underlying buffer
 *
 * @param[in] _sbuff_or_marker	to return the length of.
 * @return The length of the underlying buffer (minus 1 byte for \0).
 */
#define fr_sbuff_len(_sbuff_or_marker) \
	((size_t)(fr_sbuff_end(_sbuff_or_marker) - fr_sbuff_buff(_sbuff_or_marker)))

/** How many the sbuff or marker is behind its parent
 *
 * @param[in] _sbuff_or_marker
 * @return
 *	- 0 the sbuff or marker is ahead of its parent.
 *	- >0 the number of bytes the marker is behind its parent.
 */
#define fr_sbuff_behind(_sbuff_or_marker) \
	(fr_sbuff_current(_sbuff_or_marker) > fr_sbuff_current((_sbuff_or_marker)->parent) ? \
		0 : fr_sbuff_current((_sbuff_or_marker)->parent) - fr_sbuff_current(_sbuff_or_marker))

/** How many the sbuff or marker is ahead of its parent
 *
 * @return
 *	- 0 the sbuff or marker is behind its parent.
 *	- >0 the number of bytes the marker is ahead of its parent.
 */
#define fr_sbuff_ahead(_sbuff_or_marker) \
	(fr_sbuff_current((_sbuff_or_marker)->parent) > fr_sbuff_current(_sbuff_or_marker) ? \
		0 : fr_sbuff_current(_sbuff_or_marker) - fr_sbuff_current((_sbuff_or_marker)->parent))

/** Return the current position in the sbuff as a negative offset
 *
 */
#define FR_SBUFF_ERROR_RETURN(_sbuff) return -(fr_sbuff_used(_sbuff))

/** Return the current position in the sbuff as a negative offset
 *
 */
#define FR_SBUFF_MARKER_ERROR_RETURN(_marker) return -(fr_sbuff_used(_marker))

/** Return the current adjusted position in the sbuff as a negative offset
 *
 */
#define FR_SBUFF_ERROR_RETURN_ADJ(_sbuff, _slen) return -((fr_sbuff_used(_sbuff) + ((_slen) * -1)))

/** Check if _len bytes are available in the sbuff, and if not return the number of bytes we'd need
 *
 */
#define FR_SBUFF_CHECK_REMAINING_RETURN(_sbuff, _len) \
do { \
	if ((_len) > fr_sbuff_remaining(_sbuff)) { \
		return -((_len) - fr_sbuff_remaining(_sbuff));	\
	}\
} while (0)

/** Check if _len bytes are available in the sbuff and extend the buffer if possible
 *
 */
#define FR_SBUFF_EXTEND_OR_RETURN(_sbuff, _len) \
do { \
	if (fr_sbuff_remaining(_sbuff) < (_len)) { \
		if (!(_sbuff)->extend || ((_sbuff)->extend(_sbuff, _len) < _len)) { \
			return -(((_sbuff)->p + (_len)) - ((_sbuff)->end)); \
		} \
	} \
} while (0)

/** Extend a buffer if we're below the low water mark
 *
 * @param[in] _sbuff	to extend.
 * @param[in] _lowat	If bytes remaining are below the amount, extend.
 */
#define FR_SBUFF_CANT_EXTEND_LOWAT(_sbuff, _lowat) \
((fr_sbuff_remaining(_sbuff) < (_lowat)) && (!(_sbuff)->extend || !(_sbuff)->extend(_sbuff, (_lowat) - fr_sbuff_remaining(_sbuff))))

/** Extend a buffer if no space remains
 *
 * @param[in] _sbuff	to extend.
 */
#define FR_SBUFF_CANT_EXTEND(_sbuff) FR_SBUFF_CANT_EXTEND_LOWAT(_sbuff, 1)

/** @} */

/** @name Position modification (recursive)
 *
 * Change the current position of pointers in the sbuff and their children.
 * @{
 */

/** Update the position of p in a list of sbuffs
 *
 * @note Do not call directly.
 */
static inline void _fr_sbuff_set_recurse(fr_sbuff_t *sbuff, char const *p)
{
	sbuff->p_i = p;
	if (sbuff->adv_parent && sbuff->parent) _fr_sbuff_set_recurse(sbuff->parent, p);
}

static inline ssize_t _fr_sbuff_marker_set(fr_sbuff_marker_t *m, char const *p)
{
	fr_sbuff_t 	*sbuff = m->parent;
	char		*current = m->p;

	if (unlikely(p > sbuff->end)) return -(p - sbuff->end);
	if (unlikely(p < sbuff->start)) return 0;

	m->p_i = p;

	return p - current;
}

static inline ssize_t _fr_sbuff_set(fr_sbuff_t *sbuff, char const *p)
{
	char const *c;

	if (unlikely(p > sbuff->end)) return -(p - sbuff->end);
	if (unlikely(p < sbuff->start)) return 0;
	if (p == sbuff->p) return 0;

	c = sbuff->p;
	_fr_sbuff_set_recurse(sbuff, p);

	return p - c;
}

/** Set the position in a sbuff using another sbuff, a char pointer, or a length
 *
 * @param[in] _dst	sbuff or marker to set the position for.
 * @param[in] _src	Variable to glean new position from.  Behaviour here
 *			depends on the type of the variable.
 *			- sbuff, the current position of the sbuff.
 *			- marker, the current position of the marker.
 *			- pointer, the position of the pointer.
 *			- size_t, _dst->start + _src.
 * @return
 *	- 0	not advanced.
 *	- >0	the number of bytes the sbuff was advanced by.
 *	- <0	the number of bytes required to complete the advancement
 */
#define fr_sbuff_set(_dst, _src) \
_Generic((_dst), \
	 fr_sbuff_t *			: _fr_sbuff_set, \
	 fr_sbuff_marker_t *		: _fr_sbuff_marker_set \
)(_dst, \
_Generic((_src), \
	fr_sbuff_t *			: fr_sbuff_current((fr_sbuff_t const *)(_src)), \
	fr_sbuff_t const *		: fr_sbuff_current((fr_sbuff_t const *)(_src)), \
	fr_sbuff_marker_t *		: fr_sbuff_current((fr_sbuff_marker_t const *)(_src)), \
	fr_sbuff_marker_t const *	: fr_sbuff_current((fr_sbuff_marker_t const *)(_src)), \
	char const *			: (char const *)(_src), \
	char *				: (char const *)(_src), \
	size_t				: (fr_sbuff_start(_dst) + (uintptr_t)(_src)) \
))

/** Advance position in sbuff by N bytes
 *
 * @param[in] sbuff	to advance.
 * @param[in] n		How much to advance sbuff by.
 * @return
 *	- 0	not advanced.
 *	- >0	the number of bytes the sbuff was advanced by.
 *	- <0	the number of bytes required to complete the advancement
 */
#define fr_sbuff_advance(_sbuff_or_marker, _n)  fr_sbuff_set(_sbuff_or_marker, (fr_sbuff_current(_sbuff_or_marker) + (_n)))
#define FR_SBUFF_ADVANCE_RETURN(_sbuff, _n) FR_SBUFF_RETURN(fr_sbuff_advance, _sbuff, _n)

/** Reset the current position of the sbuff to the start of the string
 *
 */
static inline void fr_sbuff_set_to_start(fr_sbuff_t *sbuff)
{
	_fr_sbuff_set_recurse(sbuff, sbuff->start);
}

/** Reset the current position of the sbuff to the end of the string
 *
 */
static inline void fr_sbuff_set_to_end(fr_sbuff_t *sbuff)
{
	_fr_sbuff_set_recurse(sbuff, sbuff->end);
}
/** @} */

/** @name Add a marker to an sbuff
 *
 * Markers are used to indicate an area of the code is working at a particular
 * point in a string buffer.
 *
 * If the sbuff is performing stream parsing, then markers are used to update
 * any pointers to the buffer, as the data in the buffer is shifted to make
 * room for new data from the stream.
 *
 * If the sbuff is being used to create strings, then the markers are updated
 * if the buffer is re-allocated.
 * @{
 */

/** Adds a new pointer to the beginning of the list of pointers to update
 *
 * @param[out] m	to initialise.
 * @param[in] sbuff	to associate marker with.
 * @return The position the marker was set to.
 */
static inline char *fr_sbuff_marker(fr_sbuff_marker_t *m, fr_sbuff_t *sbuff)
{
	*m = (fr_sbuff_marker_t){
		.next = sbuff->m,	/* Link into the head */
		.p = sbuff->p,		/* Set the current position in the sbuff */
		.parent = sbuff		/* Record which sbuff this marker was associated with */
	};
	sbuff->m = m;

	return sbuff->p;
}

/** Trims the linked list back to the specified pointer
 *
 * Pointers should be released in the inverse order to allocation.
 *
 * Alternatively the oldest pointer can be released, resulting in any newer pointer
 * also being removed from the list.
 *
 * @param[in] m		to release.
 */
static inline void fr_sbuff_marker_release(fr_sbuff_marker_t *m)
{
	m->parent->m = m->next;

#ifndef NDEBUF
	memset(m, 0, sizeof(*m));	/* Use after release */
#endif
}

/** Trims the linked list back to the specified pointer and return how many bytes marker was behind p
 *
 * Pointers should be released in the inverse order to allocation.
 *
 * Alternatively the oldest pointer can be released, resulting in any newer pointer
 * also being removed from the list.
 *
 * @param[in] m		to release.
 * @return
 *	- 0 marker is ahead of p.
 *	- >0 the number of bytes the marker is behind p.
 */
static inline size_t fr_sbuff_marker_release_behind(fr_sbuff_marker_t *m)
{
	size_t len = fr_sbuff_behind(m);
	fr_sbuff_marker_release(m);
	return len;
}

/** Trims the linked list back to the specified pointer and return how many bytes marker was ahead of p
 *
 * Pointers should be released in the inverse order to allocation.
 *
 * Alternatively the oldest pointer can be released, resulting in any newer pointer
 * also being removed from the list.
 *
 * @param[in] m		to release.
 * @return
 *	- 0 marker is ahead of p.
 *	- >0 the number of bytes the marker is behind p.
 */
static inline size_t fr_sbuff_marker_release_ahead(fr_sbuff_marker_t *m)
{
	size_t len = fr_sbuff_ahead(m);
	fr_sbuff_marker_release(m);
	return len;
}
/** @} */

/** @name Copy/print data to an sbuff
 *
 * These functions are typically used for printing.
 *
 * @{
 */
#define	fr_sbuff_in_char(_sbuff, ...) fr_sbuff_in_bstrncpy(_sbuff, ((char []){ __VA_ARGS__ }), sizeof((char []){ __VA_ARGS__ }))
#define	FR_SBUFF_IN_CHAR_RETURN(_sbuff, ...) FR_SBUFF_RETURN(fr_sbuff_in_bstrncpy, _sbuff, ((char []){ __VA_ARGS__ }), sizeof((char []){ __VA_ARGS__ }))

ssize_t	fr_sbuff_in_strcpy(fr_sbuff_t *sbuff, char const *str);
#define	FR_SBUFF_IN_STRCPY_RETURN(...) FR_SBUFF_RETURN(fr_sbuff_in_strcpy, ##__VA_ARGS__)

ssize_t	fr_sbuff_in_bstrncpy(fr_sbuff_t *sbuff, char const *str, size_t len);
#define	FR_SBUFF_IN_BSTRNCPY_RETURN(...) FR_SBUFF_RETURN(fr_sbuff_in_bstrncpy, ##__VA_ARGS__)
#define	FR_SBUFF_IN_STRCPY_LITERAL_RETURN(_sbuff, _str) FR_SBUFF_RETURN(fr_sbuff_in_bstrncpy, _sbuff, _str, sizeof(_str) - 1)

ssize_t	fr_sbuff_in_bstrcpy_buffer(fr_sbuff_t *sbuff, char const *str);
#define	FR_SBUFF_IN_BSTRCPY_BUFFER_RETURN(...) FR_SBUFF_RETURN(fr_sbuff_in_bstrcpy_buffer, ##__VA_ARGS__)

ssize_t	fr_sbuff_in_vsprintf(fr_sbuff_t *sbuff, char const *fmt, va_list ap);
#define	FR_SBUFF_IN_VSPRINTF_RETURN(...) FR_SBUFF_RETURN(fr_sbuff_in_vsprintf, ##__VA_ARGS__)

ssize_t	fr_sbuff_in_sprintf(fr_sbuff_t *sbuff, char const *fmt, ...);
#define	FR_SBUFF_IN_SPRINTF_RETURN(...) FR_SBUFF_RETURN(fr_sbuff_in_sprintf, ##__VA_ARGS__)

ssize_t	fr_sbuff_in_snprint(fr_sbuff_t *sbuff, char const *in, size_t inlen, char quote);
#define	FR_SBUFF_IN_SNPRINT_RETURN(...) FR_SBUFF_RETURN(fr_sbuff_in_snprint, ##__VA_ARGS__)

ssize_t	fr_sbuff_in_snprint_buffer(fr_sbuff_t *sbuff, char const *in, char quote);
#define	FR_SBUFF_IN_SNPRINT_BUFFER_RETURN(...)	FR_SBUFF_RETURN(fr_sbuff_in_snprint_buffer, ##__VA_ARGS__)

/** Lookup a string in a table using an integer value, and copy it to the sbuff
 *
 * @param[out] _slen	Where to write the return value.
 * @param[in] _table	to search for number in.
 * @param[in] _number	to search for.
 * @param[in] _def	Default string value.
 */
#define		fr_sbuff_in_table_str(_slen, _sbuff, _table, _number, _def) \
				      _slen = fr_sbuff_in_strcpy(_sbuff, fr_table_str_by_value(_table, _number, _def))
#define		FR_SBUFF_IN_TABLE_STR_RETURN(_sbuff, _table, _number, _def) \
do { \
	ssize_t		_slen; \
	fr_sbuff_in_table_str(_slen, _sbuff, _table, _number, _def); \
	if (_slen < 0) return _slen; \
} while (0)
/** @} */

/** @name Copy data out of an sbuff
 *
 * These functions are typically used for parsing.
 *
 * @{
 */
fr_sbuff_term_t	*fr_sbuff_terminals_amerge(TALLOC_CTX *ctx,
					   fr_sbuff_term_t const *a, fr_sbuff_term_t const *b);

size_t	fr_sbuff_out_bstrncpy(fr_sbuff_t *out, fr_sbuff_t *in, size_t len);

ssize_t	fr_sbuff_out_bstrncpy_exact(fr_sbuff_t *out, fr_sbuff_t *in, size_t len);

size_t	fr_sbuff_out_bstrncpy_allowed(fr_sbuff_t *out, fr_sbuff_t *in, size_t len,
				      bool const allowed[static UINT8_MAX + 1]);

size_t	fr_sbuff_out_bstrncpy_until(fr_sbuff_t *out, fr_sbuff_t *in, size_t len,
				    fr_sbuff_term_t const *tt, char escape_chr);

size_t	fr_sbuff_out_unescape_until(fr_sbuff_t *out, fr_sbuff_t *in, size_t len,
				    fr_sbuff_term_t const *tt,
				    fr_sbuff_escape_rules_t const *rules);

/** Find the longest prefix in an sbuff
 *
 * @param[out] _match_len	The length of the matched string.
 *				May be NULL.
 * @param[out] _out		The value resolved in the table.
 * @param[in] _table		to find longest match in.
 * @param[in] _sbuff		containing the needle.
 * @param[in] _def		Default value if no match is found.
 */
#define fr_sbuff_out_by_longest_prefix(_match_len, _out, _table, _sbuff, _def) \
do { \
	size_t		_match_len_tmp; \
	*(_out) = fr_table_value_by_longest_prefix(&_match_len_tmp, _table, \
						   fr_sbuff_current(_sbuff), fr_sbuff_remaining(_sbuff), \
						   _def); \
	(void) fr_sbuff_advance(_sbuff, _match_len_tmp); /* can't fail */ \
	*(_match_len) = _match_len_tmp; \
} while (0)

/** Build a talloc wrapper function for a fr_sbuff_out_* function
 *
 * @param[in] _func	to call.
 * @param[in] _in	input sbuff arg.
 * @param[in] _len	expected output len.
 * @param[in] ...	additional arguments to pass to _func.
 */
#define SBUFF_OUT_TALLOC_ERR_FUNC_DEF(_func, _in, _len, ...) \
{ \
	fr_sbuff_t		sbuff; \
	fr_sbuff_uctx_talloc_t	tctx; \
	fr_sbuff_parse_error_t	err; \
	ssize_t			slen; \
	fr_sbuff_init_talloc(ctx, &sbuff, &tctx, \
			     ((_len) != SIZE_MAX) ? (_len) : 1024, \
			     ((_len) != SIZE_MAX) ? (_len) : SIZE_MAX); \
	slen = _func(&err, &sbuff, _in, _len, ##__VA_ARGS__); \
	if (slen <= 0) { \
		if (err != FR_SBUFF_PARSE_OK) { \
			TALLOC_FREE(sbuff.buff); \
		} else { \
			fr_sbuff_trim_talloc(&sbuff, 0); \
		} \
		*out = sbuff.buff; \
		return 0; \
	} \
	fr_sbuff_trim_talloc(&sbuff, SIZE_MAX); \
	*out = sbuff.buff; \
	return (size_t)slen; \
}

/** Build a talloc wrapper function for a fr_sbuff_out_* function
 *
 * @param[in] _func	to call.
 * @param[in] _in	input sbuff arg.
 * @param[in] _len	expected output len.
 * @param[in] ...	additional arguments to pass to _func.
 */
#define SBUFF_OUT_TALLOC_FUNC_DEF(_func, _in, _len, ...) \
{ \
	fr_sbuff_t		sbuff; \
	fr_sbuff_uctx_talloc_t	tctx; \
	ssize_t			slen; \
	fr_sbuff_init_talloc(ctx, &sbuff, &tctx, \
			     ((_len) != SIZE_MAX) ? (_len) : 1024, \
			     ((_len) != SIZE_MAX) ? (_len) : SIZE_MAX); \
	slen = _func(&sbuff, _in, _len, ##__VA_ARGS__); \
	if (slen <= 0) { \
		fr_sbuff_trim_talloc(&sbuff, 0); \
		*out = sbuff.buff; \
		return 0; \
	} \
	fr_sbuff_trim_talloc(&sbuff, SIZE_MAX); \
	*out = sbuff.buff; \
	return (size_t)slen; \
}

/** Build a talloc wrapper function for a fr_sbuff_out_* function
 *
 * @param[in] _func	to call.
 * @param[in] _in	input sbuff arg.
 * @param[in] _len	expected output len.
 * @param[in] ...	additional arguments to pass to _func.
 */
#define SBUFF_OUT_TALLOC_FUNC_NO_LEN_DEF(_func, ...) \
{ \
	fr_sbuff_t		sbuff; \
	fr_sbuff_uctx_talloc_t	tctx; \
	ssize_t			slen; \
	fr_sbuff_init_talloc(ctx, &sbuff, &tctx, 256, SIZE_MAX); \
	slen = _func(&sbuff, ##__VA_ARGS__); \
	if (slen <= 0) { \
		fr_sbuff_trim_talloc(&sbuff, 0); \
		*out = sbuff.buff; \
		return 0; \
	} \
	fr_sbuff_trim_talloc(&sbuff, SIZE_MAX); \
	*out = sbuff.buff; \
	return (size_t)slen; \
}

static inline size_t fr_sbuff_out_abstrncpy(TALLOC_CTX *ctx, char **out, fr_sbuff_t *in, size_t len)
SBUFF_OUT_TALLOC_FUNC_DEF(fr_sbuff_out_bstrncpy, in, len);

static inline size_t fr_sbuff_out_abstrncpy_exact(TALLOC_CTX *ctx, char **out, fr_sbuff_t *in, size_t len)
SBUFF_OUT_TALLOC_FUNC_DEF(fr_sbuff_out_bstrncpy_exact, in, len);

static inline size_t fr_sbuff_out_abstrncpy_allowed(TALLOC_CTX *ctx, char **out, fr_sbuff_t *in, size_t len,
						    bool const allowed[static UINT8_MAX + 1])
SBUFF_OUT_TALLOC_FUNC_DEF(fr_sbuff_out_bstrncpy_allowed, in, len, allowed);

static inline size_t fr_sbuff_out_abstrncpy_until(TALLOC_CTX *ctx, char **out, fr_sbuff_t *in, size_t len,
						  fr_sbuff_term_t const *tt, char escape_chr)
SBUFF_OUT_TALLOC_FUNC_DEF(fr_sbuff_out_bstrncpy_until, in, len, tt, escape_chr);

static inline size_t fr_sbuff_out_aunescape_until(TALLOC_CTX *ctx, char **out, fr_sbuff_t *in, size_t len,
						  fr_sbuff_term_t const *tt, fr_sbuff_escape_rules_t const *rules)
SBUFF_OUT_TALLOC_FUNC_DEF(fr_sbuff_out_unescape_until, in, len, tt, rules);
/** @} */

/** @name Look for a token in a particular format, parse it, and write it to the output pointer
 *
 * These functions should not be called directly.  #fr_sbuff_out should be used instead
 * so that if the output variable type changes, the parse rules are automatically changed.
 * @{
 */
size_t fr_sbuff_out_bool(bool *out, fr_sbuff_t *in);

size_t fr_sbuff_out_int8(fr_sbuff_parse_error_t *err, int8_t *out, fr_sbuff_t *sbuff, bool no_trailing);
size_t fr_sbuff_out_int16(fr_sbuff_parse_error_t *err, int16_t *out, fr_sbuff_t *sbuff, bool no_trailing);
size_t fr_sbuff_out_int32(fr_sbuff_parse_error_t *err, int32_t *out, fr_sbuff_t *sbuff, bool no_trailing);
size_t fr_sbuff_out_int64(fr_sbuff_parse_error_t *err, int64_t *out, fr_sbuff_t *sbuff, bool no_trailing);
size_t fr_sbuff_out_uint8(fr_sbuff_parse_error_t *err, uint8_t *out, fr_sbuff_t *sbuff, bool no_trailing);
size_t fr_sbuff_out_uint16(fr_sbuff_parse_error_t *err, uint16_t *out, fr_sbuff_t *sbuff, bool no_trailing);
size_t fr_sbuff_out_uint32(fr_sbuff_parse_error_t *err, uint32_t *out, fr_sbuff_t *sbuff, bool no_trailing);
size_t fr_sbuff_out_uint64(fr_sbuff_parse_error_t *err, uint64_t *out, fr_sbuff_t *sbuff, bool no_trailing);

size_t fr_sbuff_out_uint8_oct(fr_sbuff_parse_error_t *err, uint8_t *out, fr_sbuff_t *sbuff, bool no_trailing);
size_t fr_sbuff_out_uint16_oct(fr_sbuff_parse_error_t *err, uint16_t *out, fr_sbuff_t *sbuff, bool no_trailing);
size_t fr_sbuff_out_uint32_oct(fr_sbuff_parse_error_t *err, uint32_t *out, fr_sbuff_t *sbuff, bool no_trailing);
size_t fr_sbuff_out_uint64_oct(fr_sbuff_parse_error_t *err, uint64_t *out, fr_sbuff_t *sbuff, bool no_trailing);

size_t fr_sbuff_out_uint8_hex(fr_sbuff_parse_error_t *err, uint8_t *out, fr_sbuff_t *sbuff, bool no_trailing);
size_t fr_sbuff_out_uint16_hex(fr_sbuff_parse_error_t *err, uint16_t *out, fr_sbuff_t *sbuff, bool no_trailing);
size_t fr_sbuff_out_uint32_hex(fr_sbuff_parse_error_t *err, uint32_t *out, fr_sbuff_t *sbuff, bool no_trailing);
size_t fr_sbuff_out_uint64_hex(fr_sbuff_parse_error_t *err, uint64_t *out, fr_sbuff_t *sbuff, bool no_trailing);

size_t fr_sbuff_out_float32(fr_sbuff_parse_error_t *err, float *out, fr_sbuff_t *sbuff, bool no_trailing);
size_t fr_sbuff_out_float64(fr_sbuff_parse_error_t *err, double *out, fr_sbuff_t *sbuff, bool no_trailing);

/** Parse a value based on the output type
 *
 * @param[out] _err	If not NULL a value describing the parse error
 *			will be written to err.
 * @param[out] _out	Pointer to an integer type.
 * @param[in] _in	Sbuff to parse integer from.
 * @return The number of bytes parsed (even on error).
 */
#define fr_sbuff_out(_err, _out, _in) \
	_Generic((_out), \
		 bool *		: fr_sbuff_out_bool((bool *)_out, _in), \
		 int8_t *	: fr_sbuff_out_int8(_err, (int8_t *)_out, _in, true), \
		 int16_t *	: fr_sbuff_out_int16(_err, (int16_t *)_out, _in, true), \
		 int32_t *	: fr_sbuff_out_int32(_err, (int32_t *)_out, _in, true), \
		 int64_t *	: fr_sbuff_out_int64(_err, (int64_t *)_out, _in, true), \
		 uint8_t *	: fr_sbuff_out_uint8(_err, (uint8_t *)_out, _in, true), \
		 uint16_t *	: fr_sbuff_out_uint16(_err, (uint16_t *)_out, _in, true), \
		 uint32_t *	: fr_sbuff_out_uint32(_err, (uint32_t *)_out, _in, true), \
		 uint64_t *	: fr_sbuff_out_uint64(_err, (uint64_t *)_out, _in, true), \
		 float *	: fr_sbuff_out_float32(_err, (float *)_out, _in, true), \
		 double *	: fr_sbuff_out_float64(_err, (double *)_out, _in, true) \
	)
/** @} */


/** @name Conditional advancement
 *
 * These functions are typically used for parsing when trying to locate
 * a sequence of characters in the sbuff.
 * @{
 */
size_t	fr_sbuff_adv_past_str(fr_sbuff_t *sbuff, char const *needle, size_t need_len);

#define fr_sbuff_adv_past_str_literal(_sbuff, _needle) fr_sbuff_adv_past_str(_sbuff, _needle, sizeof(_needle) - 1)

size_t	fr_sbuff_adv_past_strcase(fr_sbuff_t *sbuff, char const *needle, size_t need_len);

#define fr_sbuff_adv_past_strcase_literal(_sbuff, _needle) fr_sbuff_adv_past_strcase(_sbuff, _needle, sizeof(_needle) - 1)

size_t	fr_sbuff_adv_past_whitespace(fr_sbuff_t *sbuff, size_t len);

size_t	fr_sbuff_adv_past_allowed(fr_sbuff_t *sbuff, size_t len, bool const allowed[static UINT8_MAX + 1]);

size_t	fr_sbuff_adv_until(fr_sbuff_t *sbuff, size_t len, fr_sbuff_term_t const *tt, char escape_chr);

char	*fr_sbuff_adv_to_chr_utf8(fr_sbuff_t *in, size_t len, char const *chr);

char	*fr_sbuff_adv_to_chr(fr_sbuff_t *in, size_t len, char c);

char	*fr_sbuff_adv_to_str(fr_sbuff_t *sbuff, size_t len, char const *needle, size_t needle_len);

#define fr_sbuff_adv_to_str_literal(_sbuff, _len, _needle) fr_sbuff_adv_to_str(_sbuff, _len, _needle, sizeof(_needle) - 1)

char	*fr_sbuff_adv_to_strcase(fr_sbuff_t *sbuff, size_t len, char const *needle, size_t needle_len);

#define fr_sbuff_adv_to_strcase_literal(_sbuff, _len, _needle) fr_sbuff_adv_to_strcase(_sbuff, _len, _needle, sizeof(_needle) - 1)

bool	fr_sbuff_next_if_char(fr_sbuff_t *sbuff, char c);

bool	fr_sbuff_next_unless_char(fr_sbuff_t *sbuff, char c);

/** Advance the sbuff by one char
 *
 */
static inline char fr_sbuff_next(fr_sbuff_t *sbuff)
{
	if (FR_SBUFF_CANT_EXTEND(sbuff)) return '\0';
	return *(sbuff->p++);
}
/** @} */

/** @name Conditions
 *
 * These functions are typically used in recursive decent parsing for
 * look ahead.
 * @{
 */
bool fr_sbuff_is_terminal(fr_sbuff_t *in, fr_sbuff_term_t const *tt);

static inline bool fr_sbuff_is_in_charset(fr_sbuff_t *sbuff, bool const chars[static UINT8_MAX + 1])
{
	if (FR_SBUFF_CANT_EXTEND(sbuff)) return false;
	return chars[(uint8_t)*sbuff->p];
}

static inline bool fr_sbuff_is_str(fr_sbuff_t *sbuff, char const *str, size_t len)
{
	if (len == SIZE_MAX) len = strlen(str);
	if (FR_SBUFF_CANT_EXTEND_LOWAT(sbuff, len)) return false;
	return memcmp(sbuff->p, str, len) == 0;
}
#define fr_sbuff_is_str_literal(_sbuff, _str) fr_sbuff_is_str(_sbuff, _str, sizeof(_str) - 1)

static inline bool fr_sbuff_is_char(fr_sbuff_t *sbuff, char c)
{
	if (FR_SBUFF_CANT_EXTEND(sbuff)) return false;
	return *sbuff->p == c;
}

static inline bool fr_sbuff_is_digit(fr_sbuff_t *sbuff)
{
	if (FR_SBUFF_CANT_EXTEND(sbuff)) return false;
	return isdigit(*sbuff->p);
}

static inline bool fr_sbuff_is_upper(fr_sbuff_t *sbuff)
{
	if (FR_SBUFF_CANT_EXTEND(sbuff)) return false;
	return isupper(*sbuff->p);
}

static inline bool fr_sbuff_is_lower(fr_sbuff_t *sbuff)
{
	if (FR_SBUFF_CANT_EXTEND(sbuff)) return false;
	return islower(*sbuff->p);
}

static inline bool fr_sbuff_is_alpha(fr_sbuff_t *sbuff)
{
	if (FR_SBUFF_CANT_EXTEND(sbuff)) return false;
	return isalpha(*sbuff->p);
}

static inline bool fr_sbuff_is_space(fr_sbuff_t *sbuff)
{
	if (FR_SBUFF_CANT_EXTEND(sbuff)) return false;
	return isspace(*sbuff->p);
}

static inline bool fr_sbuff_is_hex(fr_sbuff_t *sbuff)
{
	if (FR_SBUFF_CANT_EXTEND(sbuff)) return false;
	return (isdigit(*sbuff->p) || ((tolower(*sbuff->p) >= 'a') && (tolower(*sbuff->p) <= 'f')));
}
/** @} */

#ifdef __cplusplus
}
#endif
