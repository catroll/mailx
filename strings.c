/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Auto-reclaimed string allocation and support routines that build on top of
 *@ them.  Strings handed out by those are reclaimed at the top of the command
 *@ loop each time, so they need not be freed.
 *@ And below this series we do collect all other plain string support routines
 *@ in here, including those which use normal heap memory.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 * The Regents of the University of California.  All rights reserved.
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
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <ctype.h>

/* Allocate SBUFFER_SIZE chunks and keep them in a singly linked list, but
 * release all except the first two in sreset(), because other allocations are
 * performed and the underlaying allocator should have the possibility to
 * reorder stuff and possibly even madvise(2), so that S-nail(1) integrates
 * neatly into the system.
 * To relax stuff further, especially in non-interactive, i.e., send mode, do
 * not even allocate the first buffer, but let that be a builtin DATA section
 * one that is rather small, yet sufficient for send mode to *never* even
 * perform a single dynamic allocation (from our stringdope point of view).
 * Encapsulate user chunks with canaries if HAVE_DEBUG */

#ifdef HAVE_DEBUG
# define _SHOPE_SIZE       (2u * 8 * sizeof(char) + sizeof(struct schunk))

CTA(sizeof(char) == sizeof(ui8_t));

struct schunk {
   char const     *file;
   ui32_t         line;
   ui16_t         usr_size;
   ui16_t         full_size;
};

union sptr {
   void           *p;
   struct schunk  *c;
   char           *cp;
   ui8_t          *ui8p;
};
#endif /* HAVE_DEBUG */

union __align__ {
   char     *cp;
   size_t   sz;
   ul_it    ul;
};
#define SALIGN    (sizeof(union __align__) - 1)

CTA(ISPOW2(SALIGN + 1));

struct b_base {
   struct buffer  *_next;
   char           *_bot;      /* For spreserve() */
   char           *_relax;    /* If !NULL, used by srelax() instead of ._bot */
   char           *_max;      /* Max usable byte */
   char           *_caster;   /* NULL if full */
};

/* Single instance builtin buffer, DATA */
struct b_bltin {
   struct b_base  b_base;
   char           b_buf[SBUFFER_BUILTIN - sizeof(struct b_base)];
};
#define SBLTIN_SIZE  SIZEOF_FIELD(struct b_bltin, b_buf)

/* Dynamically allocated buffers */
struct b_dyn {
   struct b_base  b_base;
   char           b_buf[SBUFFER_SIZE - sizeof(struct b_base)];
};
#define SDYN_SIZE SIZEOF_FIELD(struct b_dyn, b_buf)

struct buffer {
   struct b_base  b;
   char           b_buf[VFIELD_SIZE(SALIGN + 1)];
};

static struct b_bltin   _builtin_buf;
static struct buffer    *_buf_head, *_buf_list, *_buf_server, *_buf_relax;
#ifdef HAVE_DEBUG
static size_t           _all_cnt, _all_cycnt, _all_cycnt_max,
                        _all_size, _all_cysize, _all_cysize_max, _all_min,
                           _all_max, _all_wast,
                        _all_bufcnt, _all_cybufcnt, _all_cybufcnt_max,
                        _all_resetreqs, _all_resets;
#endif

/* sreset() / srelax() release a buffer, check the canaries of all chunks */
#ifdef HAVE_DEBUG
static void    _salloc_bcheck(struct buffer *b);
#endif

#ifdef HAVE_DEBUG
static void
_salloc_bcheck(struct buffer *b)
{
   union sptr pmax, pp;
   /*NYD_ENTER;*/

   pmax.cp = (b->b._caster == NULL) ? b->b._max : b->b._caster;
   pp.cp = b->b._bot;

   while (pp.cp < pmax.cp) {
      struct schunk *c;
      union sptr x;
      void *ux;
      ui8_t i;

      c = pp.c;
      pp.cp += c->full_size;
      x.p = c + 1;
      ux = x.cp + 8;

      i = 0;
      if (x.ui8p[0] != 0xDE) i |= 1<<0;
      if (x.ui8p[1] != 0xAA) i |= 1<<1;
      if (x.ui8p[2] != 0x55) i |= 1<<2;
      if (x.ui8p[3] != 0xAD) i |= 1<<3;
      if (x.ui8p[4] != 0xBE) i |= 1<<4;
      if (x.ui8p[5] != 0x55) i |= 1<<5;
      if (x.ui8p[6] != 0xAA) i |= 1<<6;
      if (x.ui8p[7] != 0xEF) i |= 1<<7;
      if (i != 0)
         alert("sdope %p: corrupt lower canary: 0x%02X, size %u: %s, line %u",
            ux, i, c->usr_size, c->file, c->line);
      x.cp += 8 + c->usr_size;

      i = 0;
      if (x.ui8p[0] != 0xDE) i |= 1<<0;
      if (x.ui8p[1] != 0xAA) i |= 1<<1;
      if (x.ui8p[2] != 0x55) i |= 1<<2;
      if (x.ui8p[3] != 0xAD) i |= 1<<3;
      if (x.ui8p[4] != 0xBE) i |= 1<<4;
      if (x.ui8p[5] != 0x55) i |= 1<<5;
      if (x.ui8p[6] != 0xAA) i |= 1<<6;
      if (x.ui8p[7] != 0xEF) i |= 1<<7;
      if (i != 0)
         alert("sdope %p: corrupt upper canary: 0x%02X, size %u: %s, line %u",
            ux, i, c->usr_size, c->file, c->line);
   }
   /*NYD_LEAVE;*/
}
#endif

