/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Auxiliary functions that don't fit anywhere else.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
#undef n_FILE
#define n_FILE auxlily

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <sys/utsname.h>

#ifdef HAVE_SOCKETS
# ifdef HAVE_GETADDRINFO
#  include <sys/socket.h>
# endif

# include <netdb.h>
#endif

#ifndef HAVE_POSIX_RANDOM
union rand_state {
   struct rand_arc4 {
      ui8_t    __pad[6];
      ui8_t    _i;
      ui8_t    _j;
      ui8_t    _dat[256];
   }        a;
   ui8_t    b8[sizeof(struct rand_arc4)];
   ui32_t   b32[sizeof(struct rand_arc4) / sizeof(ui32_t)];
};
#endif

#ifdef HAVE_ERRORS
struct a_aux_err_node{
   struct a_aux_err_node *ae_next;
   struct str ae_str;
};
#endif

#ifndef HAVE_POSIX_RANDOM
static union rand_state *_rand;
#endif

/* Error ring, for `errors' */
#ifdef HAVE_ERRORS
static struct a_aux_err_node *a_aux_err_head, *a_aux_err_tail;
static size_t a_aux_err_cnt, a_aux_err_cnt_noted;
#endif
static size_t a_aux_err_dirty;

/* Our ARC4 random generator with its completely unacademical pseudo
 * initialization (shall /dev/urandom fail) */
#ifndef HAVE_POSIX_RANDOM
static void    _rand_init(void);
static ui32_t  _rand_weak(ui32_t seed);
SINLINE ui8_t  _rand_get8(void);
#endif

#ifndef HAVE_POSIX_RANDOM
static void
_rand_init(void)
{
# ifdef HAVE_CLOCK_GETTIME
   struct timespec ts;
# else
   struct timeval ts;
# endif
   union {int fd; size_t i;} u;
   ui32_t seed, rnd;
   NYD2_ENTER;

   _rand = smalloc(sizeof *_rand);

   if ((u.fd = open("/dev/urandom", O_RDONLY)) != -1) {
      bool_t ok = (sizeof *_rand == (size_t)read(u.fd, _rand, sizeof *_rand));

      close(u.fd);
      if (ok)
         goto jleave;
   }

   for (seed = (uintptr_t)_rand & UI32_MAX, rnd = 21; rnd != 0; --rnd) {
      for (u.i = NELEM(_rand->b32); u.i-- != 0;) {
         size_t t, k;

# ifdef HAVE_CLOCK_GETTIME
         clock_gettime(CLOCK_REALTIME, &ts);
         t = (ui32_t)ts.tv_nsec;
# else
         gettimeofday(&ts, NULL);
         t = (ui32_t)ts.tv_usec;
# endif
         if (rnd & 1)
            t = (t >> 16) | (t << 16);
         _rand->b32[u.i] ^= _rand_weak(seed ^ t);
         _rand->b32[t % NELEM(_rand->b32)] ^= seed;
         if (rnd == 7 || rnd == 17)
            _rand->b32[u.i] ^= _rand_weak(seed ^ (ui32_t)ts.tv_sec);
         k = _rand->b32[u.i] % NELEM(_rand->b32);
         _rand->b32[k] ^= _rand->b32[u.i];
         seed ^= _rand_weak(_rand->b32[k]);
         if ((rnd & 3) == 3)
            seed ^= nextprime(seed);
      }
   }

   for (u.i = 5 * sizeof(_rand->b8); u.i != 0; --u.i)
      _rand_get8();
jleave:
   NYD2_LEAVE;
}

static ui32_t
_rand_weak(ui32_t seed)
{
   /* From "Random number generators: good ones are hard to find",
    * Park and Miller, Communications of the ACM, vol. 31, no. 10,
    * October 1988, p. 1195.
    * (In fact: FreeBSD 4.7, /usr/src/lib/libc/stdlib/random.c.) */
   ui32_t hi;

   if (seed == 0)
      seed = 123459876;
   hi =  seed /  127773;
         seed %= 127773;
   seed = (seed * 16807) - (hi * 2836);
   if ((si32_t)seed < 0)
      seed += SI32_MAX;
   return seed;
}

