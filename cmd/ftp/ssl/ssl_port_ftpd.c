/* ssl_port_ftpd.c    - porting things for ftpd */
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

#ifdef USE_SSL

#include "ssl_port_ftpd.h"
#include "sslapp.h"

/*
 * Compat flags
 */
/* Status of allowing PBSZ/PROT pair usage before USER/PASS pair */
int	ssl_apbu_flag = 0;
/* Use original (334) reply code instead of 234 */
int	ssl_uorc_flag = 0;

/*
 * User policy flags
 */
/* Require protection for non-anonymous users */
int	ssl_rpnu_flag = 0;
/* Disable protection for anonymous users */
int	ssl_dpau_flag = 0;

/*
 * X.509 auth support
 */
/* X.509 auth level */
int	x509_auth_flag = X509_AUTH_DISABLED;
/* Result of X.509 auth */
int	x509_auth_ok = 0;

/* Check status of fallback from X.509 to standard auth. Return codes are:
 * 1 - standard auth is required (default)
 * 0 - fallback is prohibited because cert-based auth is used. Reason is: X.509
 *     auth was successful and it is configured as any kind of sufficient auth
 *     type.
 */
int
x509_auth_fallback_status()
{
    if (ssl_active_flag && x509_auth_ok &&
	((x509_auth_flag == X509_AUTH_SUFFICIENT) ||
        (x509_auth_flag == X509_AUTH_REQUIRED)))
	    return 0;

    return 1;
}

#endif /* USE_SSL */
