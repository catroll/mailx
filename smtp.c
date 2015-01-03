/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ SMTP client.
 *@ TODO - use initial responses to save a round-trip (RFC 4954)
 *@ TODO - more (verbose) understanding+rection upon STATUS CODES
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 2000
 * Gunnar Ritter.  All rights reserved.
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
 *    This product includes software developed by Gunnar Ritter
 *    and his contributors.
 * 4. Neither the name of Gunnar Ritter nor the names of his contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GUNNAR RITTER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL GUNNAR RITTER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

EMPTY_FILE(smtp)
#ifdef HAVE_SMTP
#include <sys/socket.h>

#include <netdb.h>

#include <netinet/in.h>

#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

#undef NL
#undef LINE
#define NL        "\015\012"
#define LINE(X)   X NL

struct smtp_line {
   char     *dat;    /* Actual data */
   size_t   datlen;
   char     *buf;    /* Memory buffer */
   size_t   bufsize;
};

static sigjmp_buf _smtp_jmp;

static void    _smtp_onterm(int signo);

/* Get the SMTP server's answer, expecting val */
static int     _smtp_read(struct sock *sp, struct smtp_line *slp, int val,
                  bool_t ign_eof, bool_t want_dat);

/* Talk to a SMTP server */
static bool_t  _smtp_talk(struct sock *sp, struct sendbundle *sbp);

#ifdef HAVE_GSSAPI
static bool_t  _smtp_gssapi(struct sock *sp, struct sendbundle *sbp,
                  struct smtp_line *slp);
#endif

static void
_smtp_onterm(int signo)
{
   NYD_X; /* Signal handler */
   UNUSED(signo);
   siglongjmp(_smtp_jmp, 1);
}

static int
_smtp_read(struct sock *sp, struct smtp_line *slp, int val,
   bool_t ign_eof, bool_t want_dat)
{
   int rv, len;
   char *cp;
   NYD_ENTER;

   do {
      if ((len = sgetline(&slp->buf, &slp->bufsize, NULL, sp)) < 6) {
         if (len >= 0 && !ign_eof)
            fprintf(stderr, _("Unexpected EOF on SMTP connection\n"));
            rv = -1;
            goto jleave;
      }
      if (options & OPT_VERBVERB)
         fputs(slp->buf, stderr);
      switch (slp->buf[0]) {
      case '1':   rv = 1; break;
      case '2':   rv = 2; break;
      case '3':   rv = 3; break;
      case '4':   rv = 4; break;
      default:    rv = 5; break;
      }
      if (val != rv)
         fprintf(stderr, _("smtp-server: %s"), slp->buf);
   } while (slp->buf[3] == '-');

   if (want_dat) {
      for (cp = slp->buf; digitchar(*cp); --len, ++cp)
         ;
      for (; blankchar(*cp); --len, ++cp)
         ;
      slp->dat = cp;
      assert(len >= 2);
      len -= 2;
      cp[slp->datlen = (size_t)len] = '\0';
   }
jleave:
   NYD_LEAVE;
   return rv;
}

/* Indirect SMTP I/O */
#define _ANSWER(X, IGNEOF, WANTDAT) \
do if (!(options & OPT_DEBUG)) {\
   int y;\
   if ((y = _smtp_read(sp, slp, X, IGNEOF, WANTDAT)) != (X) &&\
         (!(IGNEOF) || y != -1))\
      goto jleave;\
} while (0)
#define _OUT(X) \
do {\
   if (options & OPT_VERBVERB)\
      fprintf(stderr, ">>> %s", X);\
   if (!(options & OPT_DEBUG))\
      swrite(sp, X);\
} while (0)