FL void *
(salloc)(size_t size SALLOC_DEBUG_ARGS)
{
   DBG( size_t orig_size = size; )
   union {struct buffer *b; char *cp;} u;
   char *x, *y, *z;
   NYD_ENTER;

   if (size == 0)
      ++size;
   size += SALIGN;
   size &= ~SALIGN;

#ifdef HAVE_DEBUG
   ++_all_cnt;
   ++_all_cycnt;
   _all_cycnt_max = MAX(_all_cycnt_max, _all_cycnt);
   _all_size += size;
   _all_cysize += size;
   _all_cysize_max = MAX(_all_cysize_max, _all_cysize);
   _all_min = (_all_max == 0) ? size : MIN(_all_min, size);
   _all_max = MAX(_all_max, size);
   _all_wast += size - orig_size;

   size += _SHOPE_SIZE;

   if (size >= 2048)
      alert("salloc() of %" ZFMT " bytes from `%s', line %u\n",
         size, mdbg_file, mdbg_line);
#endif

   /* Search for a buffer with enough free space to serve request */
   if ((u.b = _buf_server) != NULL)
      goto jumpin;
jredo:
   for (u.b = _buf_head; u.b != NULL; u.b = u.b->b._next) {
jumpin:
      x = u.b->b._caster;
      if (x == NULL) {
         if (u.b == _buf_server) {
            if (u.b == _buf_head && (u.b = _buf_head->b._next) != NULL) {
               _buf_server = u.b;
               goto jumpin;
            }
            _buf_server = NULL;
            goto jredo;
         }
         continue;
      }
      y = x + size;
      z = u.b->b._max;
      if (PTRCMP(y, <=, z)) {
         /* Alignment is the one thing, the other is what is usually allocated,
          * and here about 40 bytes seems to be a good cut to avoid non-usable
          * non-NULL casters.  However, because of _salloc_bcheck(), we may not
          * set ._caster to NULL because then it would check all chunks up to
          * ._max, which surely doesn't work; speed is no issue with DEBUG */
         u.b->b._caster = NDBG( PTRCMP(y + 42 + 16, >=, z) ? NULL : ) y;
         u.cp = x;
         goto jleave;
      }
   }

   /* Need a new buffer */
   if (_buf_head == NULL) {
      struct b_bltin *b = &_builtin_buf;
      b->b_base._max = b->b_buf + sizeof(b->b_buf) - 1;
      _buf_head = (struct buffer*)b;
      u.b = _buf_head;
   } else {
#ifdef HAVE_DEBUG
      ++_all_bufcnt;
      ++_all_cybufcnt;
      _all_cybufcnt_max = MAX(_all_cybufcnt_max, _all_cybufcnt);
#endif
      u.b = smalloc(sizeof(struct b_dyn));
      u.b->b._max = u.b->b_buf + SDYN_SIZE - 1;
   }
   if (_buf_list != NULL)
      _buf_list->b._next = u.b;
   _buf_server = _buf_list = u.b;
   u.b->b._next = NULL;
   u.b->b._caster = (u.b->b._bot = u.b->b_buf) + size;
   u.b->b._relax = NULL;
   u.cp = u.b->b._bot;

jleave:
   /* Encapsulate user chunk in debug canaries */
#ifdef HAVE_DEBUG
   {
      union sptr xl, xu;
      struct schunk *xc;

      xl.p = u.cp;
      xc = xl.c;
      xc->file = mdbg_file;
      xc->line = mdbg_line;
      xc->usr_size = (ui16_t)orig_size;
      xc->full_size = (ui16_t)size;
      xl.p = xc + 1;
      xl.ui8p[0]=0xDE; xl.ui8p[1]=0xAA; xl.ui8p[2]=0x55; xl.ui8p[3]=0xAD;
      xl.ui8p[4]=0xBE; xl.ui8p[5]=0x55; xl.ui8p[6]=0xAA; xl.ui8p[7]=0xEF;
      u.cp = xl.cp + 8;
      xu.p = u.cp;
      xu.cp += orig_size;
      xu.ui8p[0]=0xDE; xu.ui8p[1]=0xAA; xu.ui8p[2]=0x55; xu.ui8p[3]=0xAD;
      xu.ui8p[4]=0xBE; xu.ui8p[5]=0x55; xu.ui8p[6]=0xAA; xu.ui8p[7]=0xEF;
   }
#endif
   NYD_LEAVE;
   return u.cp;
}

