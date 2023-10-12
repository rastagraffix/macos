/*
 * Copyright (c) 2002, 2003, 2004, 2005 Nick Leuta
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

/*
 * Definitions for porting to different operating systems.
 */

#ifndef	_PORT_BASE_H_
#define	_PORT_BASE_H_

/*
 * Linux porting things.
 */
#ifdef LINUX

/* MAXLOGNAME should be == UT_NAMESIZE+1 (see <utmp.h>)
 * There are the examples of the definitions in different operating systems.
 * In FreeBSD 4.x (defined in sys/param.h):
#define MAXLOGNAME 17
 * in Linux/Glibc-2.2:
#define MAXLOGNAME 33
 * We can't include <utmp.h>, because some its functions may have the same
 * names as another functions in FTP client. So we can to define the value
 * of UT_NAMESIZE like in examples above or to try next way (it works at least
 * with Glibc 2.2) */
#ifndef _UTMP_H
# define _UTMP_H
# include <bits/utmp.h>
# undef _UTMP_H
#endif
#define MAXLOGNAME UT_NAMESIZE+1

/*
 *	From FreeBSD's stdio.h
 */
#include <stdio.h>
#include <stdarg.h>

char	*fgetln(FILE *, size_t *);

/* Next functions are implemented in Linux systems. They are defined in stdio.h
 * and are available if _GNU_SOURCE is defined, for example:
#define _GNU_SOURCE
#include <stdio.h>
 * We can't use this way, because some functions from the set of GNU extensions
 * are in conflict with the functions with the same names from ftpd's code.
 *
 * It's possible to use "simple" form of definition like
int	 asprintf(char **, const char *, ...);
 * but we try to keep those definitions from glibc's headers. */
extern int vasprintf (char **__restrict __ptr, __const char *__restrict __f,
		      va_list __arg)
     __THROW __attribute__ ((__format__ (__printf__, 2, 0)));
extern int asprintf (char **__restrict __ptr,
		       __const char *__restrict __fmt, ...)
     __THROW __attribute__ ((__format__ (__printf__, 2, 3)));

/*
 *	From FreeBSD's stdlib.h
 */
void    *reallocf(void *, size_t);

/*
 *	From FreeBSD's string.h
 */
size_t	 strlcpy(char *, const char *, size_t);

/*
 *	From FreeBSD's sys/select.h
 */
#define	FD_COPY(f, t)	(void)(*(t) = *(f))

/*
 *	Special porting things.
 */
#define AI_ADDRLEN(HI) \
    HI->ai_addrlen - (HI->ai_family == AF_INET ? \
    sizeof(struct sockaddr) - __SOCKADDR_COMMON_SIZE - sizeof (in_port_t) - \
    sizeof(struct in_addr) : 0)

#ifdef INET6
#define SA_LEN(addr) \
    (addr)->sa_family == AF_INET ? sizeof(struct sockaddr_in) : \
    sizeof(struct sockaddr_in6)
#else /* !INET6 */
#define SA_LEN(addr) \
    sizeof(struct sockaddr_in)
#endif /* INET6 */

#ifdef INET6
#define SU_LEN(addr) \
    (addr).su_si.si_family == AF_INET ? sizeof(struct sockaddr_in) : \
    sizeof(struct sockaddr_in6)
#else /* !INET6 */
#define SU_LEN(addr) \
    sizeof(struct sockaddr_in)
#endif /* INET6 */

#endif /* LINUX */

/*
 * NetBSD porting things.
 */
#ifdef NETBSD

#include <sys/socket.h>
/*
 *	From FreeBSD's libutil.h
 */
extern int	realhostname_sa(char *host, size_t hsize, struct sockaddr *addr,
			     int addrlen);

/*
 *	From FreeBSD's stdlib.h
 */
void    *reallocf(void *, size_t);

#endif /* NETBSD */

#endif /* _PORT_BASE_H_ */
