/*@ Implementation of cs.h: finding related things.
 *@ TODO Optimize (even asm hooks?)
 *
 * Copyright (c) 2017 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
#undef su_FILE
#define su_FILE su_cs_find
#define su_SOURCE
#define su_SOURCE_CS_FIND

#include "su/code.h"

#include "su/bits.h"
#include "su/mem.h"

#include "su/cs.h"
#include "su/code-in.h"

char *
su_cs_find(char const *cp, char const *x){
   char c, cc;
   NYD_IN;
   ASSERT_NYD_RET_VOID(cp != NIL);
   ASSERT_NYD_RET(x != NIL, cp = NIL);

   /* Return cp if x is empty */
   if(LIKELY((c = *x++) != '\0')){
      while((cc = *cp++) != '\0'){
         if(cc == c && su_cs_starts_with(cp, x)){
            --cp;
            goto jleave;
         }
      }
      cp = NIL;
   }
jleave:
   NYD_OU;
   return S(char*,su_UNCONST(cp));
}

char *
su_cs_find_c(char const *cp, char x){
   NYD_IN;
   ASSERT_NYD_RET_VOID(cp != NIL);

   for(;; ++cp){
      char c;

      if((c = *cp) == x)
         break;
      if(c == '\0'){
         cp = NIL;
         break;
      }
   }
   NYD_OU;
   return S(char*,su_UNCONST(cp));
}

char *
su_cs_find_case(char const *cp, char const *x){
   char c, cc;
   NYD_IN;
   ASSERT_NYD_RET_VOID(cp != NIL);
   ASSERT_NYD_RET(x != NIL, cp = NIL);

   /* Return cp if x is empty */
   if(LIKELY((c = *x++) != '\0')){
      c = su_cs_to_lower(c);
      while((cc = *cp++) != '\0'){
         cc = su_cs_to_lower(cc);
         if(cc == c && su_cs_starts_with_case(cp, x)){
            --cp;
            goto jleave;
         }
      }
      cp = NIL;
   }
jleave:
   NYD_OU;
   return S(char*,su_UNCONST(cp));
}

uz
su_cs_first_of_cbuf_cbuf(char const *cp, uz cplen, char const *x, uz xlen){
   /* TODO (first|last)_(not_)?of: */
   uz rv, bs[su_BITS_TO_UZ(U8_MAX + 1)];
   char c;
   NYD_IN;
   ASSERT_NYD_RET(cplen == 0 || cp != NIL, rv = UZ_MAX);
   ASSERT_NYD_RET(xlen == 0 || x != NIL, rv = UZ_MAX);

   su_mem_set(bs, 0, sizeof bs);

   /* For all bytes in x, set the bit of value */
   for(rv = P2UZ(x);; ++x){
      if(xlen-- == 0 || (c = *x) == '\0')
         break;
      su_bits_array_set(bs, S(u8,c));
   }
   if(UNLIKELY(rv == P2UZ(x)))
      goto jnope;

   /* For all bytes in cp, test whether the value bit is set */
   for(x = cp;; ++cp){
      if(cplen-- == 0 || (c = *cp) == '\0')
         break;
      if(su_bits_array_test(bs, S(u8,c))){
         rv = P2UZ(cp - x);
         goto jleave;
      }
   }
jnope:
   rv = UZ_MAX;
jleave:
   NYD_OU;
   return rv;
}

boole
su_cs_starts_with(char const *cp, char const *x){
   boole rv;
   NYD_IN;
   ASSERT_NYD_RET(cp != NIL, rv = FAL0);
   ASSERT_NYD_RET(x != NIL, rv = FAL0);

   if(LIKELY(*x != '\0'))
      for(rv = TRU1;; ++cp, ++x){
         char xc, c;

         if((xc = *x) == '\0')
            goto jleave;
         if((c = *cp) != xc)
            break;
      }
   rv = FAL0;
jleave:
   NYD_OU;
   return rv;
}

boole
su_cs_starts_with_n(char const *cp, char const *x, uz n){
   boole rv;
   NYD_IN;
   ASSERT_NYD_RET(n == 0 || cp != NIL, rv = FAL0);
   ASSERT_NYD_RET(n == 0 || x != NIL, rv = FAL0);

   if(LIKELY(n > 0 && *x != '\0'))
      for(rv = TRU1;; ++cp, ++x){
         char xc, c;

         if((xc = *x) == '\0')
            goto jleave;
         if((c = *cp) != xc)
            break;
         if(--n == 0)
            goto jleave;
      }
   rv = FAL0;
jleave:
   NYD_OU;
   return rv;
}

boole
su_cs_starts_with_case(char const *cp, char const *x){
   boole rv;
   NYD_IN;
   ASSERT_NYD_RET(cp != NIL, rv = FAL0);
   ASSERT_NYD_RET(x != NIL, rv = FAL0);

   if(LIKELY(*x != '\0'))
      for(rv = TRU1;; ++cp, ++x){
         char xc, c;

         if((xc = *x) == '\0')
            goto jleave;
         xc = su_cs_to_lower(*x);
         if((c = su_cs_to_lower(*cp)) != xc)
            break;
      }
   rv = FAL0;
jleave:
   NYD_OU;
   return rv;
}

boole
su_cs_starts_with_case_n(char const *cp, char const *x, uz n){
   boole rv;
   NYD_IN;
   ASSERT_NYD_RET(n == 0 || cp != NIL, rv = FAL0);
   ASSERT_NYD_RET(n == 0 || x != NIL, rv = FAL0);

   if(LIKELY(n > 0 && *x != '\0'))
      for(rv = TRU1;; ++cp, ++x){
         char xc, c;

         if((xc = *x) == '\0')
            goto jleave;
         xc = su_cs_to_lower(*x);
         if((c = su_cs_to_lower(*cp)) != xc)
            break;
         if(--n == 0)
            goto jleave;
      }
   rv = FAL0;
jleave:
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
/* s-it-mode */
