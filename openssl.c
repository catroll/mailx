/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ OpenSSL functions. TODO this needs an overhaul -- there _are_ stack leaks!?
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 2002
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

EMPTY_FILE(openssl)
#ifdef HAVE_OPENSSL
#include <sys/socket.h>

#include <dirent.h>
#include <netdb.h>

#include <netinet/in.h>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <openssl/x509.h>

#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

#ifdef HAVE_OPENSSL_CONFIG
# include <openssl/conf.h>
#endif

/*
 * OpenSSL client implementation according to: John Viega, Matt Messier,
 * Pravir Chandra: Network Security with OpenSSL. Sebastopol, CA 2002.
 */

/* Update manual on changes! */
#ifndef HAVE_OPENSSL_CONF_CTX /* TODO obsolete the fallback */
# ifndef SSL_OP_NO_SSLv2
#  define SSL_OP_NO_SSLv2     0
# endif
# ifndef SSL_OP_NO_SSLv3
#  define SSL_OP_NO_SSLv3     0
# endif
# ifndef SSL_OP_NO_TLSv1
#  define SSL_OP_NO_TLSv1     0
# endif
# ifndef SSL_OP_NO_TLSv1_1
#  define SSL_OP_NO_TLSv1_1   0
# endif
# ifndef SSL_OP_NO_TLSv1_2
#  define SSL_OP_NO_TLSv1_2   0
# endif

  /* SSL_CONF_CTX and _OP_NO_SSL_MASK were both introduced with 1.0.2!?! */
# ifndef SSL_OP_NO_SSL_MASK
#  define SSL_OP_NO_SSL_MASK  \
   (SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |\
   SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2)
# endif
#endif

#ifdef HAVE_OPENSSL_STACK_OF
# define _STACKOF(X)          STACK_OF(X)
#else
# define _STACKOF(X)          /*X*/STACK
#endif

enum ssl_state {
   SS_INIT           = 1<<0,
   SS_RAND_INIT      = 1<<1,
   SS_EXIT_HDL       = 1<<2,
   SS_CONF_LOAD      = 1<<3,
   SS_ALGO_LOAD      = 1<<4,

   SS_VERIFY_ERROR   = 1<<7
};

struct ssl_method { /* TODO obsolete */
   char const  sm_name[8];
   char const  sm_map[16];
};

#ifndef HAVE_OPENSSL_CONF_CTX /* TODO obsolete the fallback */
struct ssl_protocol {
   char const  *sp_name;
   sl_i        sp_flag;
};
#endif

struct smime_cipher {
   char const           sc_name[8];
   EVP_CIPHER const *   (*sc_fun)(void);
};

/* Supported SSL/TLS methods: update manual on change! */

static struct ssl_method const   _ssl_methods[] = { /* TODO obsolete */
   {"auto",    "ALL,-SSLv2"},
   {"ssl3",    "-ALL,SSLv3"},
   {"tls1",    "-ALL,TLSv1"},
   {"tls1.1",  "-ALL,TLSv1.1"},
   {"tls1.2",  "-ALL,TLSv1.2"}
};

/* Update manual on change! */
#ifndef HAVE_OPENSSL_CONF_CTX /* TODO obsolete the fallback */
static struct ssl_protocol const _ssl_protocols[] = {
   {"ALL",        SSL_OP_NO_SSL_MASK},
   {"TLSv1.2",    SSL_OP_NO_TLSv1_2},
   {"TLSv1.1",    SSL_OP_NO_TLSv1_1},
   {"TLSv1",      SSL_OP_NO_TLSv1},
   {"SSLv3",      SSL_OP_NO_SSLv3},
   {"SSLv2",      0}
};
#endif

/* Supported S/MIME cipher algorithms */
static struct smime_cipher const _smime_ciphers[] = { /* Manual!! */
#ifndef OPENSSL_NO_AES
# define _SMIME_DEFAULT_CIPHER   EVP_aes_128_cbc   /* According to RFC 5751 */
   {"aes-128", &EVP_aes_128_cbc},
   {"aes-256", &EVP_aes_256_cbc},
   {"aes-192", &EVP_aes_192_cbc},
#endif
#ifndef OPENSSL_NO_DES
# ifndef _SMIME_DEFAULT_CIPHER
#  define _SMIME_DEFAULT_CIPHER  EVP_des_ede3_cbc
# endif
   {"des3", &EVP_des_ede3_cbc},
   {"des", &EVP_des_cbc},
#endif
};
#ifndef _SMIME_DEFAULT_CIPHER
# error Your OpenSSL library does not include the necessary
# error cipher algorithms that are required to support S/MIME
#endif

static enum ssl_state   _ssl_state;
static size_t           _ssl_msgno;

static int        _ssl_rand_init(void);
static void       _ssl_init(void);
#if defined HAVE_DEVEL && defined HAVE_OPENSSL_MEMHOOKS && defined HAVE_DEBUG
static void       _ssl_free(void *vp);
#endif
#if defined HAVE_OPENSSL_CONFIG || defined HAVE_OPENSSL_ALL_ALGORITHMS
static void       _ssl_atexit(void);
#endif

static bool_t     _ssl_parse_asn1_time(ASN1_TIME *atp,
                     char *bdat, size_t blen);
static int        _ssl_verify_cb(int success, X509_STORE_CTX *store);

/* Configure sp->s_ctx via SSL_CONF_CTX if possible, _set_option otherwise */
static bool_t     _ssl_ctx_conf(struct sock *sp, struct url const *urlp);

static bool_t     _ssl_load_verifications(struct sock *sp);
static bool_t     _ssl_certificate(struct sock *sp, struct url const *urlp);
static enum okay  ssl_check_host(struct sock *sp, struct url const *urlp);
static int        smime_verify(struct message *m, int n, _STACKOF(X509) *chain,
                        X509_STORE *store);
static EVP_CIPHER const * _smime_cipher(char const *name);
static int        ssl_password_cb(char *buf, int size, int rwflag,
                     void *userdata);
static FILE *     smime_sign_cert(char const *xname, char const *xname2,
                     bool_t dowarn);