FL void *
(csalloc)(size_t nmemb, size_t size SALLOC_DEBUG_ARGS)
{
   void *vp;
   NYD_ENTER;

   size *= nmemb;
   vp = (salloc)(size SALLOC_DEBUG_ARGSCALL);
   memset(vp, 0, size);
   NYD_LEAVE;
   return vp;
}

FL void
sreset(bool_t only_if_relaxed)
{
   struct buffer *bh;
   NYD_ENTER;

   DBG( ++_all_resetreqs; )
   if (noreset || (only_if_relaxed && _buf_relax == NULL))
      goto jleave;

#ifdef HAVE_DEBUG
   _all_cycnt = _all_cysize = 0;
   _all_cybufcnt = (_buf_head != NULL && _buf_head->b._next != NULL);
   ++_all_resets;
#endif

   if ((bh = _buf_head) != NULL) {
      struct buffer *b = bh;
      DBG( _salloc_bcheck(b); )
      b->b._caster = b->b._bot;
      b->b._relax = NULL;
      DBG( memset(b->b._caster, 0377, PTR2SIZE(b->b._max - b->b._caster)); )
      _buf_server = b;

      if ((bh = bh->b._next) != NULL) {
         b = bh;
         DBG( _salloc_bcheck(b); )
         b->b._caster = b->b._bot;
         b->b._relax = NULL;
         DBG( memset(b->b._caster, 0377, PTR2SIZE(b->b._max - b->b._caster)); )

         for (bh = bh->b._next; bh != NULL;) {
            struct buffer *b2 = bh->b._next;
            DBG( _salloc_bcheck(bh); )
            free(bh);
            bh = b2;
         }
      }
      _buf_list = b;
      b->b._next = NULL;
      _buf_relax = NULL;
   }

   DBG( smemreset(); )
jleave:
   NYD_LEAVE;
}

FL void
srelax_hold(void)
{
   struct buffer *b;
   NYD_ENTER;

   assert(_buf_relax == NULL);

   for (b = _buf_head; b != NULL; b = b->b._next)
      b->b._relax = b->b._caster;
   _buf_relax = _buf_server;
   assert(_buf_relax != NULL);
   NYD_LEAVE;
}

FL void
srelax_rele(void)
{
   struct buffer *b;
   NYD_ENTER;

   assert(_buf_relax != NULL);

   for (b = _buf_relax; b != NULL; b = b->b._next) {
      DBG( _salloc_bcheck(b); )
      b->b._caster = (b->b._relax != NULL) ? b->b._relax : b->b._bot;
      b->b._relax = NULL;
   }
   _buf_relax = NULL;
   NYD_LEAVE;
}

FL void
srelax(void)
{
   /* The purpose of relaxation is only that it is possible to reset the
    * casters, *not* to give back memory to the system.  We are presumably in
    * an iteration over all messages of a mailbox, and it'd be quite
    * counterproductive to give the system allocator a chance to waste time */
   struct buffer *b;
   NYD_ENTER;

   assert(_buf_relax != NULL);

   for (b = _buf_relax; b != NULL; b = b->b._next) {
      DBG( _salloc_bcheck(b); )
      b->b._caster = (b->b._relax != NULL) ? b->b._relax : b->b._bot;
      DBG( memset(b->b._caster, 0377, PTR2SIZE(b->b._max - b->b._caster)); )
   }
   NYD_LEAVE;
}

FL void
spreserve(void)
{
   struct buffer *b;
   NYD_ENTER;

   for (b = _buf_head; b != NULL; b = b->b._next)
      b->b._bot = b->b._caster;
   NYD_LEAVE;
}

#ifdef HAVE_DEBUG
FL int
c_sstats(void *v)
{
   size_t excess;
   NYD_ENTER;
   UNUSED(v);

   excess = (_all_cybufcnt_max * SDYN_SIZE) + SBLTIN_SIZE;
   excess = (excess >= _all_cysize_max) ? 0 : _all_cysize_max - excess;

   printf("String usage statistics (cycle means one sreset() cycle):\n"
      "  Buffer allocs ever/max simultan. : %lu/%lu\n"
      "  Buffer size of builtin(1)/dynamic: %lu/%lu\n"
      "  Overall alloc count/bytes        : %lu/%lu\n"
      "  Alloc bytes min/max/align wastage: %lu/%lu/%lu\n"
      "  sreset() cycles                  : %lu (%lu performed)\n"
      "  Cycle maximums: alloc count/bytes: %lu/%lu+%lu\n",
      (ul_it)_all_bufcnt, (ul_it)_all_cybufcnt_max,
      (ul_it)SBLTIN_SIZE, (ul_it)SDYN_SIZE,
      (ul_it)_all_cnt, (ul_it)_all_size,
      (ul_it)_all_min, (ul_it)_all_max, (ul_it)_all_wast,
      (ul_it)_all_resetreqs, (ul_it)_all_resets,
      (ul_it)_all_cycnt_max, (ul_it)_all_cysize_max, (ul_it)excess);
   NYD_LEAVE;
   return 0;
}
#endif

