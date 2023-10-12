/* sslapp.h	- ssl application code */
/*-
 * Copyright (c) 2002, 2003, 2004, 2005 Nick Leuta
 * All rights reserved.
 *
 * This software is based on code developed by Tim Hudson <tjh@cryptsoft.com>
 * for use in the SSLftp project.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef HEADER_SSLAPP_H
#define HEADER_SSLAPP_H

#ifdef USE_SSL

#include <stdio.h>

#ifdef LINUX
#include <stdarg.h>
#endif

#include <openssl/crypto.h>

#define SSL_set_pref_cipher(c,n)	SSL_set_cipher_list(c,n)
#define ONELINE_NAME(X)			X509_NAME_oneline(X,NULL,0)

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/x509.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define _PATH_X509_AUTH	"/etc/x509.auth"
#define _X509_AUTH_MAXSTRLEN	4096

extern BIO	*bio_err;
extern SSL	*ssl_con;
extern SSL_CTX	*ssl_ctx;
extern int	ssl_debug_flag;
extern int	ssl_active_flag;
extern int	ssl_verify_flag;
extern int	ssl_logerr_syslog;
extern int	ssl_verbose_flag;

extern char	*ssl_log_file; 
extern char	*ssl_cert_file; 
extern char	*ssl_key_file;
extern char	*ssl_cipher_list;
extern char	*ssl_CA_file;
extern char	*ssl_CA_path;
extern char	*ssl_CRL_file;
extern char	*ssl_CRL_path;

/* We hide all the initialisation code in a separate file now */
extern int	do_ssleay_init();

/* For TLS/SSL debugging purposes */
extern void	ssl_log_msg(BIO *bio, const char *fmt, ...);
extern void	ssl_log_msgn(BIO *bio, const char *fmt, ...);
extern void	ssl_log_vwarn(BIO *bio, const char *fmt, va_list ap);
extern void	ssl_log_warn(BIO *bio, const char *fmt, ...);
extern void	ssl_log_vwarn_debug(BIO *bio, const char *fmt, va_list ap);
extern void	ssl_log_warn_debug(BIO *bio, const char *fmt, ...);
extern void	ssl_log_err(BIO *bio, const char *fmt, ...);

extern int	ssl_X509_cmp(X509 *x509_cert1, X509 *x509_cert2);

extern X509_STORE	*ssl_X509_STORE_create(char *cpFile, char *cpPath);
extern int	verify_cb_CRL(int ok, X509_STORE_CTX *ctx);

extern int	ssl_x509_auth(char *service, X509 *cert, char *name);
extern int	ssl_X509_NAME_get_text_by_name(X509_NAME *x509_name,
		    char *obj_type, char *str, int len);

#endif /* USE_SSL */

#endif /*  HEADER_SSLAPP_H */
