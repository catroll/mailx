/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Miscellaneous user commands, like `echo', `pwd', etc.
 *
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cmd_misc
#define mx_SOURCE
#define mx_SOURCE_CMD_MISC

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <sys/utsname.h>

#include <su/cs.h>
#include <su/mem.h>
#include <su/sort.h>

#include "mx/child.h"
#include "mx/compat.h"
#include "mx/file-streams.h"
#include "mx/go.h"
#include "mx/sigs.h"

#include "mx/cmd-misc.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Expand the shell escape by expanding unescaped !'s into the last issued
 * command where possible */
static char const *a_cmisc_bangexp(char const *cp);

/* c_n?echo(), c_n?echoerr() */
static int a_cmisc_echo(void *vp, FILE *fp, boole donl);

/* c_read(), c_readsh() */
static int a_cmisc_read(void *vp, boole atifs);
static boole a_cmisc_read_set(char const *cp, char const *value);

/* c_version() */
static su_sz a_cmisc_version_cmp(void const *s1, void const *s2);

static char const *
a_cmisc_bangexp(char const *cp){
   static struct str last_bang;

   struct n_string xbang, *bang;
   char c;
   boole changed;
   NYD_IN;

   if(!ok_blook(bang))
      goto jleave;

   changed = FAL0;

   for(bang = n_string_creat(&xbang); (c = *cp++) != '\0';){
      if(c == '!'){
         if(last_bang.l > 0)
            bang = n_string_push_buf(bang, last_bang.s, last_bang.l);
         changed = TRU1;
      }else{
         if(c == '\\' && *cp == '!'){
            ++cp;
            c = '!';
            changed = TRU1;
         }
         bang = n_string_push_c(bang, c);
      }
   }

   if(last_bang.s != NIL)
      n_free(last_bang.s);

   last_bang.s = n_string_cp(bang);
   last_bang.l = bang->s_len;
   cp = last_bang.s;

   bang = n_string_drop_ownership(bang);
   n_string_gut(bang);

   if(changed)
      fprintf(n_stdout, "!%s\n", cp);

jleave:
   NYD_OU;
   return cp;
}

static int
a_cmisc_echo(void *vp, FILE *fp, boole donl){/* TODO -t=enable FEXP!! */
   struct n_string s_b, *s;
   int rv;
   boole cm_local, doerr;
   char const **argv, *varname, **ap, *cp;
   NYD2_IN;

   argv = vp;
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NIL;
   cm_local = ((n_pstate & n_PS_ARGMOD_LOCAL) != 0);
   s = n_string_reserve(n_string_creat_auto(&s_b), 121/* XXX */);
#ifdef mx_HAVE_ERRORS
   doerr = (fp == n_stderr &&  (n_psonce & n_PSO_INTERACTIVE));
#else
   doerr = FAL0;
#endif

   for(ap = argv; *ap != NIL; ++ap){
      if(ap != argv)
         s = n_string_push_c(s, ' ');
      /* TODO -t/-T en/disable if((cp = fexpand(*ap, FEXP_NVAR)) == NIL)
       *   cp = *ap;*/
      cp = *ap;
      s = n_string_push_cp(s, cp);
   }
   if(donl)
      s = n_string_push_c(s, '\n');
   cp = n_string_cp(s);

   if(varname == NIL){
      s32 e;

      e = su_ERR_NONE;
      if(doerr){
         /* xxx Ensure *log-prefix* will be placed by n_err() for next msg */
         if(donl)
            cp = n_string_cp(n_string_trunc(s, s->s_len - 1));
         n_errx(TRU1, (donl ? "%s\n" : "%s"), cp);
      }else if(fputs(cp, fp) == EOF)
         e = su_err_no_by_errno();
      if((rv = (fflush(fp) == EOF)))
         e = su_err_no_by_errno();
      rv |= ferror(fp) ? 1 : 0;
      n_pstate_err_no = e;
   }else if(!n_var_vset(varname, R(up,cp), cm_local)){
      n_pstate_err_no = su_ERR_NOTSUP;
      rv = -1;
   }else{
      n_pstate_err_no = su_ERR_NONE;
      rv = S(int,s->s_len);
   }

   NYD2_OU;
   return rv;
}