FL char *
(savestr)(char const *str SALLOC_DEBUG_ARGS)
{
   size_t size;
   char *news;
   NYD_ENTER;

   size = strlen(str) +1;
   news = (salloc)(size SALLOC_DEBUG_ARGSCALL);
   memcpy(news, str, size);
   NYD_LEAVE;
   return news;
}

FL char *
(savestrbuf)(char const *sbuf, size_t sbuf_len SALLOC_DEBUG_ARGS)
{
   char *news;
   NYD_ENTER;

   news = (salloc)(sbuf_len +1 SALLOC_DEBUG_ARGSCALL);
   memcpy(news, sbuf, sbuf_len);
   news[sbuf_len] = 0;
   NYD_LEAVE;
   return news;
}

FL char *
(save2str)(char const *str, char const *old SALLOC_DEBUG_ARGS)
{
   size_t newsize, oldsize;
   char *news;
   NYD_ENTER;

   newsize = strlen(str) +1;
   oldsize = (old != NULL) ? strlen(old) + 1 : 0;
   news = (salloc)(newsize + oldsize SALLOC_DEBUG_ARGSCALL);
   if (oldsize) {
      memcpy(news, old, oldsize);
      news[oldsize - 1] = ' ';
   }
   memcpy(news + oldsize, str, newsize);
   NYD_LEAVE;
   return news;
}

FL char *
(savecat)(char const *s1, char const *s2 SALLOC_DEBUG_ARGS)
{
   size_t l1, l2;
   char *news;
   NYD_ENTER;

   l1 = strlen(s1);
   l2 = strlen(s2);
   news = (salloc)(l1 + l2 +1 SALLOC_DEBUG_ARGSCALL);
   memcpy(news + 0, s1, l1);
   memcpy(news + l1, s2, l2);
   news[l1 + l2] = '\0';
   NYD_LEAVE;
   return news;
}

/*
 * Support routines, auto-reclaimed storage
 */

FL char *
(i_strdup)(char const *src SALLOC_DEBUG_ARGS)
{
   size_t sz;
   char *dest;
   NYD_ENTER;

   sz = strlen(src) +1;
   dest = (salloc)(sz SALLOC_DEBUG_ARGSCALL);
   i_strcpy(dest, src, sz);
   NYD_LEAVE;
   return dest;
}

FL char *
(protbase)(char const *cp SALLOC_DEBUG_ARGS) /* TODO obsolete */
{
   char *n, *np;
   NYD_ENTER;

   np = n = (salloc)(strlen(cp) +1 SALLOC_DEBUG_ARGSCALL);

   /* Just ignore the `is-system-mailbox' prefix XXX */
   if (cp[0] == '%' && cp[1] == ':')
      cp += 2;

   while (*cp != '\0') {
      if (cp[0] == ':' && cp[1] == '/' && cp[2] == '/') {
         *np++ = *cp++;
         *np++ = *cp++;
         *np++ = *cp++;
      } else if (cp[0] == '/')
         break;
      else
         *np++ = *cp++;
   }
   *np = '\0';
   NYD_LEAVE;
   return n;
}

FL char *
(urlxenc)(char const *cp SALLOC_DEBUG_ARGS) /* XXX (->URL (yet auxlily.c)) */
{
   char *n, *np;
   NYD_ENTER;

   np = n = (salloc)(strlen(cp) * 3 +1 SALLOC_DEBUG_ARGSCALL);

   while (*cp != '\0') {
      if (alnumchar(*cp) || *cp == '_' || *cp == '@' ||
            (PTRCMP(np, >, n) && (*cp == '.' || *cp == '-' || *cp == ':')))
         *np++ = *cp;
      else {
         *np++ = '%';
         *np++ = Hexchar((*cp & 0xf0) >> 4);
         *np++ = Hexchar(*cp & 0x0f);
      }
      cp++;
   }
   *np = '\0';
   NYD_LEAVE;
   return n;
}

FL char *
(urlxdec)(char const *cp SALLOC_DEBUG_ARGS) /* XXX (->URL (yet auxlily.c)) */
{
   char *n, *np;
   NYD_ENTER;

   np = n = (salloc)(strlen(cp) +1 SALLOC_DEBUG_ARGSCALL);

   while (*cp != '\0') {
      if (cp[0] == '%' && cp[1] != '\0' && cp[2] != '\0') {
         *np = (int)(cp[1] > '9' ? cp[1] - 'A' + 10 : cp[1] - '0') << 4;
         *np++ |= cp[2] > '9' ? cp[2] - 'A' + 10 : cp[2] - '0';
         cp += 3;
      } else
         *np++ = *cp++;
   }
   *np = '\0';
   NYD_LEAVE;
   return n;
}

