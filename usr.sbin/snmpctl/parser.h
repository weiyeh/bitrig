/*	$OpenBSD: parser.h,v 1.8 2013/10/07 11:40:09 reyk Exp $	*/

/*
 * Copyright (c) 2007, 2008 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

enum actions {
	NONE,
	MONITOR,
	SHOW_MIB,
	TRAP,
	GET,
	WALK,
	BULKWALK
};

struct parse_val {
	char			*val;
	TAILQ_ENTRY(parse_val)	 val_entry;
};
TAILQ_HEAD(parse_vals, parse_val);

struct parse_result {
	enum actions		 action;
	struct imsgbuf		*ibuf;
	char			*host;
	struct parse_vals	 oids;
	char			*community;
	int			 version;
};

struct parse_result	*parse(int, char *[]);
void			 snmpclient(struct parse_result *);
