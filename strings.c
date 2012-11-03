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

/*
 * Mail -- a mail program
 *
 * String allocation routines and support routines that build on top of them.
 * Strings handed out here are reclaimed at the top of the command
 * loop each time, so they need not be freed.
 */

#include "rcv.h"

#include <stdarg.h>

#include "extern.h"

/*
 * Allocate SBUFFER_SIZE chunks and keep them in a singly linked list, but
 * release all except the first in sreset(), because other allocations are
 * performed and the underlaying allocator should have the possibility to
 * reorder stuff and possibly even madvise(2), so that S-nail(1) integrates
 * neatly into the system.
 * If allocations larger than SHUGE_CUTLIMIT come in, smalloc() them directly
 * instead and store them in an extra list that is released whenever sreset()
 * is called.
 * TODO the smaller SBUFFER_NISIZE for non-interactive mode is not yet used;
 * TODO i.e., with a two-pass argument parsing we can decide upon the actual
 * TODO program mode which memory strategy should be used; e.g., one-shot
 * TODO sending needs only a small portion of memory, interactive etc. more.
 */

union __align__ {
	char	*cp;
	size_t	sz;
	ul_it	ul;
};
#define SALIGN		(sizeof(union __align__) - 1)

struct buffer {
	struct buffer	*b_next;
	char		*b_bot;		/* For spreserve() */
	char		*b_max;		/* Max usable byte */
	char		*b_caster;	/* NULL if full */
	char		b_buf[SBUFFER_SIZE - 4*sizeof(union __align__)];
};

struct huge {
	struct huge	*h_prev;
	char		h_buf[sizeof(char*)]; /* Variable size indeed */
};

#define SHUGE_CALC_SIZE(S) \
	((sizeof(struct huge) - sizeof(((struct huge*)NULL)->h_buf)) + (S))

static struct buffer	*_buf_head, *_buf_list, *_buf_server;
static struct huge	*_huge_list;

#ifdef HAVE_ASSERTS
size_t	_all_cnt, _all_cycnt, _all_cycnt_max,
	_all_size, _all_cysize, _all_cysize_max, _all_min, _all_max, _all_wast,
	_all_bufcnt, _all_cybufcnt, _all_cybufcnt_max,
	_all_hugecnt, _all_cyhugecnt, _all_cyhugecnt_max,
	_all_resetreqs, _all_resets;
#endif

/*
 * Allocate size more bytes of space and return the address of the
 * first byte to the caller.  An even number of bytes are always
 * allocated so that the space will always be on a word boundary.
 */
void *
salloc(size_t size)
{
#ifdef HAVE_ASSERTS
	size_t orig_size = size;
#endif
	union {struct buffer *b; struct huge *h; char *cp;} u;
	char *x, *y, *z;

	if (size == 0)
		++size;
	size += SALIGN;
	size &= ~SALIGN;

#ifdef HAVE_ASSERTS
	++_all_cnt;
	++_all_cycnt;
	_all_cycnt_max = smax(_all_cycnt_max, _all_cycnt);
	_all_size += size;
	_all_cysize += size;
	_all_cysize_max = smax(_all_cysize_max, _all_cysize);
	_all_min = _all_max == 0 ? size : smin(_all_min, size);
	_all_max = smax(_all_max, size);
	_all_wast += size - orig_size;
#endif

	if (size > SHUGE_CUTLIMIT)
		goto jhuge;

	if ((u.b = _buf_server) != NULL)
		goto jumpin;
jredo:
	for (u.b = _buf_head; u.b != NULL; u.b = u.b->b_next) {
jumpin:		x = u.b->b_caster;
		if (x == NULL) {
			if (u.b == _buf_server) {
				_buf_server = NULL;
				goto jredo;
			}
			continue;
		}
		y = x + size;
		z = u.b->b_max;
		if (y <= z) {
			/*
			 * Alignment is the one thing, the other is what is
			 * usually allocated, and here about 40 bytes seems to
			 * be a good cut to avoid non-usable non-NULL casters
			 */
			u.b->b_caster = (y + 42+16 >= z) ? NULL : y;
			u.cp = x;
			goto jleave;
		}
	}

#ifdef HAVE_ASSERTS
	++_all_bufcnt;
	++_all_cybufcnt;
	_all_cybufcnt_max = smax(_all_cybufcnt_max, _all_cybufcnt);
#endif
	u.b = smalloc(sizeof(struct buffer));
	if (_buf_head == NULL)
		_buf_head = u.b;
	if (_buf_list != NULL)
		_buf_list->b_next = u.b;
	_buf_server = _buf_list = u.b;
	u.b->b_next = NULL;
	u.b->b_caster = (u.b->b_bot = u.b->b_buf) + size;
	u.b->b_max = u.b->b_buf + sizeof(u.b->b_buf) - 1;
	u.cp = u.b->b_bot;
jleave:
	return (u.cp);

jhuge:
#ifdef HAVE_ASSERTS
	++_all_hugecnt;
	++_all_cyhugecnt;
	_all_cyhugecnt_max = smax(_all_cyhugecnt_max, _all_cyhugecnt);
#endif
	u.h = smalloc(SHUGE_CALC_SIZE(size));
	u.h->h_prev = _huge_list;
	_huge_list = u.h;
	u.cp = u.h->h_buf;
	goto jleave;
}

