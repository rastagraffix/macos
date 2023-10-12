/*
 * Copyright (c) 1988, 1993
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

#ifndef lint
#if 0
static char sccsid[] = "@(#)logwtmp.c	8.1 (Berkeley) 6/4/93";
#endif
#endif /* not lint */

#if 0
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/libexec/ftpd/logwtmp.c,v 1.13 2004/11/18 13:46:29 yar Exp $");
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <sys/param.h>
#if __FreeBSD_version < 900007
#include <fcntl.h>
#include <time.h>
#if 1 /* Original FreeBSD 5.0 code */
#include <timeconv.h>
#endif
#include <netdb.h>
#include <utmp.h>
#else
#include <utmpx.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <libutil.h>
#include "extern.h"

#include <port_base.h>

#ifndef _UTMPX_H_
static int fd = -1;

/*
 * Modified version of logwtmp that holds wtmp file open
 * after first call, for use with ftp (which may chroot
 * after login, but before logout).
 */
void
ftpd_logwtmp(line, name, addr)
	char *line, *name;
	struct sockaddr *addr;
{
	struct utmp ut;
	struct stat buf;
	char host[UT_HOSTSIZE];

	if (addr == NULL)
		host[0] = '\0';
	else
#ifdef LINUX /* Linux port */
		realhostname_sa(host, sizeof(host), addr, SA_LEN(addr));
#else /* BSD source */
		realhostname_sa(host, sizeof(host), addr, addr->sa_len);
#endif /* BSD source */

	if (fd < 0 && (fd = open(_PATH_WTMP, O_WRONLY|O_APPEND, 0)) < 0)
		return;
	if (fstat(fd, &buf) == 0) {
		(void)strncpy(ut.ut_line, line, sizeof(ut.ut_line));
		(void)strncpy(ut.ut_name, name, sizeof(ut.ut_name));
		(void)strncpy(ut.ut_host, host, sizeof(ut.ut_host));
#if 1 /* Original FreeBSD 5.0 code */
		ut.ut_time = _time_to_time32(time(NULL));
#else /* Portable code from FreeBSD 4.8 */
		(void)time(&ut.ut_time);
#endif
		if (write(fd, &ut, sizeof(struct utmp)) !=
		    sizeof(struct utmp))
			(void)ftruncate(fd, buf.st_size);
	}
}
#else /* Original FreeBSD 9.0 code */
void
ftpd_logwtmp(char *id, char *user, struct sockaddr *addr)
{
	struct utmpx ut;

	memset(&ut, 0, sizeof(ut));

	if (user != NULL) {
		/* Log in. */
		ut.ut_type = USER_PROCESS;
		(void)strncpy(ut.ut_user, user, sizeof(ut.ut_user));
		if (addr != NULL)
			realhostname_sa(ut.ut_host, sizeof(ut.ut_host),
			    addr, addr->sa_len);
	} else {
		/* Log out. */
		ut.ut_type = DEAD_PROCESS;
	}

	ut.ut_pid = getpid();
	gettimeofday(&ut.ut_tv, NULL);
	(void)strncpy(ut.ut_id, id, sizeof(ut.ut_id));
	(void)strncpy(ut.ut_line, "ftpd", sizeof(ut.ut_line));

	pututxline(&ut);
}
#endif