SINLINE ui8_t
_rand_get8(void)
{
   ui8_t si, sj;

   si = _rand->a._dat[++_rand->a._i];
   sj = _rand->a._dat[_rand->a._j += si];
   _rand->a._dat[_rand->a._i] = sj;
   _rand->a._dat[_rand->a._j] = si;
   return _rand->a._dat[(ui8_t)(si + sj)];
}
#endif /* HAVE_POSIX_RANDOM */

FL int
screensize(void)
{
   int s;
   char *cp;
   NYD_ENTER;

   if ((cp = ok_vlook(screen)) == NULL || (s = atoi(cp)) <= 0)
      s = scrnheight - 2; /* XXX no magics */
   NYD_LEAVE;
   return s;
}

FL char const *
n_pager_get(char const **env_addon)
{
   char const *cp;
   NYD_ENTER;

   cp = ok_vlook(PAGER);
   if (cp == NULL || *cp == '\0')
      cp = XPAGER;

   if (env_addon != NULL) {
      *env_addon = NULL;
      /* Update the manual upon any changes:
       *    *colour-pager*, $PAGER */
      if(strstr(rv, "less") != NULL){
         if(!env_blook("LESS", TRU1))
            *env_addon =
#ifdef HAVE_TERMCAP
                  (pstate & PS_TERMCAP_CA_MODE) ? "LESS=Ri"
                     : !(pstate & PS_TERMCAP_DISABLE) ? "LESS=FRi" :
#endif
                        "LESS=FRXi";
      }else if(strstr(rv, "lv") != NULL){
         if(!env_blook("LV", TRU1))
            *env_addon = "LV=-c";
      }
   }
   NYD_LEAVE;
   return cp;
}

FL void
page_or_print(FILE *fp, size_t lines)
{
   int c;
   char const *cp;
   NYD_ENTER;

   fflush_rewind(fp);

   if (n_source_may_yield_control() && (cp = ok_vlook(crt)) != NULL) {
      char *eptr;
      union {sl_i sli; size_t rows;} u;

      u.sli = strtol(cp, &eptr, 0);
      u.rows = (*cp != '\0' && *eptr == '\0')
            ? (size_t)u.sli : (size_t)scrnheight;

      if (u.rows > 0 && lines == 0) {
         while ((c = getc(fp)) != EOF)
            if (c == '\n' && ++lines >= u.rows)
               break;
         really_rewind(fp);
      }

      if (lines >= u.rows) {
         run_command(n_pager_get(NULL), 0, fileno(fp), COMMAND_FD_PASS,
            NULL, NULL, NULL, NULL);
         goto jleave;
      }
   }

   while ((c = getc(fp)) != EOF)
      putchar(c);
jleave:
   NYD_LEAVE;
}

