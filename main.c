/*
 * S-nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 Steffen "Daode" Nurpmeso.
 */
/*
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* TODO longjmp() globbering as in cmd1.c and cmd3.c (see there)
 * TODO Problem: Popen doesn't encapsulate all cases of open failures,
 * TODO may leave child running if fdopen() fails! */

/*
 * Most strcpy/sprintf functions have been changed to strncpy/snprintf to
 * correct several buffer overruns (at least one ot them was exploitable).
 * Sat Jun 20 04:58:09 CEST 1998 Alvaro Martinez Echevarria <alvaro@lander.es>
 * ---
 * Note: We set egid to realgid ... and only if we need the egid we will
 *       switch back temporary.  Nevertheless, I do not like seg faults.
 *       Werner Fink, <werner@suse.de>
 */


#include "config.h"
#ifdef	HAVE_NL_LANGINFO
# include <langinfo.h>
#endif
#define _MAIL_GLOBS_
#include "rcv.h"
#include "extern.h"
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef HAVE_SETLOCALE
# include <locale.h>
#endif

/*
 * Mail -- a mail program
 *
 * Startup -- interface with user.
 */

static sigjmp_buf	hdrjmp;

char		*progname;
sighandler_type	dflpipe = SIG_DFL;

static void hdrstop(int signo);
static void setscreensize(int dummy);