static int
a_cmisc_read(void * volatile vp, boole atifs){
   struct n_sigman sm;
   struct str trim;
   struct n_string s_b, *s;
   int rv;
   char const *ifs, **argv, *cp;
   char *linebuf;
   uz linesize, i;
   NYD2_IN;

   s = n_string_creat_auto(&s_b);
   s = n_string_reserve(s, 64 -1);
   mx_fs_linepool_aquire(&linebuf, &linesize);

   ifs = atifs ? ok_vlook(ifs) : NIL; /* (ifs has default value) */
   argv = vp;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
   case 0:
      break;
   default:
      n_pstate_err_no = su_ERR_INTR;
      rv = -1;
      goto jleave;
   }

   n_pstate_err_no = su_ERR_NONE;
   rv = mx_go_input(((n_pstate & n_PS_COMPOSE_MODE
            ? mx_GO_INPUT_CTX_COMPOSE : mx_GO_INPUT_CTX_DEFAULT) |
         mx_GO_INPUT_FORCE_STDIN | mx_GO_INPUT_NL_ESC |
         mx_GO_INPUT_PROMPT_NONE /* XXX POSIX: PS2: yes! */),
         NIL, &linebuf, &linesize, NIL, NIL);
   if(rv < 0){
      if(!mx_go_input_is_eof())
         n_pstate_err_no = su_ERR_BADF;
      goto jleave;
   }else if(rv == 0){
      if(mx_go_input_is_eof()){
         rv = -1;
         goto jleave;
      }
   }else{
      trim.s = linebuf;
      trim.l = rv;

      for(; *argv != NIL; ++argv){
         if(trim.l == 0 || (atifs && n_str_trim_ifs(&trim, FAL0)->l == 0))
            break;

         /* The last variable gets the remaining line less trailing IFS-WS */
         if(atifs){
            if(argv[1] == NIL){
jitall:
               s = n_string_assign_buf(s, trim.s, trim.l);
               trim.l = 0;
            }else for(cp = trim.s, i = 1;; ++cp, ++i){
               if(su_cs_find_c(ifs, *cp) != NIL){
                  s = n_string_assign_buf(s, trim.s, i - 1);
                  trim.s += i;
                  trim.l -= i;
                  break;
               }

               if(i == trim.l)
                  goto jitall;
            }
         }else{
            s = n_string_trunc(s, 0);
jsh_redo:
            if(n_shexp_parse_token((n_SHEXP_PARSE_LOG |
                     n_SHEXP_PARSE_IFS_VAR | n_SHEXP_PARSE_TRIM_SPACE |
                     n_SHEXP_PARSE_TRIM_IFSSPACE), s, &trim, NIL
                  ) & n_SHEXP_STATE_STOP)
               trim.l = 0;
            else if(argv[1] == NIL)
               goto jsh_redo;
         }

         if(!a_cmisc_read_set(*argv, n_string_cp(s))){
            n_pstate_err_no = su_ERR_NOTSUP;
            rv = -1;
            break;
         }
      }
   }

   /* Set the remains to the empty string */
   for(; *argv != NIL; ++argv)
      if(!a_cmisc_read_set(*argv, su_empty)){
         n_pstate_err_no = su_ERR_NOTSUP;
         rv = -1;
         break;
      }

   n_sigman_cleanup_ping(&sm);
jleave:
   mx_fs_linepool_release(linebuf, linesize);

   NYD2_OU;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

