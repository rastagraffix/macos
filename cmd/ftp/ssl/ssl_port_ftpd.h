/* ssl_port_ftpd.h    - porting things for ftpd */
/*-
 * Copyright (c) 2002, 2004 Nick Leuta
 * All rights reserved.
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

#ifndef HEADER_SSL_PORT_FTPD_H
#define HEADER_SSL_PORT_FTPD_H

#include "ssl_port.h"

#ifdef USE_SSL

extern FILE	*cin, *cout;	/* ftpd local depends */

/* Compat flags */
extern int	ssl_apbu_flag;
extern int	ssl_uorc_flag;

/* User policy flags */
extern int	ssl_rpnu_flag;
extern int	ssl_dpau_flag;

/*
 * X.509 auth support
 */
extern int	good_ssl_user();
extern int	x509_auth_fallback_status();
extern int	x509_auth_flag;
extern int	x509_auth_ok;
/* X.509 auth levels */
/* X.509 auth is disabled (default) */
#define X509_AUTH_DISABLED	0
/* X.509 auth is sufficient, but fallback to standard is enabled */
#define X509_AUTH_SUFFICIENT	1
/* X.509 auth is required, fallback to standard is disabled */
#define X509_AUTH_REQUIRED	2
/* Both X.509 and standard auth are required */
#define X509_AND_STANDARD_AUTH	3

#endif /* USE_SSL */

#endif /*  HEADER_SSL_PORT_FTPD_H */