FL enum protocol
which_protocol(char const *name) /* XXX (->URL (yet auxlily.c)) */
{
   struct stat st;
   char const *cp;
   char *np;
   size_t sz;
   enum protocol rv = PROTO_UNKNOWN;
   NYD_ENTER;

   temporary_protocol_ext = NULL;

   if (name[0] == '%' && name[1] == ':')
      name += 2;
   for (cp = name; *cp && *cp != ':'; cp++)
      if (!alnumchar(*cp))
         goto jfile;

   if (cp[0] == ':' && cp[1] == '/' && cp[2] == '/') {
      if (!strncmp(name, "pop3://", 7)) {
#ifdef HAVE_POP3
         rv = PROTO_POP3;
#else
         n_err(_("No POP3 support compiled in\n"));
#endif
      } else if (!strncmp(name, "pop3s://", 8)) {
#if defined HAVE_POP3 && defined HAVE_SSL
         rv = PROTO_POP3;
#else
# ifndef HAVE_POP3
         n_err(_("No POP3 support compiled in\n"));
# endif
# ifndef HAVE_SSL
         n_err(_("No SSL support compiled in\n"));
# endif
#endif
      }
      goto jleave;
   }

   /* TODO This is the de facto maildir code and thus belongs into there!
    * TODO and: we should have maildir:// and mbox:// pseudo-protos, instead of
    * TODO or (more likely) in addition to *newfolders*) */
jfile:
   rv = PROTO_FILE;
   np = ac_alloc((sz = strlen(name)) + 4 +1);
   memcpy(np, name, sz + 1);
   if (!stat(name, &st)) {
      if (S_ISDIR(st.st_mode) &&
            (memcpy(np+sz, "/tmp", 5), !stat(np, &st) && S_ISDIR(st.st_mode)) &&
            (memcpy(np+sz, "/new", 5), !stat(np, &st) && S_ISDIR(st.st_mode)) &&
            (memcpy(np+sz, "/cur", 5), !stat(np, &st) && S_ISDIR(st.st_mode)))
          rv = PROTO_MAILDIR;
   } else {
      if ((memcpy(np+sz, cp=".gz", 4), !stat(np, &st) && S_ISREG(st.st_mode)) ||
            (memcpy(np+sz, cp=".xz",4), !stat(np,&st) && S_ISREG(st.st_mode)) ||
            (memcpy(np+sz, cp=".bz2",5), !stat(np, &st) && S_ISREG(st.st_mode)))
         temporary_protocol_ext = cp;
      else if ((cp = ok_vlook(newfolders)) != NULL && !strcmp(cp, "maildir"))
         rv = PROTO_MAILDIR;
   }
   ac_free(np);
jleave:
   NYD_LEAVE;
   return rv;
}

FL ui32_t
torek_hash(char const *name)
{
   /* Chris Torek's hash.
    * NOTE: need to change *at least* mk-okey-map.pl when changing the
    * algorithm!! */
   ui32_t h = 0;
   NYD_ENTER;

   while (*name != '\0') {
      h *= 33;
      h += *name++;
   }
   NYD_LEAVE;
   return h;
}

FL unsigned
pjw(char const *cp) /* TODO obsolete that -> torek_hash */
{
   unsigned h = 0, g;
   NYD_ENTER;

   cp--;
   while (*++cp) {
      h = (h << 4 & 0xffffffff) + (*cp&0377);
      if ((g = h & 0xf0000000) != 0) {
         h = h ^ g >> 24;
         h = h ^ g;
      }
   }
   NYD_LEAVE;
   return h;
}

FL ui32_t
nextprime(ui32_t n)
{
   static ui32_t const primes[] = {
      5, 11, 23, 47, 97, 157, 283,
      509, 1021, 2039, 4093, 8191, 16381, 32749, 65521,
      131071, 262139, 524287, 1048573, 2097143, 4194301,
      8388593, 16777213, 33554393, 67108859, 134217689,
      268435399, 536870909, 1073741789, 2147483647
   };

   ui32_t i, mprime;
   NYD_ENTER;

   i = (n < primes[NELEM(primes) / 4] ? 0
         : (n < primes[NELEM(primes) / 2] ? NELEM(primes) / 4
         : NELEM(primes) / 2));
   do
      if ((mprime = primes[i]) > n)
         break;
   while (++i < NELEM(primes));
   if (i == NELEM(primes) && mprime < n)
      mprime = n;
   NYD_LEAVE;
   return mprime;
}