FL struct str *
str_concat_csvl(struct str *self, ...) /* XXX onepass maybe better here */
{
   va_list vl;
   size_t l;
   char const *cs;
   NYD_ENTER;

   va_start(vl, self);
   for (l = 0; (cs = va_arg(vl, char const*)) != NULL;)
      l += strlen(cs);
   va_end(vl);

   self->l = l;
   self->s = salloc(l +1);

   va_start(vl, self);
   for (l = 0; (cs = va_arg(vl, char const*)) != NULL;) {
      size_t i = strlen(cs);
      memcpy(self->s + l, cs, i);
      l += i;
   }
   self->s[l] = '\0';
   va_end(vl);
   NYD_LEAVE;
   return self;
}

#ifdef HAVE_SPAM
FL struct str *
(str_concat_cpa)(struct str *self, char const * const *cpa,
   char const *sep_o_null SALLOC_DEBUG_ARGS)
{
   size_t sonl, l;
   char const * const *xcpa;
   NYD_ENTER;

   sonl = (sep_o_null != NULL) ? strlen(sep_o_null) : 0;

   for (l = 0, xcpa = cpa; *xcpa != NULL; ++xcpa)
      l += strlen(*xcpa) + sonl;

   self->l = l;
   self->s = (salloc)(l +1 SALLOC_DEBUG_ARGSCALL);

   for (l = 0, xcpa = cpa; *xcpa != NULL; ++xcpa) {
      size_t i = strlen(*xcpa);
      memcpy(self->s + l, *xcpa, i);
      l += i;
      if (sonl > 0) {
         memcpy(self->s + l, sep_o_null, sonl);
         l += sonl;
      }
   }
   self->s[l] = '\0';
   NYD_LEAVE;
   return self;
}
#endif

/*
 * Routines that are not related to auto-reclaimed storage follow.
 */

FL int
anyof(char const *s1, char const *s2)
{
   NYD_ENTER;
   for (; *s1 != '\0'; ++s1)
      if (strchr(s2, *s1) != NULL)
         break;
   NYD_LEAVE;
   return (*s1 != '\0');
}

FL char *
n_strsep(char **iolist, char sep, bool_t ignore_empty)
{
   char *base, *cp;
   NYD_ENTER;

   for (base = *iolist; base != NULL; base = *iolist) {
      while (*base != '\0' && blankspacechar(*base))
         ++base;
      cp = strchr(base, sep);
      if (cp != NULL)
         *iolist = cp + 1;
      else {
         *iolist = NULL;
         cp = base + strlen(base);
      }
      while (cp > base && blankspacechar(cp[-1]))
         --cp;
      *cp = '\0';
      if (*base != '\0' || !ignore_empty)
         break;
   }
   NYD_LEAVE;
   return base;
}

FL void
i_strcpy(char *dest, char const *src, size_t size)
{
   NYD_ENTER;
   if (size > 0) {
      for (;; ++dest, ++src)
         if ((*dest = lowerconv(*src)) == '\0') {
            break;
         } else if (--size == 0) {
            *dest = '\0';
            break;
         }
   }
   NYD_LEAVE;
}

FL int
is_prefix(char const *as1, char const *as2)
{
   char c;
   NYD_ENTER;

   for (; (c = *as1) == *as2 && c != '\0'; ++as1, ++as2)
      if (*as2 == '\0')
         break;
   NYD_LEAVE;
   return (c == '\0');
}

FL char const *
last_at_before_slash(char const *sp)/* XXX (->URL (yet auxlily.c) / obsolete) */
{
   char const *cp;
   char c;
   NYD_ENTER;

   for (cp = sp; (c = *cp) != '\0'; ++cp)
      if (c == '/')
         break;
   while (cp > sp && *--cp != '@')
      ;
   if (*cp != '@')
      cp = NULL;
   NYD_LEAVE;
   return cp;
}

