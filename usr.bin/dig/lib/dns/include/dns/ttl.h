/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: ttl.h,v 1.4 2020/09/14 08:40:43 florian Exp $ */

#ifndef DNS_TTL_H
#define DNS_TTL_H 1

/*! \file dns/ttl.h */

/***
 ***	Imports
 ***/

#include <isc/types.h>

/***
 ***	Functions
 ***/

isc_result_t
dns_ttl_totext(uint32_t src, int verbose,
	       isc_buffer_t *target);
/*%<
 * Output a TTL or other time interval in a human-readable form.
 * The time interval is given as a count of seconds in 'src'.
 * The text representation is appended to 'target'.
 *
 * If 'verbose' is 0, use the terse BIND 8 style, like "1w2d3h4m5s".
 *
 * If 'verbose' is 1, use a verbose style like the SOA comments
 * in "dig", like "1 week 2 days 3 hours 4 minutes 5 seconds".
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	ISC_R_NOSPACE
 */

#endif /* DNS_TTL_H */