FL char *
getprompt(void) /* TODO evaluate only as necessary (needs a bit) PART OF UI! */
{ /* FIXME getprompt must mb->wc->mb+reset seq! */
   static char buf[PROMPT_BUFFER_SIZE];

   char *cp;
   char const *ccp_base, *ccp;
   size_t NATCH_CHAR( cclen_base COMMA cclen COMMA ) maxlen, dfmaxlen;
   bool_t trigger; /* 1.: `errors' ring note?  2.: first loop tick done */
   NYD_ENTER;

   /* No other place to place this */
#ifdef HAVE_ERRORS
   if (options & OPT_INTERACTIVE) {
      if (!(pstate & PS_ERRORS_NOTED) && a_aux_err_head != NULL) {
         pstate |= PS_ERRORS_NOTED;
         fprintf(stderr, _("There are new messages in the error message ring "
               "(denoted by \"#ERR#\")\n"
            "  The `errors' command manages this message ring\n"));
      }

      if ((trigger = (a_aux_err_cnt_noted != a_aux_err_cnt)))
         a_aux_err_cnt_noted = a_aux_err_cnt;
   } else
      trigger = FAL0;
#endif

   cp = buf;
   if ((ccp_base = ok_vlook(prompt)) == NULL || *ccp_base == '\0') {
#ifdef HAVE_ERRORS
      if (trigger)
         ccp_base = "";
      else
#endif
         goto jleave;
   }
#ifdef HAVE_ERRORS
   if (trigger)
      ccp_base = savecatsep(_("#ERR#"), '\0', ccp_base);
#endif
   NATCH_CHAR( cclen_base = strlen(ccp_base); )

   dfmaxlen = 0; /* keep CC happy */
   trigger = FAL0;
jredo:
   ccp = ccp_base;
   NATCH_CHAR( cclen = cclen_base; )
   maxlen = sizeof(buf) -1;

   for (;;) {
      size_t l;
      int c;

      if (maxlen == 0)
         goto jleave;
#ifdef HAVE_NATCH_CHAR
      c = mblen(ccp, cclen); /* TODO use mbrtowc() */
      if (c <= 0) {
         mblen(NULL, 0);
         if (c < 0) {
            *buf = '?';
            cp = buf + 1;
            goto jleave;
         }
         break;
      } else if ((l = c) > 1) {
         if (trigger) {
            memcpy(cp, ccp, l);
            cp += l;
         }
         ccp += l;
         maxlen -= l;
         continue;
      } else
#endif
      if ((c = n_shell_expand_escape(&ccp, TRU1)) > 0) {
            if (trigger)
               *cp++ = (char)c;
            --maxlen;
            continue;
      }
      if (c == 0 || c == PROMPT_STOP)
         break;

      if (trigger) {
         char const *a = (c == PROMPT_DOLLAR) ? account_name : displayname;
         if (a == NULL)
            a = "";
         if ((l = field_put_bidi_clip(cp, dfmaxlen, a, strlen(a))) > 0) {
            cp += l;
            maxlen -= l;
            dfmaxlen -= l;
         }
      }
   }

   if (!trigger) {
      trigger = TRU1;
      dfmaxlen = maxlen;
      goto jredo;
   }
jleave:
   *cp = '\0';
   NYD_LEAVE;
   return buf;
}

FL char *
nodename(int mayoverride)
{
   static char *sys_hostname, *hostname; /* XXX free-at-exit */

   struct utsname ut;
   char *hn;
#ifdef HAVE_SOCKETS
# ifdef HAVE_GETADDRINFO
   struct addrinfo hints, *res;
# else
   struct hostent *hent;
# endif
#endif
   NYD_ENTER;

   if (mayoverride && (hn = ok_vlook(hostname)) != NULL && *hn != '\0') {
      ;
   } else if ((hn = sys_hostname) == NULL) {
      uname(&ut);
      hn = ut.nodename;
#ifdef HAVE_SOCKETS
# ifdef HAVE_GETADDRINFO
      memset(&hints, 0, sizeof hints);
      hints.ai_family = AF_UNSPEC;
      hints.ai_flags = AI_CANONNAME;
      if (getaddrinfo(hn, NULL, &hints, &res) == 0) {
         if (res->ai_canonname != NULL) {
            size_t l = strlen(res->ai_canonname) +1;

            hn = ac_alloc(l);
            memcpy(hn, res->ai_canonname, l);
         }
         freeaddrinfo(res);
      }
# else
      hent = gethostbyname(hn);
      if (hent != NULL)
         hn = hent->h_name;
# endif
#endif
      sys_hostname = sstrdup(hn);
#if defined HAVE_SOCKETS && defined HAVE_GETADDRINFO
      if (hn != ut.nodename)
         ac_free(hn);
#endif
      hn = sys_hostname;
   }

   if (hostname != NULL && hostname != sys_hostname)
      free(hostname);
   hostname = sstrdup(hn);
   NYD_LEAVE;
   return hostname;
}