static char *     _smime_sign_include_certs(char const *name);
static bool_t     _smime_sign_include_chain_creat(_STACKOF(X509) **chain,
                     char const *cfiles);
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
static enum okay  load_crl1(X509_STORE *store, char const *name);
#endif
static enum okay  load_crls(X509_STORE *store, enum okeys fok, enum okeys dok);

static int
_ssl_rand_init(void)
{
   char *cp, *x;
   int state = 0;
   NYD_ENTER;

#ifdef HAVE_OPENSSL_RAND_EGD
   if ((cp = ok_vlook(ssl_rand_egd)) != NULL) {
      if ((x = file_expand(cp)) == NULL || RAND_egd(cp = x) == -1)
         fprintf(stderr, _("Entropy daemon at \"%s\" not available\n"),
            cp);
      else
         state = 1;
   } else
#endif
   if ((cp = ok_vlook(ssl_rand_file)) != NULL) {
      if ((x = file_expand(cp)) == NULL || RAND_load_file(cp = x, 1024) == -1)
         fprintf(stderr, _("Entropy file at \"%s\" not available\n"), cp);
      else {
         struct stat st;

         if (!stat(cp, &st) && S_ISREG(st.st_mode) && !access(cp, W_OK)) {
            if (RAND_write_file(cp) == -1) {
               fprintf(stderr, _(
                  "Writing entropy data to \"%s\" failed\n"), cp);
            }
         }
         state = 1;
      }
   }
   NYD_LEAVE;
   return state;
}

static void
_ssl_init(void)
{
#ifdef HAVE_OPENSSL_CONFIG
   char const *cp;
#endif
   NYD_ENTER;

   if (!(_ssl_state & SS_INIT)) {
#if defined HAVE_DEVEL && defined HAVE_OPENSSL_MEMHOOKS
# ifdef HAVE_DEBUG
      CRYPTO_set_mem_ex_functions(&smalloc, &srealloc, &_ssl_free);
# else
      CRYPTO_set_mem_functions(&smalloc, &srealloc, &free);
# endif
#endif
      SSL_library_init();
      SSL_load_error_strings();
      _ssl_state |= SS_INIT;
   }

   /* Load openssl.cnf or whatever was given in *ssl-config-file* */
#ifdef HAVE_OPENSSL_CONFIG
   if (!(_ssl_state & SS_CONF_LOAD) &&
         (cp = ok_vlook(ssl_config_file)) != NULL) {
      ul_i flags = CONF_MFLAGS_IGNORE_MISSING_FILE;

      if (*cp == '\0') {
         cp = NULL;
         flags = 0;
      }
      if (CONF_modules_load_file(cp, uagent, flags) == 1) {
         _ssl_state |= SS_CONF_LOAD;
         if (!(_ssl_state & SS_EXIT_HDL)) {
            _ssl_state |= SS_EXIT_HDL;
            atexit(&_ssl_atexit); /* TODO generic program-wide event mech. */
         }
      } else
         ssl_gen_err(_("Ignoring CONF_modules_load_file() load error"));
   }
#endif

   if (!(_ssl_state & SS_RAND_INIT) && _ssl_rand_init())
      _ssl_state |= SS_RAND_INIT;
   NYD_LEAVE;
}

#if defined HAVE_DEVEL && defined HAVE_OPENSSL_MEMHOOKS && defined HAVE_DEBUG
static void
_ssl_free(void *vp)
{
   NYD_ENTER;
   if (vp != NULL)
      free(vp);
   NYD_LEAVE;
}
#endif

#if defined HAVE_OPENSSL_CONFIG || defined HAVE_OPENSSL_ALL_ALGORITHMS
static void
_ssl_atexit(void)
{
   NYD_ENTER;
# ifdef HAVE_OPENSSL_ALL_ALGORITHMS
   if (_ssl_state & SS_ALGO_LOAD)
      EVP_cleanup();
# endif
# ifdef HAVE_OPENSSL_CONFIG
   if (_ssl_state & SS_CONF_LOAD)
      CONF_modules_free();
# endif
   NYD_LEAVE;
}
#endif

static bool_t
_ssl_parse_asn1_time(ASN1_TIME *atp, char *bdat, size_t blen)
{
   BIO *mbp;
   char *mcp;
   long l;
   NYD_ENTER;

   mbp = BIO_new(BIO_s_mem());

   if (ASN1_TIME_print(mbp, atp) && (l = BIO_get_mem_data(mbp, &mcp)) > 0)
      snprintf(bdat, blen, "%.*s", (int)l, mcp);
   else {
      snprintf(bdat, blen, _("Bogus certificate date: %.*s"),
         /*is (int)*/atp->length, (char const*)atp->data);
      mcp = NULL;
   }

   BIO_free(mbp);
   NYD_LEAVE;
   return (mcp != NULL);
}

static int
_ssl_verify_cb(int success, X509_STORE_CTX *store)
{
   char data[256];
   X509 *cert;
   int rv = TRU1;
   NYD_ENTER;

   if (success && !(options & OPT_VERB))
      goto jleave;

   if (_ssl_msgno != 0) {
      fprintf(stderr, "Message %" PRIuZ ":\n", _ssl_msgno);
      _ssl_msgno = 0;
   }
   fprintf(stderr, _(" Certificate depth %d %s\n"),
      X509_STORE_CTX_get_error_depth(store), (success ? "" : _("ERROR")));

   cert = X509_STORE_CTX_get_current_cert(store);

   X509_NAME_oneline(X509_get_subject_name(cert), data, sizeof data);
   fprintf(stderr, _("  subject = %s\n"), data);

   _ssl_parse_asn1_time(X509_get_notBefore(cert), data, sizeof data);
   fprintf(stderr, _("  notBefore = %s\n"), data);

   _ssl_parse_asn1_time(X509_get_notAfter(cert), data, sizeof data);
   fprintf(stderr, _("  notAfter = %s\n"), data);

   if (!success) {
      int err = X509_STORE_CTX_get_error(store);
      fprintf(stderr, _("  err %i: %s\n"),
         err, X509_verify_cert_error_string(err));
      _ssl_state |= SS_VERIFY_ERROR;
   }

   X509_NAME_oneline(X509_get_issuer_name(cert), data, sizeof data);
   fprintf(stderr, _("  issuer = %s\n"), data);

   if (!success && ssl_verify_decide() != OKAY)
      rv = FAL0;
jleave:
   NYD_LEAVE;
   return rv;
}