FL char *
laststring(char *linebuf, bool_t *needs_list, bool_t strip)
{
   char *cp, *p, quoted;
   NYD_ENTER;

   /* Anything to do at all? */
   if (*(cp = linebuf) == '\0')
      goto jnull;
   cp += strlen(linebuf) -1;

   /* Strip away trailing blanks */
   while (whitechar(*cp) && cp > linebuf)
      --cp;
   cp[1] = '\0';
   if (cp == linebuf)
      goto jleave;

   /* Now search for the BOS of the "last string" */
   quoted = *cp;
   if (quoted == '\'' || quoted == '"') {
      if (strip)
         *cp = '\0';
   } else
      quoted = ' ';

   while (cp > linebuf) {
      --cp;
      if (quoted != ' ') {
         if (*cp != quoted)
            continue;
      } else if (!whitechar(*cp))
         continue;
      if (cp == linebuf || cp[-1] != '\\') {
         /* When in whitespace mode, WS prefix doesn't belong */
         if (quoted == ' ')
            ++cp;
         break;
      }
      /* Expand the escaped quote character */
      for (p = --cp; (p[0] = p[1]) != '\0'; ++p)
         ;
   }
   if (strip && quoted != ' ' && *cp == quoted)
      for (p = cp; (p[0] = p[1]) != '\0'; ++p)
         ;

   /* The "last string" has been skipped over, but still, try to step backwards
    * until we are at BOS or see whitespace, so as to make possible things like
    * "? copy +'x y.mbox'" or even "? copy +x\ y.mbox" */
   while (cp > linebuf) {
      --cp;
      if (whitechar(*cp)) {
         p = cp;
         *cp++ = '\0';
         /* We can furtherly release our callees if we now decide wether the
          * remaining non-"last string" line content contains non-WS */
         while (--p >= linebuf)
            if (!whitechar(*p))
               goto jleave;
         linebuf = cp;
         break;
      }
   }

jleave:
   if (cp != NULL && *cp == '\0')
      goto jnull;
   *needs_list = (cp != linebuf && *linebuf != '\0');
j_leave:
   NYD_LEAVE;
   return cp;
jnull:
   *needs_list = FAL0;
   cp = NULL;
   goto j_leave;
}

FL void
makelow(char *cp) /* TODO isn't that crap? --> */
{
      NYD_ENTER;
#ifdef HAVE_C90AMEND1
   if (mb_cur_max > 1) {
      char *tp = cp;
      wchar_t wc;
      int len;

      while (*cp != '\0') {
         len = mbtowc(&wc, cp, mb_cur_max);
         if (len < 0)
            *tp++ = *cp++;
         else {
            wc = towlower(wc);
            if (wctomb(tp, wc) == len)
               tp += len, cp += len;
            else
               *tp++ = *cp++; /* <-- at least here */
         }
      }
   } else
#endif
   {
      do
         *cp = tolower((uc_it)*cp);
      while (*cp++ != '\0');
   }
   NYD_LEAVE;
}

FL bool_t
substr(char const *str, char const *sub)
{
   char const *cp, *backup;
   NYD_ENTER;

   cp = sub;
   backup = str;
   while (*str != '\0' && *cp != '\0') {
#ifdef HAVE_C90AMEND1
      if (mb_cur_max > 1) {
         wchar_t c, c2;
         int sz;

         if ((sz = mbtowc(&c, cp, mb_cur_max)) == -1)
            goto Jsinglebyte;
         cp += sz;
         if ((sz = mbtowc(&c2, str, mb_cur_max)) == -1)
            goto Jsinglebyte;
         str += sz;
         c = towupper(c);
         c2 = towupper(c2);
         if (c != c2) {
            if ((sz = mbtowc(&c, backup, mb_cur_max)) > 0) {
               backup += sz;
               str = backup;
            } else
               str = ++backup;
            cp = sub;
         }
      } else
Jsinglebyte:
#endif
      {
         int c, c2;

         c = *cp++ & 0377;
         if (islower(c))
            c = toupper(c);
         c2 = *str++ & 0377;
         if (islower(c2))
            c2 = toupper(c2);
         if (c != c2) {
            str = ++backup;
            cp = sub;
         }
      }
   }
   NYD_LEAVE;
   return (*cp == '\0');
}

#ifndef HAVE_SNPRINTF
FL int
snprintf(char *str, size_t size, char const *format, ...) /* XXX DANGER! */
{
   va_list ap;
   int ret;
   NYD_ENTER;

   va_start(ap, format);
   ret = vsprintf(str, format, ap);
   va_end(ap);
   if (ret < 0)
      ret = strlen(str);
   NYD_LEAVE;
   return ret;
}
#endif

FL char *
sstpcpy(char *dst, char const *src)
{
   NYD_ENTER;
   while ((*dst = *src++) != '\0')
      ++dst;
   NYD_LEAVE;
   return dst;
}

FL char *
(sstrdup)(char const *cp SMALLOC_DEBUG_ARGS)
{
   char *dp;
   NYD_ENTER;

   dp = (cp == NULL) ? NULL : (sbufdup)(cp, strlen(cp) SMALLOC_DEBUG_ARGSCALL);
   NYD_LEAVE;
   return dp;
}

FL char *
(sbufdup)(char const *cp, size_t len SMALLOC_DEBUG_ARGS)
{
   char *dp = NULL;
   NYD_ENTER;

   dp = (smalloc)(len +1 SMALLOC_DEBUG_ARGSCALL);
   if (cp != NULL)
      memcpy(dp, cp, len);
   dp[len] = '\0';
   NYD_LEAVE;
   return dp;
}