static boole
a_cmisc_read_set(char const *cp, char const *value){
   boole rv;
   NYD2_IN;

   if(!n_shexp_is_valid_varname(cp, FAL0))
      value = N_("not a valid variable name");
   else if(!n_var_is_user_writable(cp))
      value = N_("variable is read-only");
   else if(!n_var_vset(cp, S(up,value), FAL0))
      value = N_("failed to update variable value");
   else{
      rv = TRU1;
      goto jleave;
   }

   n_err("read: %s: %s\n", V_(value), n_shexp_quote_cp(cp, FAL0));
   rv = FAL0;

jleave:
   NYD2_OU;
   return rv;
}

static su_sz
a_cmisc_version_cmp(void const *s1, void const *s2){
   su_sz rv;
   char const *cp1, *cp2;
   NYD2_IN;

   cp1 = s1;
   cp2 = s2;
   rv = su_cs_cmp(&cp1[1], &cp2[1]);

   NYD2_OU;
   return rv;
}

int
c_shell(void *vp){
   struct mx_child_ctx cc;
   sigset_t mask;
   int rv;
   FILE *fp;
   boole cm_local;
   char const **argv, *varname, *varres;
   NYD_IN;

   n_pstate_err_no = su_ERR_NONE;
   argv = vp;
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NIL;
   varres = n_empty;
   cm_local = ((n_pstate & n_PS_ARGMOD_LOCAL) != 0);
   fp = NIL;

   if(varname != NIL &&
         (fp = mx_fs_tmp_open(NIL, "shell", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
               NIL)) == NIL){
      n_pstate_err_no = su_ERR_CANCELED;
      rv = -1;
   }else{
      sigemptyset(&mask);
      mx_child_ctx_setup(&cc);
      cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
      cc.cc_mask = &mask;
      if(fp != NIL)
         cc.cc_fds[mx_CHILD_FD_OUT] = fileno(fp);
      mx_child_ctx_set_args_for_sh(&cc, NIL, a_cmisc_bangexp(*argv));

      if(!mx_child_run(&cc) || (rv = cc.cc_exit_status) < 0){
         n_pstate_err_no = cc.cc_error;
         rv = -1;
      }
   }

   if(fp != NIL){
      if(rv != -1){
         int c;
         char *x;
         off_t l;

         fflush_rewind(fp);
         l = fsize(fp);
         if(UCMP(64, l, >=, UZ_MAX -42)){
            n_pstate_err_no = su_ERR_NOMEM;
            varres = n_empty;
         }else if(l > 0){
            varres = x = n_autorec_alloc(l +1);

            for(; l > 0 && (c = getc(fp)) != EOF; --l)
               *x++ = c;
            *x++ = '\0';
            if(l != 0){
               n_pstate_err_no = su_err_no_by_errno();
               varres = n_empty; /* xxx hmmm */
            }
         }
      }

      mx_fs_close(fp);
   }

   if(varname != NIL){
      if(!n_var_vset(varname, R(up,varres), cm_local)){
         n_pstate_err_no = su_ERR_NOTSUP;
         rv = -1;
      }
   }else if(rv >= 0 && (n_psonce & n_PSO_INTERACTIVE)){
      fprintf(n_stdout, "!\n");
      /* Line buffered fflush(n_stdout); */
   }
   NYD_OU;
   return rv;
}

int
c_dosh(void *vp){
   struct mx_child_ctx cc;
   int rv;
   NYD_IN;
   UNUSED(vp);

   mx_child_ctx_setup(&cc);
   cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
   cc.cc_cmd = ok_vlook(SHELL);

   if(mx_child_run(&cc) && (rv = cc.cc_exit_status) >= 0){
      putc('\n', n_stdout);
      /* Line buffered fflush(n_stdout); */
      n_pstate_err_no = su_ERR_NONE;
   }else{
      n_pstate_err_no = cc.cc_error;
      rv = -1;
   }

   NYD_OU;
   return rv;
}

