/* ssl_port.c    - standard porting things */
/*-
 * Copyright (c) 2002, 2003, 2004 Nick Leuta
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

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <sys/param.h>

#include "ssl_port.h"
#include "sslapp.h"

#ifdef USE_SSL
/* Enable all modes */
char	ssl_secure_flags = (SSL_ENABLED | SSL_USE_NONSECURE);
#else /* !USE_SSL */
/* No TLS/SSL => only non-secure mode */
char	ssl_secure_flags = SSL_USE_NONSECURE;
#endif /* USE_SSL */

void
PRINTF(const char *fmt, ...)
{
    va_list ap;
#ifdef USE_SSL
    /* The size seems to be enough for normal use */
    char outputbuf[BUFSIZ + MAXPATHLEN + MAXHOSTNAMELEN];
#endif /*USE_SSL*/
    va_start(ap,fmt);
#ifdef USE_SSL
    (void)vsnprintf(outputbuf, sizeof(outputbuf), fmt, ap);
    if (ssl_active_flag) {
	    ssl_write(ssl_con, outputbuf, strlen(outputbuf));
    } else {
	    printf("%s", outputbuf);
	    fflush(stdout);
    }
#else /*!USE_SSL*/	
    vfprintf(stdout, fmt, ap);
#endif /*USE_SSL*/
    va_end(ap);
}

#ifdef USE_SSL

SSL	*ssl_data_con;
int	ssl_data_active_flag = 0;

/*
 * Data connection protection control for RFC2228 compliance
 */
/* RFC2228: default state is "Clear" */
int	ssl_encrypt_data = 0;	
/* RFC2228: PBSZ must be used before first PROT */
int	PBSZ_used_flag = 0;  
/* For compatibility with early implementations of FTP-SSL upgrade */
int	ssl_compat_flag = 0; 

/*
 * Wrapper around SSL_read(), arguments and return codes are the same.
 * This function handles SSL_ERROR_WANT_READ and SSL_ERROR_WANT_WRITE result
 * codes of TLS/SSL I/O operation.
 */
int
ssl_read(SSL *ssl, void *buf, int num)
{
    int ret, err = SSL_ERROR_NONE;

    do {
	    ret = SSL_read(ssl, buf, num);
	    if (ret <= 0) {
		    err = SSL_get_error(ssl, ret);
		    if (ssl_debug_flag)
			    ssl_log_msg(bio_err, "ssl_read(): SSL_ERROR %d", err);
	    }
    } while (ret<0 && (err==SSL_ERROR_WANT_READ || err==SSL_ERROR_WANT_WRITE));
    return ret;
}

/*
 * Wrapper around SSL_write(), arguments and return codes are the same.
 * This function handles SSL_ERROR_WANT_READ and SSL_ERROR_WANT_WRITE result
 * codes of TLS/SSL I/O operation.
 */
int
ssl_write(SSL *ssl, void *buf, int num)
{
    int ret, err = SSL_ERROR_NONE;

    do {
	    ret = SSL_write(ssl, buf, num);
	    if (ret <= 0) {
		    err = SSL_get_error(ssl, ret);
		    if (ssl_debug_flag)
			    ssl_log_msg(bio_err, "ssl_write(): SSL_ERROR %d", err);
	    }
    } while (ret<0 && (err==SSL_ERROR_WANT_READ || err==SSL_ERROR_WANT_WRITE));
    return ret;
}

int
ssl_getc(SSL *ssl_con)
{
    char onebyte;
    int ret;

    if ((ret = ssl_read(ssl_con, &onebyte, 1)) != 1) {
	    if (ssl_debug_flag || (ret < 0)) {
		    ssl_log_msgn(bio_err,
			"ssl_getc: ssl_read failed (SSL code: %d, errno: %d)\n",
			ret, errno);
	    }
	    return -1;
    } else {
	    if (ssl_debug_flag) {
		    ssl_log_msg(bio_err,
			"ssl_getc: ssl_read %d (%c) ", onebyte & 0xff,
			isprint(onebyte) ? onebyte : '.');
	    }
	    return onebyte & 0xff;
    }
}

/* got back to this an implemented some rather "simple" buffering */
static char	putc_buf[BUFSIZ];
static int	putc_buf_pos = 0;

int
ssl_putc_flush(SSL *ssl_con)
{
    if (putc_buf_pos > 0) {
	    if (ssl_write(ssl_con, putc_buf, putc_buf_pos) != putc_buf_pos) {
		if (ssl_debug_flag) 
			ssl_log_msgn(bio_err, "ssl_putc_flush: WRITE FAILED");
		putc_buf_pos = 0;
	        return -1;
	    }
    }
    putc_buf_pos = 0;
    return 0;
}

int
ssl_putc(SSL *ssl_con, int oneint)
{
    char onebyte;

    onebyte = oneint & 0xff;

    /* make sure there is space */
    if (putc_buf_pos >= sizeof(putc_buf)) 
	    if (ssl_putc_flush(ssl_con) != 0)
		    return EOF;
    putc_buf[putc_buf_pos++] = onebyte;

    return onebyte;
}

#endif /* USE_SSL */