static bool_t
_ssl_ctx_conf(struct sock *sp, struct url const *urlp)
{
   union {size_t i; int s; sl_i opts;} u;
   char *cp;
   char const *proto;
#ifdef HAVE_OPENSSL_CONF_CTX
   SSL_CONF_CTX *sslccp;
#endif
   bool_t rv = FAL0;
   NYD_ENTER;

   proto = NULL;

   /* TODO obsolete Check for *ssl-method*, warp to a *ssl-protocol* value */
   if ((cp = xok_vlook(ssl_method, urlp, OXM_ALL)) != NULL) {
      OBSOLETE(_("please use *ssl-protocol* instead of *ssl-method*"));
      if (options & OPT_VERB)
         fprintf(stderr, "*ssl-method*: \"%s\"\n", cp);
      for (u.i = 0;;) {
         if (!asccasecmp(_ssl_methods[u.i].sm_name, cp)) {
            proto = _ssl_methods[u.i].sm_map;
            break;
         }
         if (++u.i == NELEM(_ssl_methods)) {
            fprintf(stderr, _("Unsupported TLS/SSL method \"%s\"\n"), cp);
            goto jleave;
         }
      }
   }

   /* *ssl-protocol* */
   if ((cp = xok_vlook(ssl_protocol, urlp, OXM_ALL)) != NULL) {
      if (options & OPT_VERB)
         fprintf(stderr, "*ssl-protocol*: \"%s\"\n", cp);
      proto = cp;
   }

#ifdef HAVE_OPENSSL_CONF_CTX
   if ((sslccp = SSL_CONF_CTX_new()) == NULL) {
      ssl_gen_err(_("SSL_CONF_CTX_new() failed"));
      goto jleave;
   }
   SSL_CONF_CTX_set_flags(sslccp,
      SSL_CONF_FLAG_FILE | SSL_CONF_FLAG_CLIENT | SSL_CONF_FLAG_SHOW_ERRORS);
   SSL_CONF_CTX_set_ssl_ctx(sslccp, sp->s_ctx);

   u.s = SSL_CONF_cmd(sslccp, "Options", "Bugs");/* TODO *ssl-options* */
   if (u.s != 2)
      goto jcmderr;

   u.s = SSL_CONF_cmd(sslccp, "Protocol",
         (proto != NULL ? savecatsep(proto, ',', "-SSLv2") : "-SSLv2"));
   if (u.s == 2)
      u.s = 0;
   else {
jcmderr:
      if (u.s == 0)
         ssl_gen_err(_("SSL_CONF_CTX_cmd() failed"));
      else
         fprintf(stderr,
            "%s: *ssl-protocol* implementation error, please report this\n",
            uagent);
      u.s = 2;
   }

   u.s |= !SSL_CONF_CTX_finish(sslccp);
   SSL_CONF_CTX_free(sslccp);

   if (u.s != 0) {
      if (u.s & 1)
         ssl_gen_err(_("SSL_CONF_CTX_finish() failed"));
      goto jleave;
   }

#else /* HAVE_OPENSSL_CONF_CTX */
   u.opts = SSL_OP_ALL | SSL_OP_NO_SSLv2; /* == "Options" = "Bugs" */

   if (proto != NULL) {
      char *iolist, addin;
      size_t i;

      for (iolist = cp = savestr(proto);
            (cp = n_strsep(&iolist, ',', FAL0)) != NULL;) {
         if (*cp == '\0') {
            fprintf(stderr,
               _("*ssl-protocol*: empty arguments are not supported\n"));
            goto jleave;
         }

         addin = TRU1;
         switch (cp[0]) {
         case '-': addin = FAL0; /* FALLTHRU */
         case '+': ++cp; /* FALLTHRU */
         default : break;
         }

         for (i = 0;;) {
            if (!asccasecmp(cp, _ssl_protocols[i].sp_name)) {
               /* We need to inverse the meaning of the _NO_s */
               if (!addin)
                  u.opts |= _ssl_protocols[i].sp_flag;
               else
                  u.opts &= ~_ssl_protocols[i].sp_flag;
               break;
            }
            if (++i < NELEM(_ssl_protocols))
               continue;
            fprintf(stderr, _("*ssl-protocol*: unsupported value \"%s\"\n"),
               cp);
            goto jleave;
         }
      }
   }
   SSL_CTX_set_options(sp->s_ctx, u.opts);

#endif /* !HAVE_OPENSSL_CONF_CTX */

   rv = TRU1;
jleave:
   NYD_LEAVE;
   return rv;
}

static bool_t
_ssl_load_verifications(struct sock *sp)
{
   char *ca_dir, *ca_file;
   X509_STORE *store;
   bool_t rv = FAL0;
   NYD_ENTER;

   if (ssl_verify_level == SSL_VERIFY_IGNORE) {
      rv = TRU1;
      goto jleave;
   }

   if ((ca_dir = ok_vlook(ssl_ca_dir)) != NULL)
      ca_dir = file_expand(ca_dir);
   if ((ca_file = ok_vlook(ssl_ca_file)) != NULL)
      ca_file = file_expand(ca_file);

   if ((ca_dir != NULL || ca_file != NULL) &&
         SSL_CTX_load_verify_locations(sp->s_ctx, ca_file, ca_dir) != 1) {
      char const *m1, *m2, *m3;

      if (ca_dir != NULL) {
         m1 = ca_dir;
         m2 = (ca_file != NULL) ? _(" or ") : "";
      } else
         m1 = m2 = "";
      m3 = (ca_file != NULL) ? ca_file : "";
      ssl_gen_err(_("Error loading %s%s%s\n"), m1, m2, m3);
      goto jleave;
   }

   if (!ok_blook(ssl_no_default_ca) &&
         SSL_CTX_set_default_verify_paths(sp->s_ctx) != 1) {
      ssl_gen_err(_("Error loading default CA locations\n"));
      goto jleave;
   }

   _ssl_state &= ~SS_VERIFY_ERROR;
   _ssl_msgno = 0;
   SSL_CTX_set_verify(sp->s_ctx, SSL_VERIFY_PEER, &_ssl_verify_cb);
   store = SSL_CTX_get_cert_store(sp->s_ctx);
   load_crls(store, ok_v_ssl_crl_file, ok_v_ssl_crl_dir);

   rv = TRU1;
jleave:
   NYD_LEAVE;
   return rv;
}