int
c_cwd(void *vp){
   struct n_string s_b, *s;
   uz l;
   boole cm_local;
   char const *varname;
   NYD_IN;

   s = n_string_creat_auto(&s_b);
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *S(char const**,vp) : NIL;
   cm_local = ((n_pstate & n_PS_ARGMOD_LOCAL) != 0);
   l = PATH_MAX;

   for(;; l += PATH_MAX){
      s = n_string_resize(n_string_trunc(s, 0), l);

      if(getcwd(s->s_dat, s->s_len) == NIL){
         int e;

         e = su_err_no_by_errno();
         if(e == su_ERR_RANGE)
            continue;
         n_perr(_("Failed to getcwd(3)"), e);
         vp = NIL;
         break;
      }

      if(varname != NIL){
         if(!n_var_vset(varname, R(up,s->s_dat), cm_local))
            vp = NIL;
      }else{
         l = su_cs_len(s->s_dat);
         s = n_string_trunc(s, l);
         if(fwrite(s->s_dat, 1, s->s_len, n_stdout) == s->s_len &&
               putc('\n', n_stdout) == EOF)
            vp = NIL;
      }
      break;
   }

   NYD_OU;
   return (vp == NIL ? n_EXIT_ERR : n_EXIT_OK);
}

int
c_chdir(void *vp){
   char **arglist;
   char const *cp;
   NYD_IN;

   if(*(arglist = vp) == NIL)
      cp = ok_vlook(HOME);
   else if((cp = fexpand(*arglist, /*FEXP_NOPROTO |*/ FEXP_LOCAL_FILE |
         FEXP_NVAR)) == NIL)
      goto jleave;

   if(chdir(cp) == -1){
      n_perr(cp, su_err_no_by_errno());
      cp = NIL;
   }

jleave:
   NYD_OU;
   return (cp == NIL ? n_EXIT_ERR : n_EXIT_OK);
}

int
c_echo(void *vp){
   int rv;
   NYD_IN;

   rv = a_cmisc_echo(vp, n_stdout, TRU1);

   NYD_OU;
   return rv;
}

int
c_echoerr(void *vp){
   int rv;
   NYD_IN;

   rv = a_cmisc_echo(vp, n_stderr, TRU1);

   NYD_OU;
   return rv;
}

int
c_echon(void *vp){
   int rv;
   NYD_IN;

   rv = a_cmisc_echo(vp, n_stdout, FAL0);

   NYD_OU;
   return rv;
}

int
c_echoerrn(void *vp){
   int rv;
   NYD_IN;

   rv = a_cmisc_echo(vp, n_stderr, FAL0);

   NYD_OU;
   return rv;
}

int
c_read(void *vp){
   int rv;
   NYD2_IN;

   rv = a_cmisc_read(vp, TRU1);

   NYD2_OU;
   return rv;
}

int
c_readsh(void *vp){
   int rv;
   NYD2_IN;

   rv = a_cmisc_read(vp, FAL0);

   NYD2_OU;
   return rv;
}

int
c_readall(void *vp){ /* TODO 64-bit retval */
   struct n_sigman sm;
   struct n_string s_b, *s;
   char *linebuf;
   uz linesize;
   int rv;
   char const **argv;
   NYD2_IN;

   s = n_string_creat_auto(&s_b);
   s = n_string_reserve(s, 64 -1);

   linesize = 0;
   linebuf = NIL;
   argv = vp;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
   case 0:
      break;
   default:
      n_pstate_err_no = su_ERR_INTR;
      rv = -1;
      goto jleave;
   }

   n_pstate_err_no = su_ERR_NONE;

   for(;;){
      rv = mx_go_input(((n_pstate & n_PS_COMPOSE_MODE
               ? mx_GO_INPUT_CTX_COMPOSE : mx_GO_INPUT_CTX_DEFAULT) |
            mx_GO_INPUT_FORCE_STDIN | /*mx_GO_INPUT_NL_ESC |*/
            mx_GO_INPUT_PROMPT_NONE),
            NIL, &linebuf, &linesize, NIL, NIL);
      if(rv < 0){
         if(!mx_go_input_is_eof()){
            n_pstate_err_no = su_ERR_BADF;
            goto jleave;
         }
         if(s->s_len == 0)
            goto jleave;
         break;
      }

      if(n_pstate & n_PS_READLINE_NL)
         linebuf[rv++] = '\n'; /* Replace NUL with it */

      if(UNLIKELY(rv == 0)){ /* xxx will not get*/
         if(mx_go_input_is_eof()){
            if(s->s_len == 0){
               rv = -1;
               goto jleave;
            }
            break;
         }
      }else if(LIKELY(UCMP(32, S32_MAX - s->s_len, >, rv)))
         s = n_string_push_buf(s, linebuf, rv);
      else{
         n_pstate_err_no = su_ERR_OVERFLOW;
         rv = -1;
         goto jleave;
      }
   }

   if(!a_cmisc_read_set(argv[0], n_string_cp(s))){
      n_pstate_err_no = su_ERR_NOTSUP;
      rv = -1;
      goto jleave;
   }
   rv = s->s_len;

   n_sigman_cleanup_ping(&sm);