int 
main(int argc, char *argv[])
{
	const char optstr[] = "A:BHEFINVT:RS:a:b:c:dDefinqr:s:tu:v~O:";
	int scnt, i, existonly = 0, headersonly = 0, sendflag = 0;
	struct name *to, *cc, *bcc, *smopts;
	struct attachment *attach;
	char *subject, *cp, *ef, *qf = NULL, *fromaddr = NULL, *Aflag = NULL;
	char nosrc = 0;
	int Eflag = 0, Fflag = 0, Nflag = 0, tflag = 0;
	sighandler_type prevint;

	(void)&Nflag;
	/*
	 * Absolutely the first thing we do is save our egid
	 * and set it to the rgid, so that we can safely run
	 * setgid.  We use the sgid (saved set-gid) to allow ourselves
	 * to revert to the egid if we want (temporarily) to become
	 * priveliged.
	 */
	effectivegid = getegid();
	realgid = getgid();
	if (setgid(realgid) < 0) {
		perror("setgid");
		exit(1);
	}

	starting = 1;
	progname = strrchr(argv[0], '/');
	if (progname != NULL)
		progname++;
	else
		progname = argv[0];
	/*
	 * Set up a reasonable environment.
	 * Figure out whether we are being run interactively,
	 * start the SIGCHLD catcher, and so forth.
	 */
	safe_signal(SIGCHLD, sigchild);
	is_a_tty[0] = isatty(0);
	is_a_tty[1] = isatty(1);
	if (is_a_tty[0]) {
		assign("interactive", "");
		if (is_a_tty[1])
			safe_signal(SIGPIPE, dflpipe = SIG_IGN);
	}
	assign("header", "");
	assign("save", "");
#ifdef	HAVE_SETLOCALE
	setlocale(LC_ALL, "");
	mb_cur_max = MB_CUR_MAX;
# if defined HAVE_NL_LANGINFO && defined CODESET
	if (value("ttycharset") == NULL && (cp = nl_langinfo(CODESET)) != NULL)
		assign("ttycharset", cp);
# endif
# if defined HAVE_MBTOWC && defined HAVE_WCTYPE_H
	if (mb_cur_max > 1) {
		wchar_t	wc;
		if (mbtowc(&wc, "\303\266", 2) == 2 && wc == 0xF6 &&
				mbtowc(&wc, "\342\202\254", 3) == 3 &&
				wc == 0x20AC)
			utf8 = 1;
	}
# endif
#else
	mb_cur_max = 1;
#endif
#ifdef HAVE_CATGETS
# ifdef NL_CAT_LOCALE
	i = NL_CAT_LOCALE;
# else
	i = 0;
# endif
	catd = catopen(CATNAME, i);
#endif
#ifdef HAVE_ICONV
	iconvd = (iconv_t) -1;
#endif
	image = -1;
	/*
	 * Now, determine how we are being used.
	 * We successively pick off - flags.
	 * If there is anything left, it is the base of the list
	 * of users to mail to.  Argp will be set to point to the
	 * first of these users.
	 */
	ef = NULL;
	to = NULL;
	cc = NULL;
	bcc = NULL;
	attach = NULL;
	smopts = NULL;
	subject = NULL;
	scnt = 0;
	while ((i = getopt(argc, argv, optstr)) != EOF) {
		switch (i) {
		case 'V':
			puts(version);
			exit(0);
			/*NOTREACHED*/
		case 'B':
			setvbuf(stdin, NULL, _IOLBF, 0);
			setvbuf(stdout, NULL, _IOLBF, 0);
			break;
		case 'H':
			headersonly = 1;
			break;
		case 'E':
			Eflag = 1;
			break;
		case 'F':
			Fflag = 1;
			sendflag++;
			break;
		case 'S':
			argv[scnt++] = optarg;
			break;
		case 'T':
			/*
			 * Next argument is temp file to write which
			 * articles have been read/deleted for netnews.
			 */
			Tflag = optarg;
			if ((i = creat(Tflag, 0600)) < 0) {
				perror(Tflag);
				exit(1);
			}
			close(i);
			/*FALLTHRU*/
		case 'I':
			/*
			 * Show Newsgroups: field in header summary
			 */
			Iflag = 1;
			break;
		case 'u':
			/*
			 * Next argument is person to pretend to be.
			 */
			uflag = myname = optarg;
			break;
		case 'i':
			/*
			 * User wants to ignore interrupts.
			 * Set the variable "ignore"
			 */
			assign("ignore", "");
			break;
		case 'd':
			debug++;
			break;
		case 'D':
			assign("disconnected", "");
			break;
		case 'e':
			existonly++;
			break;
		case 's':
			/*
			 * Give a subject field for sending from
			 * non terminal
			 */
			subject = optarg;
			sendflag++;
			break;
		case 'f':
			/*
			 * User is specifying file to "edit" with Mail,
			 * as opposed to reading system mailbox.
			 * If no argument is given, we read his
			 * mbox file.
			 *
			 * Check for remaining arguments later.
			 */
			ef = "&";
			break;
		case 'q':
			/*
			 * User is specifying file to quote in front of
			 * the mail to be collected.
			 */
			if ((argv[optind]) && (argv[optind][0] != '-'))
				qf = argv[optind++];
			else
				qf = NULL;
			sendflag++;
			break;
		case 'n':
			/*
			 * User doesn't want to source /usr/lib/Mail.rc
			 */
			nosrc++;
			break;
		case 'N':
			/*
			 * Avoid initial header printing.
			 */
			Nflag = 1;
			unset_internal("header");
			break;
		case 'v':
			/*
			 * Send mailer verbose flag
			 */
			assign("verbose", "");
			break;
		case 'r':
			/*
			 * Set From address.
			 * MTA is only interested in plain address so strip
			 * anything else (but still give user the option to
			 * simply pass a fully fledged email address)
			 */
			i = count(smopts);
			fromaddr = optarg;
			smopts = cat(smopts, nalloc("-r", 0));
			smopts = cat(smopts, userarg_extract(optarg, GFULL));
			if (count(smopts) != i + 2) {
				fprintf(stderr, tr(271,
					"More than one address "
					"given with -r\n"));
				goto usage;
			}
			tildeflag = -1;
			sendflag++;
			break;
		case 'a':
			/*
			 * Get attachment filenames
			 */
			attach = add_attachment(attach, optarg, 0);
			if (attach == NULL) {
				perror(optarg);
				exit(1);
			}
			sendflag++;
			break;
		case 'c':
			/*
			 * Get Carbon Copy Recipient list
			 */
			cc = checkaddrs(cat(cc,
				userarg_extract(optarg, GCC|GFULL)));
			sendflag++;
			break;
		case 'b':
			/*
			 * Get Blind Carbon Copy Recipient list
			 */
			bcc = checkaddrs(cat(bcc,
				userarg_extract(optarg, GBCC|GFULL)));
			sendflag++;
			break;
		case 'O':
			/*
			 * Additional options to pass-through to MTA
			 */
			smopts = cat(smopts, nalloc(optarg, 0));
			sendflag++;
			break;
		case '~':
			if (tildeflag == 0)
				tildeflag = 1;
			break;
		case 't':
			sendflag = 1;
			tflag = 1;
			break;
		case 'A':
			Aflag = optarg;
			break;
		case 'R':
			Rflag = 1;
			break;
		case '?':
usage:
			fprintf(stderr, tr(135,
					"Usage: %s -eiIUdEFntBDNHRV~ -T FILE "
					"-u USER -O MTAARG -r address "
					"-s SUBJECT -a FILE -q FILE -f FILE "
					"-A ACCOUNT -b USERS -c USERS "
					"-S OPTION USERS\n"),
				progname);
			exit(2);
		}
	}
	if (ef != NULL) {
		if (optind < argc) {
			if (optind + 1 < argc) {
				fprintf(stderr, tr(205,
					"More than one file given with -f\n"));
				goto usage;
			}
			ef = argv[optind];
		}
	} else {
		for (i = optind; argv[i]; i++)
			to = checkaddrs(cat(to,
				userarg_extract(argv[i], GTO|GFULL)));
	}
	/*
	 * Check for inconsistent arguments.
	 */
	if (ef != NULL && to != NULL) {
		fprintf(stderr, tr(137,
			"Cannot give -f and people to send to.\n"));
		goto usage;
	}
	if (sendflag && !tflag && to == NULL) {
		fprintf(stderr, tr(138,
			"Send options without primary recipient specified.\n"));
		goto usage;
	}
	if (Rflag && to != NULL) {
		fprintf(stderr, "The -R option is meaningless in send mode.\n");
		goto usage;
	}
	if (Iflag && ef == NULL) {
		fprintf(stderr, tr(204, "Need -f with -I.\n"));
		goto usage;
	}
	tinit();
	setscreensize(0);
#ifdef SIGWINCH
	if (value("interactive"))
		if (safe_signal(SIGWINCH, SIG_IGN) != SIG_IGN)
			safe_signal(SIGWINCH, setscreensize);
#endif
	input = stdin;
	rcvmode = !to && !tflag;
	spreserve();
	if (!nosrc)
		load(MAILRC);
	/*
	 * Expand returns a savestr, but load only uses the file name
	 * for fopen, so it's safe to do this.
	 */
	if ((cp = getenv("MAILRC")) != NULL)
		load(expand(cp));
	else if ((cp = getenv("NAILRC")) != NULL)
		load(expand(cp));
	else
		load(expand("~/.mailrc"));
	if (getenv("NAIL_EXTRA_RC") == NULL &&
			(cp = value("NAIL_EXTRA_RC")) != NULL)
		load(expand(cp));
	/*
	 * Now we can set the account.
	 */
	if (Aflag) {
		char	*a[2];
		a[0] = Aflag;
		a[1] = NULL;
		account(a);
	}
	/*
	 * Override 'skipemptybody' if '-E' flag was given.
	 */
	if (Eflag)
		assign("skipemptybody", "");
	/*
	 * -S arguments override rc files.
	 */
	for (i = 0; i < scnt; ++i) {
		char *a[2];
		a[0] = argv[i];
		a[1] = NULL;
		set(a);
	}

	starting = 0;

	/*
	 * From address from command line overrides rc files.
	 */
	if (fromaddr)
		assign("from", fromaddr);
	if (!rcvmode) {
		mail(to, cc, bcc, smopts, subject, attach, qf, Fflag, tflag,
		    Eflag);
		/*
		 * why wait?
		 */
		exit(senderr ? 1 : 0);
	}
	/*
	 * Ok, we are reading mail.
	 * Decide whether we are editing a mailbox or reading
	 * the system mailbox, and open up the right stuff.
	 */
	if (ef == NULL)
		ef = "%";
	else if (*ef == '@') {
		/*
		 * This must be treated specially to make invocation like
		 * -A imap -f @mailbox work.
		 */
		if ((cp = value("folder")) != NULL &&
				which_protocol(cp) == PROTO_IMAP)
			strncpy(mailname, cp, PATHSIZE)[PATHSIZE-1] = '\0';
	}
	i = setfile(ef, 0);
	if (i < 0)
		exit(1);		/* error already reported */
	if (existonly)
		exit(i);
	if (headersonly) {
		if (mb.mb_type == MB_IMAP)
			imap_getheaders(1, msgCount);
		for (i = 1; i <= msgCount; i++)
			printhead(i, stdout, 0);
		exit(exit_status);
	}
	callhook(mailname, 0);
	if (i > 0 && value("emptystart") == NULL)
		exit(1);
	if (sigsetjmp(hdrjmp, 1) == 0) {
		if ((prevint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
			safe_signal(SIGINT, hdrstop);
		if (Nflag == 0) {
			if (value("quiet") == NULL)
				printf(tr(140,
					"%s version %s.  Type ? for help.\n"),
					value("bsdcompat") ? "Mail" : uagent,
					version);
			announce(1);
			fflush(stdout);
		}
		safe_signal(SIGINT, prevint);
	}
	commands();
	if (mb.mb_type == MB_FILE || mb.mb_type == MB_MAILDIR) {
		safe_signal(SIGHUP, SIG_IGN);
		safe_signal(SIGINT, SIG_IGN);
		safe_signal(SIGQUIT, SIG_IGN);
	}
	strncpy(mboxname, expand("&"), sizeof mboxname)[sizeof mboxname-1]='\0';
	quit();
	return exit_status;
}

/*
 * Interrupt printing of the headers.
 */
/*ARGSUSED*/
static void 
hdrstop(int signo)
{
	(void)signo;

	fflush(stdout);
	fprintf(stderr, tr(141, "\nInterrupt\n"));
	siglongjmp(hdrjmp, 1);
}

/*
 * Compute what the screen size for printing headers should be.
 * We use the following algorithm for the height:
 *	If baud rate < 1200, use  9
 *	If baud rate = 1200, use 14
 *	If baud rate > 1200, use 24 or ws_row
 * Width is either 80 or ws_col;
 */
/*ARGSUSED*/
static void 
setscreensize(int dummy)
{
	struct termios tbuf;
#ifdef	TIOCGWINSZ
	struct winsize ws;
#endif
	speed_t ospeed;
	(void)dummy;

#ifdef	TIOCGWINSZ
	if (ioctl(1, TIOCGWINSZ, &ws) < 0)
		ws.ws_col = ws.ws_row = 0;
#endif
	if (tcgetattr(1, &tbuf) < 0)
		ospeed = B9600;
	else
		ospeed = cfgetospeed(&tbuf);
	if (ospeed < B1200)
		scrnheight = 9;
	else if (ospeed == B1200)
		scrnheight = 14;
#ifdef	TIOCGWINSZ
	else if (ws.ws_row != 0)
		scrnheight = ws.ws_row;
#endif
	else
		scrnheight = 24;
#ifdef	TIOCGWINSZ
	if ((realscreenheight = ws.ws_row) == 0)
		realscreenheight = 24;
#endif
#ifdef	TIOCGWINSZ
	if ((scrnwidth = ws.ws_col) == 0)
#endif
		scrnwidth = 80;
}