FL char *
n_strlcpy(char *dst, char const *src, size_t len)
{
   NYD_ENTER;

   assert(len > 0);

   dst = strncpy(dst, src, len);
   dst[len -1] = '\0';
   NYD_LEAVE;
   return dst;
}

FL int
asccasecmp(char const *s1, char const *s2)
{
   int cmp;
   NYD_ENTER;

   for (;;) {
      char c1 = *s1++, c2 = *s2++;
      if ((cmp = lowerconv(c1) - lowerconv(c2)) != 0 || c1 == '\0')
         break;
   }
   NYD_LEAVE;
   return cmp;
}

FL int
ascncasecmp(char const *s1, char const *s2, size_t sz)
{
   int cmp = 0;
   NYD_ENTER;

   while (sz-- > 0) {
      char c1 = *s1++, c2 = *s2++;
      cmp = (ui8_t)lowerconv(c1);
      cmp -= (ui8_t)lowerconv(c2);
      if (cmp != 0 || c1 == '\0')
         break;
   }
   NYD_LEAVE;
   return cmp;
}

FL char const *
asccasestr(char const *haystack, char const *xneedle)
{
   char *needle = NULL, *NEEDLE;
   size_t i, sz;
   NYD_ENTER;

   sz = strlen(xneedle);
   if (sz == 0)
      goto jleave;

   needle = ac_alloc(sz);
   NEEDLE = ac_alloc(sz);
   for (i = 0; i < sz; i++) {
      needle[i] = lowerconv(xneedle[i]);
      NEEDLE[i] = upperconv(xneedle[i]);
   }

   while (*haystack != '\0') {
      if (*haystack == *needle || *haystack == *NEEDLE) {
         for (i = 1; i < sz; ++i)
            if (haystack[i] != needle[i] && haystack[i] != NEEDLE[i])
               break;
         if (i == sz)
            goto jleave;
      }
      ++haystack;
   }
   haystack = NULL;
jleave:
   if (needle != NULL) {
      ac_free(NEEDLE);
      ac_free(needle);
   }
   NYD_LEAVE;
   return haystack;
}

FL bool_t
is_asccaseprefix(char const *as1, char const *as2)
{
   bool_t rv = FAL0;
   NYD_ENTER;

   for (;; ++as1, ++as2) {
      char c1 = lowerconv(*as1), c2 = lowerconv(*as2);
      if ((rv = (c1 == '\0')))
         break;
      if (c1 != c2 || c2 == '\0')
         break;
   }
   NYD_LEAVE;
   return rv;
}

FL struct str *
(n_str_dup)(struct str *self, struct str const *t SMALLOC_DEBUG_ARGS)
{
   NYD_ENTER;
   if (t != NULL && t->l > 0) {
      self->l = t->l;
      self->s = (srealloc)(self->s, t->l +1 SMALLOC_DEBUG_ARGSCALL);
      memcpy(self->s, t->s, t->l +1);
   } else
      self->l = 0;
   NYD_LEAVE;
   return self;
}

FL struct str *
(n_str_add_buf)(struct str *self, char const *buf, size_t buflen
   SMALLOC_DEBUG_ARGS)
{
   NYD_ENTER;
   if (buflen != 0) {
      size_t sl = self->l;
      self->l = sl + buflen;
      self->s = (srealloc)(self->s, self->l +1 SMALLOC_DEBUG_ARGSCALL);
      memcpy(self->s + sl, buf, buflen);
      self->s[self->l] = '\0';
   }
   NYD_LEAVE;
   return self;
}

/*
 * Our iconv(3) wrapper
 */
#ifdef HAVE_ICONV

static void _ic_toupper(char *dest, char const *src);
static void _ic_stripdash(char *p);

static void
_ic_toupper(char *dest, char const *src)
{
   NYD_ENTER;
   do
      *dest++ = upperconv(*src);
   while (*src++ != '\0');
   NYD_LEAVE;
}

static void
_ic_stripdash(char *p)
{
   char *q = p;
   NYD_ENTER;

   do
      if (*(q = p) != '-')
         ++q;
   while (*p++ != '\0');
   NYD_LEAVE;
}