FL char *
getrandstring(size_t length)
{
   struct str b64;
   char *data;
   size_t i;
   NYD_ENTER;

#ifndef HAVE_POSIX_RANDOM
   if (_rand == NULL)
      _rand_init();
#endif

   /* We use our base64 encoder with _NOPAD set, so ensure the encoded result
    * with PAD stripped is still longer than what the user requests, easy way */
   data = ac_alloc(i = length + 3);

#ifndef HAVE_POSIX_RANDOM
   while (i-- > 0)
      data[i] = (char)_rand_get8();
#else
   {  char *cp = data;

      while (i > 0) {
         union {ui32_t i4; char c[4];} r;
         size_t j;

         r.i4 = (ui32_t)arc4random();
         switch ((j = i & 3)) {
         case 0:  cp[3] = r.c[3]; j = 4;
         case 3:  cp[2] = r.c[2];
         case 2:  cp[1] = r.c[1];
         default: cp[0] = r.c[0]; break;
         }
         cp += j;
         i -= j;
      }
   }
#endif

   b64_encode_buf(&b64, data, length + 3,
      B64_SALLOC | B64_RFC4648URL | B64_NOPAD);
   ac_free(data);

   assert(b64.l >= length);
   b64.s[length] = '\0';
   NYD_LEAVE;
   return b64.s;
}

FL si8_t
boolify(char const *inbuf, uiz_t inlen, si8_t emptyrv)
{
   char *dat, *eptr;
   sl_i sli;
   si8_t rv;
   NYD_ENTER;

   assert(inlen == 0 || inbuf != NULL);

   if (inlen == UIZ_MAX)
      inlen = strlen(inbuf);

   if (inlen == 0)
      rv = (emptyrv >= 0) ? (emptyrv == 0 ? 0 : 1) : -1;
   else {
      if ((inlen == 1 && *inbuf == '1') ||
            !ascncasecmp(inbuf, "true", inlen) ||
            !ascncasecmp(inbuf, "yes", inlen) ||
            !ascncasecmp(inbuf, "on", inlen))
         rv = 1;
      else if ((inlen == 1 && *inbuf == '0') ||
            !ascncasecmp(inbuf, "false", inlen) ||
            !ascncasecmp(inbuf, "no", inlen) ||
            !ascncasecmp(inbuf, "off", inlen))
         rv = 0;
      else {
         dat = ac_alloc(inlen +1);
         memcpy(dat, inbuf, inlen);
         dat[inlen] = '\0';

         sli = strtol(dat, &eptr, 0);
         if (*dat != '\0' && *eptr == '\0')
            rv = (sli != 0);
         else
            rv = -1;

         ac_free(dat);
      }
   }
   NYD_LEAVE;
   return rv;
}

FL si8_t
quadify(char const *inbuf, uiz_t inlen, char const *prompt, si8_t emptyrv)
{
   si8_t rv;
   NYD_ENTER;

   assert(inlen == 0 || inbuf != NULL);

   if (inlen == UIZ_MAX)
      inlen = strlen(inbuf);

   if (inlen == 0)
      rv = (emptyrv >= 0) ? (emptyrv == 0 ? 0 : 1) : -1;
   else if ((rv = boolify(inbuf, inlen, -1)) < 0 &&
         !ascncasecmp(inbuf, "ask-", 4) &&
         (rv = boolify(inbuf + 4, inlen - 4, -1)) >= 0 &&
         (options & OPT_INTERACTIVE))
      rv = getapproval(prompt, rv);
   NYD_LEAVE;
   return rv;
}

FL bool_t
n_is_all_or_aster(char const *name){
   bool_t rv;
   NYD_ENTER;

   rv = ((name[0] == '*' && name[1] == '\0') || !asccasecmp(name, "all"));
   NYD_LEAVE;
   return rv;
}

FL time_t
n_time_epoch(void)
{
#ifdef HAVE_CLOCK_GETTIME
   struct timespec ts;
#elif defined HAVE_GETTIMEOFDAY
   struct timeval ts;
#endif
   time_t rv;
   NYD2_ENTER;

#ifdef HAVE_CLOCK_GETTIME
   clock_gettime(CLOCK_REALTIME, &ts);
   rv = (time_t)ts.tv_sec;
#elif defined HAVE_GETTIMEOFDAY
   gettimeofday(&ts, NULL);
   rv = (time_t)ts.tv_sec;
#else
   rv = time(NULL);
#endif
   NYD2_LEAVE;
   return rv;
}