void *
csalloc(size_t nmemb, size_t size)
{
	void *vp;

	size *= nmemb;
	vp = salloc(size);
	memset(vp, 0, size);
	return (vp);
}

/*
 * Reset the string area to be empty.
 * Called to free all strings allocated since last reset.
 */
void 
sreset(void)
{
	union {struct buffer *b; struct huge *h;} u;

#ifdef HAVE_ASSERTS
	++_all_resetreqs;
#endif
	if (noreset)
		goto jleave;

#ifdef HAVE_ASSERTS
	_all_cycnt = _all_cysize = _all_cyhugecnt = 0;
	_all_cybufcnt = (_buf_head != NULL);
	++_all_resets;
#endif

	for (u.h = _huge_list; u.h != NULL;) {
		struct huge *tmp = u.h;
		u.h = u.h->h_prev;
		free(tmp);
	}
	_huge_list = NULL;

	if ((u.b = _buf_head) != NULL) {
		u.b = u.b->b_next;
		_buf_head->b_next = NULL;
		while (u.b != NULL) {
			struct buffer *tmp = u.b;
			u.b = u.b->b_next;
			free(tmp);
		}
		u.b = _buf_head;
		u.b->b_caster = u.b->b_bot;
		_buf_server = _buf_list = u.b;
	}
jleave:	;
}

/*
 * Make the string area permanent.
 * Meant to be called in main, after initialization.
 */
void 
spreserve(void)
{
	if (_buf_head != NULL) {
		/* Before spreserve() we cannot run into this - assert it */
		assert(_buf_head->b_next == NULL);
		_buf_head->b_bot = _buf_head->b_caster;
	}
}

#ifdef HAVE_ASSERTS
int
sstats(void *v)
{
	printf("String usage statistics (cycle means one sreset() cycle):\n"
		"  Buffer allocs ever/max simultan. : %lu/%lu (size: %lu)\n"
		"  Overall alloc count/bytes        : %lu/%lu\n"
		"  Alloc bytes min/max/align wastage: %lu/%lu/%lu\n"
		"  Hugealloc count overall/cycle    : %lu/%lu (cutlimit: %lu)\n"
		"  sreset() cycles                  : %lu (%lu performed)\n"
		"  Cycle maximums: alloc count/bytes: %lu/%lu\n",
		(ul_it)_all_bufcnt, (ul_it)_all_cybufcnt_max,
			(ul_it)sizeof(((struct buffer*)v)->b_buf),
		(ul_it)_all_cnt, (ul_it)_all_size,
		(ul_it)_all_min, (ul_it)_all_max, (ul_it)_all_wast,
		(ul_it)_all_hugecnt, (ul_it)_all_cyhugecnt_max,
			(ul_it)SHUGE_CUTLIMIT,
		(ul_it)_all_resetreqs, (ul_it)_all_resets,
		(ul_it)_all_cycnt_max, (ul_it)_all_cysize_max);
	return (0);
}
#endif

/*
 * Return a pointer to a dynamic copy of the argument.
 */
char *
savestr(const char *str)
{
	size_t size = strlen(str) + 1;
	char *news = salloc(size);
	memcpy(news, str, size);
	return (news);
}

/*
 * Return new string copy of a non-terminated argument.
 */
char *
savestrbuf(const char *sbuf, size_t sbuf_len)
{
	char *news = salloc(sbuf_len + 1);
	memcpy(news, sbuf, sbuf_len);
	news[sbuf_len] = 0;
	return (news);
}

/*
 * Make a copy of new argument incorporating old one.
 */
char *
save2str(const char *str, const char *old)
{
	size_t newsize = strlen(str) + 1, oldsize = old ? strlen(old) + 1 : 0;
	char *news = salloc(newsize + oldsize);
	if (oldsize) {
		memcpy(news, old, oldsize);
		news[oldsize - 1] = ' ';
	}
	memcpy(news + oldsize, str, newsize);
	return (news);
}

char *
savecat(char const *s1, char const *s2)
{
	size_t l1 = strlen(s1), l2 = strlen(s2);
	char *news = salloc(l1 + l2 + 1);
	memcpy(news + 0, s1, l1);
	memcpy(news + l1, s2, l2);
	news[l1 + l2] = '\0';
	return (news);
}

struct str *
str_concat_csvl(struct str *self, ...) /* XXX onepass maybe better here */
{
	va_list vl;
	size_t l;
	char const *cs;

	va_start(vl, self);
	for (l = 0; (cs = va_arg(vl, char const*)) != NULL;)
		l += strlen(cs);
	va_end(vl);

	self->l = l;
	self->s = salloc(l + 1);

	va_start(vl, self);
	for (l = 0; (cs = va_arg(vl, char const*)) != NULL;) {
		size_t i = strlen(cs);
		memcpy(self->s + l, cs, i);
		l += i;
	}
	self->s[l] = '\0';
	va_end(vl);
	return (self);
}
