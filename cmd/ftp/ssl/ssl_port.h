/* ssl_port.h    - standard porting things */
/*-
 * Copyright (c) 2002, 2004 Nick Leuta
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

#ifndef HEADER_SSL_PORT_H
#define HEADER_SSL_PORT_H

/*
 * SSL security flags
 */
#define SSL_USE_NONSECURE	0x01 /* non-secure mode */
#define SSL_USE_COMPAT		0x02 /* FTP-SSL compatibility mode */
#define SSL_USE_TLS		0x04 /* RFC2228 compliant FTP-TLS mode */
#define SSL_ENABLED		0x06 /* SSL_USE_TLS | SSL_USE_COMPAT */

/* Binary inversion */
#define SSL_FINV(FLAG)			(FLAG^0xff)
/* Turn FLAG bits on */
#define SSL_secure_flags_ON(FLAG)	ssl_secure_flags|=FLAG
/* Turn FLAG bits off */
#define SSL_secure_flags_OFF(FLAG)	ssl_secure_flags&=SSL_FINV(FLAG)

extern char	ssl_secure_flags;

extern void	PRINTF(const char *fmt, ...);

#ifdef USE_SSL

#include <stdio.h>

#include <openssl/buffer.h>
#include <openssl/x509.h>
#include <openssl/ssl.h>

extern SSL	*ssl_data_con;

extern int	ssl_compat_flag;
extern int	PBSZ_used_flag;

extern int	ssl_encrypt_data;
extern int	ssl_data_active_flag;

extern int	ssl_read();
extern int	ssl_write();

extern int	ssl_putc();
extern int	ssl_putc_flush();
extern int	ssl_getc();

/* This cute macro makes things much easier to handle ... */
#define GETC(X)		(ssl_active_flag && (((X)==cin)||((X)==cout)) ? ssl_getc(ssl_con) : getc((X)))
#define PUTC(X,Y)	(ssl_active_flag && (((Y)==cin)||((Y)==cout)||((Y)==stdout)) ? ssl_putc(ssl_con,(X)) : putc((X),(Y)))
#define DATAGETC(X)	(ssl_data_active_flag && ((fileno(X)==data)||(fileno(X)==pdata)) ? ssl_getc(ssl_data_con) : getc((X)))
#define DATAPUTC(X,Y)	(ssl_data_active_flag && ((fileno(Y)==data)||(fileno(Y)==pdata)) ? ssl_putc(ssl_data_con,(X)) : putc((X),(Y)))
#define FFLUSH(X)	(ssl_active_flag && (((X)==cin)||((X)==cout)) ? 1 : fflush((X)) )
#define DATAFLUSH(X)	(ssl_data_active_flag && ((fileno(X)==data)||(fileno(X)==pdata)) ? ssl_putc_flush(ssl_data_con) : fflush((X)))

#else

#define GETC(X)		getc((X))
#define PUTC(X,Y)	putc((X),(Y))
#define DATAGETC(X)	getc((X))
#define DATAPUTC(X,Y)	putc((X),(Y))
#define FFLUSH(X)	fflush((X))
#define DATAFLUSH(X)	fflush((X))

#endif /* USE_SSL */

#endif /*  HEADER_SSL_PORT_H */