FL void
time_current_update(struct time_current *tc, bool_t full_update)
{
   NYD_ENTER;
   tc->tc_time = n_time_epoch();
   if (full_update) {
      memcpy(&tc->tc_gm, gmtime(&tc->tc_time), sizeof tc->tc_gm);
      memcpy(&tc->tc_local, localtime(&tc->tc_time), sizeof tc->tc_local);
      sstpcpy(tc->tc_ctime, ctime(&tc->tc_time));
   }
   NYD_LEAVE;
}

FL uiz_t
n_msleep(uiz_t millis, bool_t ignint){
   uiz_t rv;
   NYD2_ENTER;

#ifdef HAVE_NANOSLEEP
   /* C99 */{
      struct timespec ts, trem;
      int i;

      ts.tv_sec = millis / 1000;
      ts.tv_nsec = (millis %= 1000) * 1000 * 1000;

      while((i = nanosleep(&ts, &trem)) != 0 && ignint)
         ts = trem;
      rv = (i == 0) ? 0 : (trem.tv_sec * 1000) + (trem.tv_nsec / (1000 * 1000));
   }

#elif defined HAVE_SLEEP
   if((millis /= 1000) == 0)
      millis = 1;
   while((rv = sleep((unsigned int)millis)) != 0 && ignint)
      millis = rv;
#else
# error Configuration should have detected a function for sleeping.
#endif

   NYD2_LEAVE;
   return rv;
}

FL void
n_err(char const *format, ...){
   va_list ap;
   NYD2_ENTER;

   va_start(ap, format);
#ifdef HAVE_ERRORS
   if(options & OPT_INTERACTIVE)
      n_verr(format, ap);
   else
#endif
   {
      if(a_aux_err_dirty++ == 0)
         fputs(UAGENT ": ", stderr);
      vfprintf(stderr, format, ap);
      if(strchr(format, '\n') != NULL){ /* TODO */
         a_aux_err_dirty = 0;
         fflush(stderr);
      }
   }
   va_end(ap);
   NYD2_LEAVE;
}

FL void
n_verr(char const *format, va_list ap){
   /* Check use cases of PS_ERRORS_NOTED, too! */
#ifdef HAVE_ERRORS
   char buf[LINESIZE], *xbuf;
   int lmax, l;
   struct a_aux_err_node *enp;

   LCTA(ERRORS_MAX > 3);
#endif
   NYD2_ENTER;

   if(a_aux_err_dirty++ == 0)
      fputs(UAGENT ": ", stderr);

#ifdef HAVE_ERRORS
   if(!(options & OPT_INTERACTIVE))
#endif
   {
      vfprintf(stderr, format, ap);
      goto jleave;
   }

#ifdef HAVE_ERRORS
   xbuf = buf;
   lmax = sizeof buf;
jredo:
   l = vsnprintf(xbuf, lmax, format, ap);
   if (l <= 0)
      goto jleave;
   if (UICMP(z, l, >=, lmax)) {
      /* FIXME Cannot reuse va_list
      lmax = ++l;
      xbuf = srealloc((xbuf == buf ? NULL : xbuf), lmax);
      goto jredo;
      */
   }

   fwrite(xbuf, 1, l, stderr);

   /* Link it into the `errors' message ring */
   if((enp = a_aux_err_tail) == NULL){
jcreat:
      enp = scalloc(1, sizeof *enp);
      if(a_aux_err_tail != NULL)
         a_aux_err_tail->ae_next = enp;
      else
         a_aux_err_head = enp;
      a_aux_err_tail = enp;
      ++a_aux_err_cnt;
   }else if(enp->ae_str.l > 0 && enp->ae_str.s[enp->ae_str.l - 1] == '\n'){
      if(a_aux_err_cnt < ERRORS_MAX)
         goto jcreat;

      a_aux_err_head = (enp = a_aux_err_head)->ae_next;
      a_aux_err_tail->ae_next = enp;
      a_aux_err_tail = enp;
      free(enp->ae_str.s);
      memset(enp, 0, sizeof *enp);
   }

   n_str_add_buf(&enp->ae_str, xbuf, l);

   if(xbuf != buf)
      free(xbuf);
#endif /* HAVE_ERRORS */

jleave:
   /* If the format ends with newline, be clean again */
   /* C99 */{
      size_t i = strlen(format);

      if(i > 0 && format[i - 1] == '\n'){
         fflush(stderr);
         a_aux_err_dirty = 0;
      }
   }
   NYD2_LEAVE;
}