static bool_t
_ssl_certificate(struct sock *sp, struct url const *urlp)
{
   char *cert, *key, *cp;
   bool_t rv = FAL0;
   NYD_ENTER;

   if ((cert = xok_vlook(ssl_cert, urlp, OXM_ALL)) == NULL) {
      rv = TRU1;
      goto jleave;
   }
   if (options & OPT_VERB)
      fprintf(stderr, "*ssl-cert* \"%s\"", cert);

   if ((cp = file_expand(cert)) == NULL) {
      fprintf(stderr, _("*ssl-cert* value expansion failed: \"%s\"\n"), cert);
      goto jleave;
   }
   cert = cp;

   if (SSL_CTX_use_certificate_chain_file(sp->s_ctx, cert) != 1) {
      ssl_gen_err(_("Can't load certificate from file \"%s\"\n"), cert);
      goto jleave;
   }

   if ((key = xok_vlook(ssl_key, urlp, OXM_ALL)) == NULL)
      key = cert;
   else {
      if (options & OPT_VERB)
         fprintf(stderr, "*ssl-key* \"%s\"", key);

      if ((cp = file_expand(key)) == NULL) {
         fprintf(stderr, _("*ssl-key* value expansion failed: \"%s\"\n"), key);
         goto jleave;
      }
      key = cp;
   }

   if (SSL_CTX_use_PrivateKey_file(sp->s_ctx, key, SSL_FILETYPE_PEM) != 1) {
      ssl_gen_err(_("Can't load private key from file \"%s\"\n"), key);
      goto jleave;
   }

   rv = TRU1;
jleave:
   NYD_LEAVE;
   return rv;
}

static enum okay
ssl_check_host(struct sock *sp, struct url const *urlp)
{
   char data[256];
   X509 *cert;
   _STACKOF(GENERAL_NAME) *gens;
   GENERAL_NAME *gen;
   X509_NAME *subj;
   enum okay rv = STOP;
   NYD_ENTER;

   if ((cert = SSL_get_peer_certificate(sp->s_ssl)) == NULL) {
      fprintf(stderr, _("No certificate from \"%s\"\n"), urlp->url_h_p.s);
      goto jleave;
   }

   gens = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
   if (gens != NULL) {
      int i;

      for (i = 0; i < sk_GENERAL_NAME_num(gens); ++i) {
         gen = sk_GENERAL_NAME_value(gens, i);
         if (gen->type == GEN_DNS) {
            if (options & OPT_VERB)
               fprintf(stderr, "Comparing subject_alt_name: need<%s> is<%s>\n",
                  urlp->url_host.s, (char*)gen->d.ia5->data);
            rv = rfc2595_hostname_match(urlp->url_host.s,
                  (char*)gen->d.ia5->data);
            if (rv == OKAY)
               goto jdone;
         }
      }
   }

   if ((subj = X509_get_subject_name(cert)) != NULL &&
         X509_NAME_get_text_by_NID(subj, NID_commonName, data, sizeof data)
            > 0) {
      data[sizeof data - 1] = '\0';
      if (options & OPT_VERB)
         fprintf(stderr, "Comparing commonName: need<%s> is<%s>\n",
            urlp->url_host.s, data);
      rv = rfc2595_hostname_match(urlp->url_host.s, data);
   }

jdone:
   X509_free(cert);
jleave:
   NYD_LEAVE;
   return rv;
}