static bool_t
_smtp_talk(struct sock *sp, struct sendbundle *sbp)
{
   char o[LINESIZE], *hostname;
   struct smtp_line _sl, *slp = &_sl;
   struct str b64;
   struct name *n;
   size_t blen, cnt;
   bool_t inhdr = TRU1, inbcc = FAL0, rv = FAL0;
   NYD_ENTER;

   hostname = nodename(TRU1);
   slp->buf = NULL;
   slp->bufsize = 0;

   /* Read greeting */
   _ANSWER(2, FAL0, FAL0);

#ifdef HAVE_SSL
   if (!sp->s_use_ssl && xok_blook(smtp_use_starttls, &sbp->sb_url, OXM_ALL)) {
      snprintf(o, sizeof o, LINE("EHLO %s"), hostname);
      _OUT(o);
      _ANSWER(2, FAL0, FAL0);

      _OUT(LINE("STARTTLS"));
      _ANSWER(2, FAL0, FAL0);

      if (!(options & OPT_DEBUG) && ssl_open(&sbp->sb_url, sp) != OKAY)
         goto jleave;
   }
#else
   if (xok_blook(smtp_use_starttls, &sbp->sb_url, OXM_ALL)) {
      fprintf(stderr, _("No SSL support compiled in.\n"));
      goto jleave;
   }
#endif

   /* Shorthand: no authentication, plain HELO? */
   if (sbp->sb_ccred.cc_authtype == AUTHTYPE_NONE) {
      snprintf(o, sizeof o, LINE("HELO %s"), hostname);
      _OUT(o);
      _ANSWER(2, FAL0, FAL0);
      goto jsend;
   }

   /* We'll have to deal with authentication */
   snprintf(o, sizeof o, LINE("EHLO %s"), hostname);
   _OUT(o);
   _ANSWER(2, FAL0, FAL0);

   switch (sbp->sb_ccred.cc_authtype) {
   default:
      /* FALLTHRU (doesn't happen) */
   case AUTHTYPE_PLAIN:
      _OUT(LINE("AUTH PLAIN"));
      _ANSWER(3, FAL0, FAL0);

      snprintf(o, sizeof o, "%c%s%c%s",
         '\0', sbp->sb_ccred.cc_user.s, '\0', sbp->sb_ccred.cc_pass.s);
      b64_encode_buf(&b64, o,
         sbp->sb_ccred.cc_user.l + sbp->sb_ccred.cc_pass.l + 2,
         B64_SALLOC | B64_CRLF);
      _OUT(b64.s);
      _ANSWER(2, FAL0, FAL0);
      break;
   case AUTHTYPE_LOGIN:
      _OUT(LINE("AUTH LOGIN"));
      _ANSWER(3, FAL0, FAL0);

      b64_encode_cp(&b64, sbp->sb_ccred.cc_user.s, B64_SALLOC | B64_CRLF);
      _OUT(b64.s);
      _ANSWER(3, FAL0, FAL0);

      b64_encode_cp(&b64, sbp->sb_ccred.cc_pass.s, B64_SALLOC | B64_CRLF);
      _OUT(b64.s);
      _ANSWER(2, FAL0, FAL0);
      break;
#ifdef HAVE_MD5
   case AUTHTYPE_CRAM_MD5:
      _OUT(LINE("AUTH CRAM-MD5"));
      _ANSWER(3, FAL0, TRU1);
      {  char *cp = cram_md5_string(&sbp->sb_ccred.cc_user,
               &sbp->sb_ccred.cc_pass, slp->dat);
         _OUT(cp);
      }
      _ANSWER(2, FAL0, FAL0);
      break;
#endif
#ifdef HAVE_GSSAPI
   case AUTHTYPE_GSSAPI:
      if (!_smtp_gssapi(sp, sbp, slp))
         goto jleave;
      break;
#endif
   }

jsend:
   snprintf(o, sizeof o, LINE("MAIL FROM:<%s>"), sbp->sb_url.url_u_h.s);
   _OUT(o);
   _ANSWER(2, FAL0, FAL0);

   for (n = sbp->sb_to; n != NULL; n = n->n_flink) {
      if (!(n->n_type & GDEL)) {
         snprintf(o, sizeof o, LINE("RCPT TO:<%s>"), skinned_name(n));
         _OUT(o);
         _ANSWER(2, FAL0, FAL0);
      }
   }

   _OUT(LINE("DATA"));
   _ANSWER(3, FAL0, FAL0);

   fflush_rewind(sbp->sb_input);
   cnt = fsize(sbp->sb_input);
   while (fgetline(&slp->buf, &slp->bufsize, &cnt, &blen, sbp->sb_input, 1)
         != NULL) {
      if (inhdr) {
         if (*slp->buf == '\n')
            inhdr = inbcc = FAL0;
         else if (inbcc && blankchar(*slp->buf))
            continue;
         /* We know what we have generated first, so do not look for whitespace
          * before the ':' */
         else if (!ascncasecmp(slp->buf, "bcc: ", 5)) {
            inbcc = TRU1;
            continue;
         } else
            inbcc = FAL0;
      }

      if (*slp->buf == '.') {
         if (options & OPT_DEBUG)
            putc('.', stderr);
         else
            swrite1(sp, ".", 1, 1);
      }
      if (options & OPT_DEBUG) {
         fprintf(stderr, ">>> %s", slp->buf);
         continue;
      }
      slp->buf[blen - 1] = NL[0];
      slp->buf[blen] = NL[1];
      swrite1(sp, slp->buf, blen + 1, 1);
   }
   _OUT(LINE("."));
   _ANSWER(2, FAL0, FAL0);

   _OUT(LINE("QUIT"));
   _ANSWER(2, TRU1, FAL0);
   rv = TRU1;
jleave:
   if (slp->buf != NULL)
      free(slp->buf);
   NYD_LEAVE;
   return rv;
}

#ifdef HAVE_GSSAPI
# include "smtp_gssapi.h"
#endif

#undef _OUT
#undef _ANSWER

FL bool_t
smtp_mta(struct sendbundle *sbp)
{
   struct sock so;
   sighandler_type volatile saveterm;
   bool_t volatile rv = FAL0;
   NYD_ENTER;

   saveterm = safe_signal(SIGTERM, SIG_IGN);
   if (sigsetjmp(_smtp_jmp, 1))
      goto jleave;
   if (saveterm != SIG_IGN)
      safe_signal(SIGTERM, &_smtp_onterm);

   memset(&so, 0, sizeof so);
   if (!(options & OPT_DEBUG) && !sopen(&so, &sbp->sb_url))
      goto jleave;

   so.s_desc = "SMTP";
   rv = _smtp_talk(&so, sbp);

   if (!(options & OPT_DEBUG))
      sclose(&so);
jleave:
   safe_signal(SIGTERM, saveterm);
   NYD_LEAVE;
   return rv;
}

#undef LINE
#undef NL
#endif /* HAVE_SMTP */

/* s-it-mode */