jleave:
   if(linebuf != NIL)
      n_free(linebuf);

   NYD2_OU;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

int
c_version(void *vp){
   struct utsname ut;
   struct n_string s_b, *s;
   int rv;
   char *iop;
   char const *cp, **arr;
   uz i, lnlen, j;
   NYD_IN;

   s = n_string_creat_auto(&s_b);
   s = n_string_book(s, 1024);

   /* First two lines */
   s = mx_version(s);
   s = n_string_push_cp(s, _("Features included (+) or not (-):\n"));

   /* Some lines with the features.
    * *features* starts with dummy byte to avoid + -> *folder* expansions */
   i = su_cs_len(cp = &ok_vlook(features)[1]) +1;
   iop = n_autorec_alloc(i);
   su_mem_copy(iop, cp, i);

   arr = n_autorec_alloc(sizeof(cp) * VAL_FEATURES_CNT);
   for(i = 0; (cp = su_cs_sep_c(&iop, ',', TRU1)) != NIL; ++i)
      arr[i] = cp;
   su_sort_shell_vpp(su_S(void const**,arr), i, &a_cmisc_version_cmp);

   for(lnlen = 0; i-- > 0;){
      cp = *(arr++);
      j = su_cs_len(cp);

      if((lnlen += j + 1) > 72){
         s = n_string_push_c(s, '\n');
         lnlen = j + 1;
      }
      s = n_string_push_c(s, ' ');
      s = n_string_push_buf(s, cp, j);
   }
   s = n_string_push_c(s, '\n');

   /* */
   if(n_poption & n_PO_V){
      s = n_string_push_cp(s, "Compile: ");
      s = n_string_push_cp(s, ok_vlook(build_cc));
      s = n_string_push_cp(s, "\nLink: ");
      s = n_string_push_cp(s, ok_vlook(build_ld));
      if(*(cp = ok_vlook(build_rest)) != '\0'){
         s = n_string_push_cp(s, "\nRest: ");
         s = n_string_push_cp(s, cp);
      }
      s = n_string_push_c(s, '\n');

      /* A trailing line with info of the running machine */
      uname(&ut);
      s = n_string_push_c(s, '@');
      s = n_string_push_cp(s, ut.sysname);
      s = n_string_push_c(s, ' ');
      s = n_string_push_cp(s, ut.release);
      s = n_string_push_c(s, ' ');
      s = n_string_push_cp(s, ut.version);
      s = n_string_push_c(s, ' ');
      s = n_string_push_cp(s, ut.machine);
      s = n_string_push_c(s, '\n');
   }

   /* Done */
   cp = n_string_cp(s);

   if(n_pstate & n_PS_ARGMOD_VPUT){
      if(n_var_vset(*(char const**)vp, R(up,cp),
            ((n_pstate & n_PS_ARGMOD_LOCAL) != 0)))
         rv = 0;
      else
         rv = -1;
   }else{
      if(fputs(cp, n_stdout) != EOF)
         rv = 0;
      else{
         clearerr(n_stdout);
         rv = 1;
      }
   }

   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_MISC
/* s-it-mode */
