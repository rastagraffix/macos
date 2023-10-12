/* ssl_port_ftps.c    - porting things for ftps */
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

#include "ssl_port_ftps.h"
#include "sslapp.h"
#include "../ftp/ftp_var.h"

/* Try to turn on the encryption of data connections during establishing a
 * secure connection with the server */
int	ssl_tryprot_flag = 1;

/*
 * Reinitialization of ssl flags, etc
 */
void
ssl_reinit()
{
    ssl_active_flag = 0;
    ssl_encrypt_data = 0;
    ssl_compat_flag = 0;
    PBSZ_used_flag = 0;
}

/*
 * Try to turn on encryption of data connections
 */
void
ssl_try_setprot()
{
    char *tmpstring[2];

    if (ssl_active_flag && !ssl_compat_flag && ssl_tryprot_flag) {
	    tmpstring[0] = "prot";
	    tmpstring[1] = "on";
	    setprot(2, tmpstring);
    }
}

#endif /* USE_SSL */