static int
smime_verify(struct message *m, int n, _STACKOF(X509) *chain, X509_STORE *store)
{
   char data[LINESIZE], *sender, *to, *cc, *cnttype;
   int rv, c, i, j;
   struct message *x;
   FILE *fp, *ip;
   off_t size;
   BIO *fb, *pb;
   PKCS7 *pkcs7;
   _STACKOF(X509) *certs;
   _STACKOF(GENERAL_NAME) *gens;
   X509 *cert;
   X509_NAME *subj;
   GENERAL_NAME *gen;
   NYD_ENTER;

   rv = 1;
   fp = NULL;
   fb = NULL;
   _ssl_state &= ~SS_VERIFY_ERROR;
   _ssl_msgno = (size_t)n;

   for (;;) {
      sender = getsender(m);
      to = hfield1("to", m);
      cc = hfield1("cc", m);
      cnttype = hfield1("content-type", m);
      if ((ip = setinput(&mb, m, NEED_BODY)) == NULL)
         goto jleave;
      if (cnttype && !ascncasecmp(cnttype, "application/x-pkcs7-mime", 24)) {
         if ((x = smime_decrypt(m, to, cc, 1)) == NULL)
            goto jleave;
         if (x != (struct message*)-1) {
            m = x;
            continue;
         }
      }
      size = m->m_size;
      break;
   }

   if ((fp = Ftmp(NULL, "smimever", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      perror("tempfile");
      goto jleave;
   }
   while (size-- > 0) {
      c = getc(ip);
      putc(c, fp);
   }
   fflush_rewind(fp);

   if ((fb = BIO_new_fp(fp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(_(
         "Error creating BIO verification object for message %d"), n);
      goto jleave;
   }

   if ((pkcs7 = SMIME_read_PKCS7(fb, &pb)) == NULL) {
      ssl_gen_err(_("Error reading PKCS#7 object for message %d"), n);
      goto jleave;
   }
   if (PKCS7_verify(pkcs7, chain, store, pb, NULL, 0) != 1) {
      ssl_gen_err(_("Error verifying message %d"), n);
      goto jleave;
   }

   if (sender == NULL) {
      fprintf(stderr, _("Warning: Message %d has no sender.\n"), n);
      rv = 0;
      goto jleave;
   }

   certs = PKCS7_get0_signers(pkcs7, chain, 0);
   if (certs == NULL) {
      fprintf(stderr, _("No certificates found in message %d.\n"), n);
      goto jleave;
   }

   for (i = 0; i < sk_X509_num(certs); ++i) {
      cert = sk_X509_value(certs, i);
      gens = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
      if (gens != NULL) {
         for (j = 0; j < sk_GENERAL_NAME_num(gens); ++j) {
            gen = sk_GENERAL_NAME_value(gens, j);
            if (gen->type == GEN_EMAIL) {
               if (options & OPT_VERB)
                  fprintf(stderr,
                     "Comparing subject_alt_name: need<%s> is<%s>\n",
                     sender, (char*)gen->d.ia5->data);
               if (!asccasecmp((char*)gen->d.ia5->data, sender))
                  goto jfound;
            }
         }
      }

      if ((subj = X509_get_subject_name(cert)) != NULL &&
            X509_NAME_get_text_by_NID(subj, NID_pkcs9_emailAddress,
               data, sizeof data) > 0) {
         data[sizeof data -1] = '\0';
         if (options & OPT_VERB)
            fprintf(stderr, "Comparing emailAddress: need<%s> is<%s>\n",
               sender, data);
         if (!asccasecmp(data, sender))
            goto jfound;
      }
   }
   fprintf(stderr, _("Message %d: certificate does not match <%s>\n"),
      n, sender);
   goto jleave;
jfound:
   rv = ((_ssl_state & SS_VERIFY_ERROR) != 0);
   if (!rv)
      printf(_("Message %d was verified successfully.\n"), n);
jleave:
   if (fb != NULL)
      BIO_free(fb);
   if (fp != NULL)
      Fclose(fp);
   NYD_LEAVE;
   return rv;
}

static EVP_CIPHER const *
_smime_cipher(char const *name)
{
   EVP_CIPHER const *cipher;
   char *vn;
   char const *cp;
   size_t i;
   NYD_ENTER;

   vn = ac_alloc(i = strlen(name) + sizeof("smime-cipher-") -1 +1);
   snprintf(vn, (int)i, "smime-cipher-%s", name);
   cp = vok_vlook(vn);
   ac_free(vn);

   if (cp == NULL) {
      cipher = _SMIME_DEFAULT_CIPHER();
      goto jleave;
   }
   cipher = NULL;

   for (i = 0; i < NELEM(_smime_ciphers); ++i)
      if (!asccasecmp(_smime_ciphers[i].sc_name, cp)) {
         cipher = (*_smime_ciphers[i].sc_fun)();
         goto jleave;
      }

   /* Not a builtin algorithm, but we may have dynamic support for more */
#ifdef HAVE_OPENSSL_ALL_ALGORITHMS
   if (!(_ssl_state & SS_ALGO_LOAD)) {
      _ssl_state |= SS_ALGO_LOAD;
      OpenSSL_add_all_algorithms();
      if (!(_ssl_state & SS_EXIT_HDL)) {
         _ssl_state |= SS_EXIT_HDL;
         atexit(&_ssl_atexit); /* TODO generic program-wide event mech. */
      }
   }

   if ((cipher = EVP_get_cipherbyname(cp)) != NULL)
      goto jleave;
#endif

   fprintf(stderr, _("Invalid cipher(s): %s\n"), cp);
jleave:
   NYD_LEAVE;
   return cipher;
}

static int
ssl_password_cb(char *buf, int size, int rwflag, void *userdata)
{
   char *pass;
   size_t len;
   NYD_ENTER;
   UNUSED(rwflag);
   UNUSED(userdata);

   if ((pass = getpassword("PEM pass phrase:")) != NULL) {
      len = strlen(pass);
      if (UICMP(z, len, >=, size))
         len = size -1;
      memcpy(buf, pass, len);
      buf[len] = '\0';
   } else
      len = 0;
   NYD_LEAVE;
   return (int)len;
}

static FILE *
smime_sign_cert(char const *xname, char const *xname2, bool_t dowarn)
{
   char *vn, *cp;
   int vs;
   struct name *np;
   char const *name = xname, *name2 = xname2;
   FILE *fp = NULL;
   NYD_ENTER;

jloop:
   if (name) {
      np = lextract(name, GTO | GSKIN);
      while (np != NULL) {
         /* This needs to be more intelligent since it will currently take the
          * first name for which a private key is available regardless of
          * whether it is the right one for the message */
         vn = ac_alloc(vs = strlen(np->n_name) + 30);
         snprintf(vn, vs, "smime-sign-cert-%s", np->n_name);
         cp = vok_vlook(vn);
         ac_free(vn);
         if (cp != NULL)
            goto jopen;
         np = np->n_flink;
      }
      if (name2 != NULL) {
         name = name2;
         name2 = NULL;
         goto jloop;
      }
   }

   if ((cp = ok_vlook(smime_sign_cert)) == NULL)
      goto jerr;
jopen:
   if ((cp = file_expand(cp)) == NULL)
      goto jleave;
   if ((fp = Fopen(cp, "r")) == NULL)
      perror(cp);
jleave:
   NYD_LEAVE;
   return fp;
jerr:
   if (dowarn)
      fprintf(stderr, _("Could not find a certificate for %s%s%s\n"),
         xname, (xname2 != NULL ? _("or ") : ""),
         (xname2 != NULL ? xname2 : ""));
   goto jleave;
}

static char *
_smime_sign_include_certs(char const *name)
{
   char *rv;
   NYD_ENTER;

   /* See comments in smime_sign_cert() for algorithm pitfalls */
   if (name != NULL) {
      struct name *np;

      for (np = lextract(name, GTO | GSKIN); np != NULL; np = np->n_flink) {
         int vs;
         char *vn = ac_alloc(vs = strlen(np->n_name) + 30);
         snprintf(vn, vs, "smime-sign-include-certs-%s", np->n_name);
         rv = vok_vlook(vn);
         ac_free(vn);
         if (rv != NULL)
            goto jleave;
      }
   }
   rv = ok_vlook(smime_sign_include_certs);
jleave:
   NYD_LEAVE;
   return rv;
}

static bool_t
_smime_sign_include_chain_creat(_STACKOF(X509) **chain, char const *cfiles)
{
   X509 *tmp;
   FILE *fp;
   char *nfield, *cfield, *x;
   NYD_ENTER;

   *chain = sk_X509_new_null();

   for (nfield = savestr(cfiles);
         (cfield = n_strsep(&nfield, ',', TRU1)) != NULL;) {
      if ((x = file_expand(cfield)) == NULL ||
            (fp = Fopen(cfield = x, "r")) == NULL) {
         perror(cfiles);
         goto jerr;
      }
      if ((tmp = PEM_read_X509(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
         ssl_gen_err(_("Error reading certificate from \"%s\""), cfield);
         Fclose(fp);
         goto jerr;
      }
      sk_X509_push(*chain, tmp);
      Fclose(fp);
   }

   if (sk_X509_num(*chain) == 0) {
      fprintf(stderr, _("*smime-sign-include-certs* defined but empty\n"));
      goto jerr;
   }
jleave:
   NYD_LEAVE;
   return (*chain != NULL);
jerr:
   sk_X509_pop_free(*chain, X509_free);
   *chain = NULL;
   goto jleave;
}

#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
static enum okay
load_crl1(X509_STORE *store, char const *name)
{
   X509_LOOKUP *lookup;
   enum okay rv = STOP;
   NYD_ENTER;

   if (options & OPT_VERB)
      fprintf(stderr, "Loading CRL from \"%s\".\n", name);
   if ((lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file())) == NULL) {
      ssl_gen_err(_("Error creating X509 lookup object"));
      goto jleave;
   }
   if (X509_load_crl_file(lookup, name, X509_FILETYPE_PEM) != 1) {
      ssl_gen_err(_("Error loading CRL from \"%s\""), name);
      goto jleave;
   }
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}
#endif /* new OpenSSL */

static enum okay
load_crls(X509_STORE *store, enum okeys fok, enum okeys dok)
{
   char *crl_file, *crl_dir;
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
   DIR *dirp;
   struct dirent *dp;
   char *fn = NULL;
   int fs = 0, ds, es;
#endif
   enum okay rv = STOP;
   NYD_ENTER;

   if ((crl_file = _var_oklook(fok)) != NULL) {
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
      if ((crl_file = file_expand(crl_file)) == NULL ||
            load_crl1(store, crl_file) != OKAY)
         goto jleave;
#else
      fprintf(stderr, _(
         "This OpenSSL version is too old to use CRLs.\n"));
      goto jleave;
#endif
   }

   if ((crl_dir = _var_oklook(dok)) != NULL) {
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
      char *x;
      if ((x = file_expand(crl_dir)) == NULL ||
            (dirp = opendir(crl_dir = x)) == NULL) {
         perror(crl_dir);
         goto jleave;
      }

      ds = strlen(crl_dir);
      fn = smalloc(fs = ds + 20);
      memcpy(fn, crl_dir, ds);
      fn[ds] = '/';
      while ((dp = readdir(dirp)) != NULL) {
         if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
               (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
            continue;
         if (dp->d_name[0] == '.')
            continue;
         if (ds + (es = strlen(dp->d_name)) + 2 < fs)
            fn = srealloc(fn, fs = ds + es + 20);
         memcpy(fn + ds + 1, dp->d_name, es + 1);
         if (load_crl1(store, fn) != OKAY) {
            closedir(dirp);
            free(fn);
            goto jleave;
         }
      }
      closedir(dirp);
      free(fn);
#else /* old OpenSSL */
      fprintf(stderr, _(
         "This OpenSSL version is too old to use CRLs.\n"));
      goto jleave;
#endif
   }
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
   if (crl_file || crl_dir)
      X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK |
         X509_V_FLAG_CRL_CHECK_ALL);
#endif
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
ssl_open(struct url const *urlp, struct sock *sp)
{
   char *cp;
   enum okay rv = STOP;
   NYD_ENTER;

   _ssl_init();

   ssl_set_verify_level(urlp);

   if ((sp->s_ctx = SSL_CTX_new(SSLv23_client_method())) == NULL) {
      ssl_gen_err(_("SSL_CTX_new() failed"));
      goto jleave;
   }

   /* Available with OpenSSL 0.9.6 or later */
#ifdef SSL_MODE_AUTO_RETRY
   SSL_CTX_set_mode(sp->s_ctx, SSL_MODE_AUTO_RETRY);
#endif

   if (!_ssl_ctx_conf(sp, urlp))
      goto jerr1;
   if (!_ssl_load_verifications(sp))
      goto jerr1;
   if (!_ssl_certificate(sp, urlp))
      goto jerr1;

   if ((cp = xok_vlook(ssl_cipher_list, urlp, OXM_ALL)) != NULL &&
         SSL_CTX_set_cipher_list(sp->s_ctx, cp) != 1) {
      ssl_gen_err(_("Invalid cipher(s): %s\n"), cp);
      goto jerr1;
   }

   if ((sp->s_ssl = SSL_new(sp->s_ctx)) == NULL) {
      ssl_gen_err(_("SSL_new() failed"));
      goto jerr1;
   }

   SSL_set_fd(sp->s_ssl, sp->s_fd);

   if (SSL_connect(sp->s_ssl) < 0) {
      ssl_gen_err(_("could not initiate SSL/TLS connection"));
      goto jerr2;
   }

   if (ssl_verify_level != SSL_VERIFY_IGNORE) {
      if (ssl_check_host(sp, urlp) != OKAY) {
         fprintf(stderr, _("Host certificate does not match \"%s\"\n"),
            urlp->url_h_p.s);
         if (ssl_verify_decide() != OKAY)
            goto jerr2;
      }
   }

   sp->s_use_ssl = 1;
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
jerr2:
   SSL_free(sp->s_ssl);
   sp->s_ssl = NULL;
jerr1:
   SSL_CTX_free(sp->s_ctx);
   sp->s_ctx = NULL;
   goto jleave;
}

FL void
ssl_gen_err(char const *fmt, ...)
{
   va_list ap;
   NYD_ENTER;

   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);

   fprintf(stderr, ": %s\n", ERR_error_string(ERR_get_error(), NULL));
   NYD_LEAVE;
}

FL int
c_verify(void *vp)
{
   int *msgvec = vp, *ip, ec = 0, rv = 1;
   _STACKOF(X509) *chain = NULL;
   X509_STORE *store;
   char *ca_dir, *ca_file;
   NYD_ENTER;

   _ssl_init();

   ssl_verify_level = SSL_VERIFY_STRICT;
   if ((store = X509_STORE_new()) == NULL) {
      ssl_gen_err(_("Error creating X509 store"));
      goto jleave;
   }
   X509_STORE_set_verify_cb_func(store, &_ssl_verify_cb);

   if ((ca_dir = ok_vlook(smime_ca_dir)) != NULL)
      ca_dir = file_expand(ca_dir);
   if ((ca_file = ok_vlook(smime_ca_file)) != NULL)
      ca_file = file_expand(ca_file);

   if (ca_dir != NULL || ca_file != NULL) {
      if (X509_STORE_load_locations(store, ca_file, ca_dir) != 1) {
         ssl_gen_err(_("Error loading %s"),
            (ca_file != NULL) ? ca_file : ca_dir);
         goto jleave;
      }
   }
   if (!ok_blook(smime_no_default_ca)) {
      if (X509_STORE_set_default_paths(store) != 1) {
         ssl_gen_err(_("Error loading default CA locations"));
         goto jleave;
      }
   }

   if (load_crls(store, ok_v_smime_crl_file, ok_v_smime_crl_dir) != OKAY)
      goto jleave;
   for (ip = msgvec; *ip != 0; ++ip) {
      struct message *mp = message + *ip - 1;
      setdot(mp);
      ec |= smime_verify(mp, *ip, chain, store);
   }
   if ((rv = ec) != 0)
      exit_status |= EXIT_ERR;
jleave:
   NYD_LEAVE;
   return rv;
}

FL FILE *
smime_sign(FILE *ip, char const *addr)
{
   FILE *rv = NULL, *sp = NULL, *fp = NULL, *bp, *hp;
   X509 *cert = NULL;
   _STACKOF(X509) *chain = NULL;
   PKCS7 *pkcs7;
   EVP_PKEY *pkey = NULL;
   BIO *bb, *sb;
   bool_t bail = FAL0;
   NYD_ENTER;

   _ssl_init();

   if (addr == NULL) {
      fprintf(stderr, _("No *from* address for signing specified\n"));
      goto jleave;
   }
   if ((fp = smime_sign_cert(addr, NULL, 1)) == NULL)
      goto jleave;

   if ((pkey = PEM_read_PrivateKey(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
      ssl_gen_err(_("Error reading private key from"));
      goto jleave;
   }

   rewind(fp);
   if ((cert = PEM_read_X509(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
      ssl_gen_err(_("Error reading signer certificate from"));
      goto jleave;
   }
   Fclose(fp);
   fp = NULL;

   if ((addr = _smime_sign_include_certs(addr)) != NULL &&
         !_smime_sign_include_chain_creat(&chain, addr))
      goto jleave;

   if ((sp = Ftmp(NULL, "smimesign", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600))
         == NULL) {
      perror("tempfile");
      goto jleave;
   }

   rewind(ip);
   if (smime_split(ip, &hp, &bp, -1, 0) == STOP) {
      bail = TRU1;
      goto jerr1;
   }

   sb = NULL;
   if ((bb = BIO_new_fp(bp, BIO_NOCLOSE)) == NULL ||
         (sb = BIO_new_fp(sp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(_("Error creating BIO signing objects"));
      bail = TRU1;
      goto jerr;
   }

   if ((pkcs7 = PKCS7_sign(cert, pkey, chain, bb, PKCS7_DETACHED)) == NULL) {
      ssl_gen_err(_("Error creating the PKCS#7 signing object"));
      bail = TRU1;
      goto jerr;
   }
   if (PEM_write_bio_PKCS7(sb, pkcs7) == 0) {
      ssl_gen_err(_("Error writing signed S/MIME data"));
      bail = TRU1;
      /*goto jerr*/
   }
jerr:
   if (sb != NULL)
      BIO_free(sb);
   if (bb != NULL)
      BIO_free(bb);
   if (!bail) {
      rewind(bp);
      fflush_rewind(sp);
      rv = smime_sign_assemble(hp, bp, sp);
   } else
jerr1:
      Fclose(sp);

jleave:
   if (chain != NULL)
      sk_X509_pop_free(chain, X509_free);
   if (cert != NULL)
      X509_free(cert);
   if (pkey != NULL)
      EVP_PKEY_free(pkey);
   if (fp != NULL)
      Fclose(fp);
   NYD_LEAVE;
   return rv;
}

FL FILE *
smime_encrypt(FILE *ip, char const *xcertfile, char const *to)
{
   char *certfile = UNCONST(xcertfile);
   FILE *rv = NULL, *yp, *fp, *bp, *hp;
   X509 *cert;
   PKCS7 *pkcs7;
   BIO *bb, *yb;
   _STACKOF(X509) *certs;
   EVP_CIPHER const *cipher;
   bool_t bail = FAL0;
   NYD_ENTER;

   if ((certfile = file_expand(certfile)) == NULL)
      goto jleave;

   _ssl_init();

   if ((cipher = _smime_cipher(to)) == NULL)
      goto jleave;
   if ((fp = Fopen(certfile, "r")) == NULL) {
      perror(certfile);
      goto jleave;
   }

   if ((cert = PEM_read_X509(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
      ssl_gen_err(_("Error reading encryption certificate from \"%s\""),
         certfile);
      bail = TRU1;
   }
   Fclose(fp);
   if (bail)
      goto jleave;
   bail = FAL0;

   certs = sk_X509_new_null();
   sk_X509_push(certs, cert);

   if ((yp = Ftmp(NULL, "smimeenc", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      perror("tempfile");
      goto jleave;
   }

   rewind(ip);
   if (smime_split(ip, &hp, &bp, -1, 0) == STOP) {
      Fclose(yp);
      goto jleave;
   }

   yb = NULL;
   if ((bb = BIO_new_fp(bp, BIO_NOCLOSE)) == NULL ||
         (yb = BIO_new_fp(yp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(_("Error creating BIO encryption objects"));
      bail = TRU1;
      goto jerr;
   }
   if ((pkcs7 = PKCS7_encrypt(certs, bb, cipher, 0)) == NULL) {
      ssl_gen_err(_("Error creating the PKCS#7 encryption object"));
      bail = TRU1;
      goto jerr;
   }
   if (PEM_write_bio_PKCS7(yb, pkcs7) == 0) {
      ssl_gen_err(_("Error writing encrypted S/MIME data"));
      bail = TRU1;
      /* goto jerr */
   }
jerr:
   if (bb != NULL)
      BIO_free(bb);
   if (yb != NULL)
      BIO_free(yb);
   Fclose(bp);
   if (bail)
      Fclose(yp);
   else {
      fflush_rewind(yp);
      rv = smime_encrypt_assemble(hp, yp);
   }
jleave:
   NYD_LEAVE;
   return rv;
}

FL struct message *
smime_decrypt(struct message *m, char const *to, char const *cc, int signcall)
{
   struct message *rv;
   FILE *fp, *bp, *hp, *op;
   X509 *cert;
   PKCS7 *pkcs7;
   EVP_PKEY *pkey;
   BIO *bb, *pb, *ob;
   long size;
   FILE *yp;
   NYD_ENTER;

   rv = NULL;
   cert = NULL;
   pkey = NULL;
   size = m->m_size;

   if ((yp = setinput(&mb, m, NEED_BODY)) == NULL)
      goto jleave;

   _ssl_init();

   if ((fp = smime_sign_cert(to, cc, 0)) != NULL) {
      pkey = PEM_read_PrivateKey(fp, NULL, &ssl_password_cb, NULL);
      if (pkey == NULL) {
         ssl_gen_err(_("Error reading private key"));
         Fclose(fp);
         goto jleave;
      }
      rewind(fp);

      if ((cert = PEM_read_X509(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
         ssl_gen_err(_("Error reading decryption certificate"));
         Fclose(fp);
         EVP_PKEY_free(pkey);
         goto jleave;
      }
      Fclose(fp);
   }

   if ((op = Ftmp(NULL, "smimedec", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      perror("tempfile");
      goto j_ferr;
   }

   if (smime_split(yp, &hp, &bp, size, 1) == STOP)
      goto jferr;

   if ((ob = BIO_new_fp(op, BIO_NOCLOSE)) == NULL ||
         (bb = BIO_new_fp(bp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(_("Error creating BIO decryption objects"));
      goto jferr;
   }
   if ((pkcs7 = SMIME_read_PKCS7(bb, &pb)) == NULL) {
      ssl_gen_err(_("Error reading PKCS#7 object"));
jferr:
      Fclose(op);
j_ferr:
      if (cert)
         X509_free(cert);
      if (pkey)
         EVP_PKEY_free(pkey);
      goto jleave;
   }

   if (PKCS7_type_is_signed(pkcs7)) {
      if (signcall) {
         setinput(&mb, m, NEED_BODY);
         rv = (struct message*)-1;
         goto jerr2;
      }
      if (PKCS7_verify(pkcs7, NULL, NULL, NULL, ob,
            PKCS7_NOVERIFY | PKCS7_NOSIGS) != 1)
         goto jerr;
      fseek(hp, 0L, SEEK_END);
      fprintf(hp, "X-Encryption-Cipher: none\n");
      fflush(hp);
      rewind(hp);
   } else if (pkey == NULL) {
      fprintf(stderr, _("No appropriate private key found.\n"));
      goto jerr2;
   } else if (cert == NULL) {
      fprintf(stderr, _("No appropriate certificate found.\n"));
      goto jerr2;
   } else if (PKCS7_decrypt(pkcs7, pkey, cert, ob, 0) != 1) {
jerr:
      ssl_gen_err(_("Error decrypting PKCS#7 object"));
jerr2:
      BIO_free(bb);
      BIO_free(ob);
      Fclose(op);
      Fclose(bp);
      Fclose(hp);
      if (cert != NULL)
         X509_free(cert);
      if (pkey != NULL)
         EVP_PKEY_free(pkey);
      goto jleave;
   }
   BIO_free(bb);
   BIO_free(ob);
   if (cert)
      X509_free(cert);
   if (pkey)
      EVP_PKEY_free(pkey);
   fflush_rewind(op);
   Fclose(bp);

   rv = smime_decrypt_assemble(m, hp, op);
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
smime_certsave(struct message *m, int n, FILE *op)
{
   struct message *x;
   char *to, *cc, *cnttype;
   int c, i;
   FILE *fp, *ip;
   off_t size;
   BIO *fb, *pb;
   PKCS7 *pkcs7;
   _STACKOF(X509) *certs, *chain = NULL;
   X509 *cert;
   enum okay rv = STOP;
   NYD_ENTER;

   _ssl_msgno = (size_t)n;
jloop:
   to = hfield1("to", m);
   cc = hfield1("cc", m);
   cnttype = hfield1("content-type", m);
   if ((ip = setinput(&mb, m, NEED_BODY)) == NULL)
      goto jleave;
   if (cnttype && !strncmp(cnttype, "application/x-pkcs7-mime", 24)) {
      if ((x = smime_decrypt(m, to, cc, 1)) == NULL)
         goto jleave;
      if (x != (struct message*)-1) {
         m = x;
         goto jloop;
      }
   }
   size = m->m_size;

   if ((fp = Ftmp(NULL, "smimecert", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600))
         == NULL) {
      perror("tempfile");
      goto jleave;
   }

   while (size-- > 0) {
      c = getc(ip);
      putc(c, fp);
   }
   fflush(fp);

   rewind(fp);
   if ((fb = BIO_new_fp(fp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err("Error creating BIO object for message %d", n);
      Fclose(fp);
      goto jleave;
   }

   if ((pkcs7 = SMIME_read_PKCS7(fb, &pb)) == NULL) {
      ssl_gen_err(_("Error reading PKCS#7 object for message %d"), n);
      BIO_free(fb);
      Fclose(fp);
      goto jleave;
   }
   BIO_free(fb);
   Fclose(fp);

   certs = PKCS7_get0_signers(pkcs7, chain, 0);
   if (certs == NULL) {
      fprintf(stderr, _("No certificates found in message %d\n"), n);
      goto jleave;
   }

   for (i = 0; i < sk_X509_num(certs); ++i) {
      cert = sk_X509_value(certs, i);
      if (X509_print_fp(op, cert) == 0 || PEM_write_X509(op, cert) == 0) {
         ssl_gen_err(_("Error writing certificate %d from message %d"),
            i, n);
         goto jleave;
      }
   }
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_OPENSSL */

/* s-it-mode */
