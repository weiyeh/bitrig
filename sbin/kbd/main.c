/*	$OpenBSD: main.c,v 1.2 1996/06/30 05:16:28 deraadt Exp $	*/

/*
 * Copyright (c) 1996 Juergen Hannken-Illjes
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project
 *	by Juergen Hannken-Illjes.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <unistd.h>
#include <stdio.h>

extern void kbd_list __P((void));
extern void kbd_set __P((char *, int));

extern char *__progname;

static void
usage()
{
	fprintf(stderr, "usage: %s -l  or  %s [-q] name\n",
		__progname, __progname);
	exit(1);
}

main(argc, argv)
	int argc;
	char **argv;
{
	char *optstring = "lq";
	int ch, list_tables, verbose;

	list_tables = 0;
	verbose = 1;

	while ((ch = getopt(argc, argv, optstring)) != -1)
		switch (ch) {
		case 'l':
			list_tables = 1;
			break;

		case 'q':
			verbose = 0;
			break;

		case '?':
		default:
			usage();
		}
	if (argc != optind + list_tables ? 0 : 1)
		usage();

	if (list_tables) {
		kbd_list();
	} else {
		kbd_set(argv[optind], verbose);
	}

	exit(0);
}
