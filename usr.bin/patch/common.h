/*	$OpenBSD: common.h,v 1.18 2003/07/22 21:50:21 millert Exp $	*/

#define DEBUGGING

#include <stdio.h>

/* constants */

#define TRUE	1
#define FALSE	0

#define MAXHUNKSIZE 100000	/* is this enough lines? */
#define INITHUNKMAX 125		/* initial dynamic allocation size */
#define MAXLINELEN 8192
#define BUFFERSIZE 1024

#define SCCSPREFIX "s."
#define GET "get -e %s"
#define SCCSDIFF "get -p %s | diff - %s >/dev/null"

#define RCSSUFFIX ",v"
#define CHECKOUT "co -l %s"
#define RCSDIFF "rcsdiff %s > /dev/null"

#define ORIGEXT ".orig"
#define REJEXT ".rej"

/* handy definitions */

#define strNE(s1,s2) (strcmp(s1, s2))
#define strEQ(s1,s2) (!strcmp(s1, s2))
#define strnNE(s1,s2,l) (strncmp(s1, s2, l))
#define strnEQ(s1,s2,l) (!strncmp(s1, s2, l))

/* typedefs */

typedef char    bool;
typedef long    LINENUM;	/* must be signed */

/* globals */

EXT int         Argc;		/* guess */
EXT char      **Argv;
EXT int         Argc_last;	/* for restarting plan_b */
EXT char      **Argv_last;

EXT struct stat filestat;	/* file statistics area */
EXT int filemode INIT(0644);

EXT char        buf[MAXLINELEN];/* general purpose buffer */
EXT FILE       *ofp INIT(NULL);	/* output file pointer */
EXT FILE       *rejfp INIT(NULL);	/* reject file pointer */

EXT int         myuid;		/* cache getuid return value */

EXT bool using_plan_a INIT(TRUE);	/* try to keep everything in memory */
EXT bool out_of_mem INIT(FALSE);/* ran out of memory in plan a */

#define MAXFILEC 2
EXT int filec   INIT(0);	/* how many file arguments? */
EXT char       *filearg[MAXFILEC];
EXT bool ok_to_create_file INIT(FALSE);
EXT char       *bestguess INIT(NULL);	/* guess at correct filename */

EXT char       *outname INIT(NULL);

EXT char       *origprae INIT(NULL);

EXT char       *TMPOUTNAME;
EXT char       *TMPINNAME;
EXT char       *TMPREJNAME;
EXT char       *TMPPATNAME;
EXT bool toutkeep INIT(FALSE);
EXT bool trejkeep INIT(FALSE);

EXT LINENUM last_offset INIT(0);
#ifdef DEBUGGING
EXT int debug   INIT(0);
#endif
EXT LINENUM maxfuzz INIT(2);
EXT bool force  INIT(FALSE);
EXT bool batch  INIT(FALSE);
EXT bool verbose INIT(TRUE);
EXT bool reverse INIT(FALSE);
EXT bool noreverse INIT(FALSE);
EXT bool skip_rest_of_patch INIT(FALSE);
EXT int strippath INIT(957);
EXT bool canonicalize INIT(FALSE);
/* TRUE if -C was specified on command line.  */
EXT bool check_only  INIT(FALSE);


#define CONTEXT_DIFF 1
#define NORMAL_DIFF 2
#define ED_DIFF 3
#define NEW_CONTEXT_DIFF 4
#define UNI_DIFF 5
EXT int diff_type INIT(0);

EXT bool do_defines INIT(FALSE);/* patch using ifdef, ifndef, etc. */
EXT char        if_defined[128];/* #ifdef xyzzy */
EXT char        not_defined[128];	/* #ifndef xyzzy */
EXT char        else_defined[] INIT("#else\n");	/* #else */
EXT char        end_defined[128];	/* #endif xyzzy */

EXT char       *revision INIT(NULL);	/* prerequisite revision, if any */