FL void
n_err_sighdl(char const *format, ...){ /* TODO sigsafe; obsolete! */
   va_list ap;
   NYD_X;

   va_start(ap, format);
   vfprintf(stderr, format, ap);
   va_end(ap);
   fflush(stderr);
}

FL void
n_perr(char const *msg, int errval){
   char const *fmt;
   NYD2_ENTER;

   if(msg == NULL){
      fmt = "%s%s\n";
      msg = "";
   }else
      fmt = "%s: %s\n";

   if(errval == 0)
      errval = errno;

   n_err(fmt, msg, strerror(errval));
   NYD2_LEAVE;
}

FL void
n_alert(char const *format, ...){
   va_list ap;
   NYD2_ENTER;

   n_err(a_aux_err_dirty > 0 ? _("\nAlert: ") : _("Alert: "));

   va_start(ap, format);
   n_verr(format, ap);
   va_end(ap);

   n_err("\n");
   NYD2_LEAVE;
}

FL void
n_panic(char const *format, ...){
   va_list ap;
   NYD2_ENTER;

   if(a_aux_err_dirty > 0){
      putc('\n', stderr);
      a_aux_err_dirty = 0;
   }
   fprintf(stderr, UAGENT ": Panic: ");

   va_start(ap, format);
   vfprintf(stderr, format, ap);
   va_end(ap);

   putc('\n', stderr);
   fflush(stderr);
   NYD2_LEAVE;
   abort(); /* Was exit(EXIT_ERR); for a while, but no */
}

#ifdef HAVE_ERRORS
FL int
c_errors(void *v){
   char **argv = v;
   struct a_aux_err_node *enp;
   NYD_ENTER;

   if(*argv == NULL)
      goto jlist;
   if(argv[1] != NULL)
      goto jerr;
   if(!asccasecmp(*argv, "show"))
      goto jlist;
   if(!asccasecmp(*argv, "clear"))
      goto jclear;
jerr:
   fprintf(stderr, _("Synopsis: errors: (<show> or) <clear> the error ring\n"));
   v = NULL;
jleave:
   NYD_LEAVE;
   return v == NULL ? !STOP : !OKAY; /* xxx 1:bad 0:good -- do some */

jlist:{
      FILE *fp;
      size_t i;

      if(a_aux_err_head == NULL){
         fprintf(stderr, _("The error ring is empty\n"));
         goto jleave;
      }

      if((fp = Ftmp(NULL, "errors", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==
            NULL){
         fprintf(stderr, _("tmpfile"));
         v = NULL;
         goto jleave;
      }

      for(i = 0, enp = a_aux_err_head; enp != NULL; enp = enp->ae_next)
         fprintf(fp, "- %4" PRIuZ ". %" PRIuZ " bytes: %s",
            ++i, enp->ae_str.l, enp->ae_str.s);
      /* We don't know wether last string ended with NL; be simple */
      putc('\n', fp);

      page_or_print(fp, 0);
      Fclose(fp);
   }
   /* FALLTHRU */

jclear:
   a_aux_err_tail = NULL;
   a_aux_err_cnt = a_aux_err_cnt_noted = 0;
   while((enp = a_aux_err_head) != NULL){
      a_aux_err_head = enp->ae_next;
      free(enp->ae_str.s);
      free(enp);
   }
   goto jleave;
}
#endif /* HAVE_ERRORS */

/* s-it-mode */