FL iconv_t
n_iconv_open(char const *tocode, char const *fromcode)
{
   iconv_t id;
   char *t, *f;
   NYD_ENTER;

   if ((id = iconv_open(tocode, fromcode)) != (iconv_t)-1)
      goto jleave;

   /* Remove the "iso-" prefixes for Solaris */
   if (!ascncasecmp(tocode, "iso-", 4))
      tocode += 4;
   else if (!ascncasecmp(tocode, "iso", 3))
      tocode += 3;
   if (!ascncasecmp(fromcode, "iso-", 4))
      fromcode += 4;
   else if (!ascncasecmp(fromcode, "iso", 3))
      fromcode += 3;
   if (*tocode == '\0' || *fromcode == '\0') {
      id = (iconv_t)-1;
      goto jleave;
   }
   if ((id = iconv_open(tocode, fromcode)) != (iconv_t)-1)
      goto jleave;

   /* Solaris prefers upper-case charset names. Don't ask... */
   t = salloc(strlen(tocode) +1);
   _ic_toupper(t, tocode);
   f = salloc(strlen(fromcode) +1);
   _ic_toupper(f, fromcode);
   if ((id = iconv_open(t, f)) != (iconv_t)-1)
      goto jleave;

   /* Strip dashes for UnixWare */
   _ic_stripdash(t);
   _ic_stripdash(f);
   if ((id = iconv_open(t, f)) != (iconv_t)-1)
      goto jleave;

   /* Add your vendor's sillynesses here */

   /* If the encoding names are equal at this point, they are just not
    * understood by iconv(), and we cannot sensibly use it in any way.  We do
    * not perform this as an optimization above since iconv() can otherwise be
    * used to check the validity of the input even with identical encoding
    * names */
   if (!strcmp(t, f))
      errno = 0;
jleave:
   NYD_LEAVE;
   return id;
}

FL void
n_iconv_close(iconv_t cd)
{
   NYD_ENTER;
   iconv_close(cd);
   if (cd == iconvd)
      iconvd = (iconv_t)-1;
   NYD_LEAVE;
}

#ifdef notyet
FL void
n_iconv_reset(iconv_t cd)
{
   NYD_ENTER;
   iconv(cd, NULL, NULL, NULL, NULL);
   NYD_LEAVE;
}
#endif

/* (2012-09-24: export and use it exclusively to isolate prototype problems
 * (*inb* is 'char const **' except in POSIX) in a single place.
 * GNU libiconv even allows for configuration time const/non-const..
 * In the end it's an ugly guess, but we can't do better since make(1) doesn't
 * support compiler invocations which bail on error, so no -Werror */
/* Citrus project? */
# if defined _ICONV_H_ && defined __ICONV_F_HIDE_INVALID
  /* DragonFly 3.2.1 is special TODO newer DragonFly too, but different */
#  ifdef __DragonFly__
#   define __INBCAST(S) (char ** __restrict__)UNCONST(S)
#  else
#   define __INBCAST(S) (char const **)UNCONST(S)
#  endif
# endif
# ifndef __INBCAST
#  define __INBCAST(S)  (char **)UNCONST(S)
# endif

FL int
n_iconv_buf(iconv_t cd, char const **inb, size_t *inbleft,/*XXX redo iconv use*/
   char **outb, size_t *outbleft, bool_t skipilseq)
{
   int err = 0;
   NYD_ENTER;

   for (;;) {
      size_t sz = iconv(cd, __INBCAST(inb), inbleft, outb, outbleft);
      if (sz != (size_t)-1)
         break;
      err = errno;
      if (!skipilseq || err != EILSEQ)
         break;
      if (*inbleft > 0) {
         ++(*inb);
         --(*inbleft);
      } else if (*outbleft > 0) {
         **outb = '\0';
         break;
      }
      if (*outbleft > 0/* TODO 0xFFFD 2*/) {
         /* TODO 0xFFFD (*outb)[0] = '[';
          * TODO (*outb)[1] = '?';
          * TODO 0xFFFD (*outb)[2] = ']';
          * TODO (*outb) += 3;
          * TODO (*outbleft) -= 3; */
          *(*outb)++ = '?';
          --*outbleft;
      } else {
         err = E2BIG;
         break;
      }
      err = 0;
   }
   NYD_LEAVE;
   return err;
}
# undef __INBCAST

FL int
n_iconv_str(iconv_t cd, struct str *out, struct str const *in,
   struct str *in_rest_or_null, bool_t skipilseq)
{
   int err;
   char *obb, *ob;
   char const *ib;
   size_t olb, ol, il;
   NYD_ENTER;

   err = 0;
   obb = out->s;
   olb = out->l;
   ol = in->l;

   ol = (ol << 1) - (ol >> 4);
   if (olb < ol) {
      olb = ol;
      goto jrealloc;
   }

   for (;;) {
      ib = in->s;
      il = in->l;
      ob = obb;
      ol = olb;
      err = n_iconv_buf(cd, &ib, &il, &ob, &ol, skipilseq);
      if (err == 0 || err != E2BIG)
         break;
      err = 0;
      olb += in->l;
jrealloc:
      obb = srealloc(obb, olb);
   }

   if (in_rest_or_null != NULL) {
      in_rest_or_null->s = UNCONST(ib);
      in_rest_or_null->l = il;
   }
   out->s = obb;
   out->l = olb - ol;
   NYD_LEAVE;
   return err;
}
#endif /* HAVE_ICONV */

/* vim:set fenc=utf-8:s-it-mode */
