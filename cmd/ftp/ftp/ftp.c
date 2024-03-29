/* $FreeBSD: src/usr.bin/ftp/ftp.c,v 1.28.2.5 2002/07/25 15:29:18 ume Exp $	*/
/*	$NetBSD: ftp.c,v 1.29.2.1 1997/11/18 01:01:04 mellon Exp $	*/

/*
 * Copyright (c) 2002, 2003, 2004 Nick Leuta
 *
 * TLS/SSL modifications are based on code developed by
 * Tim Hudson <tjh@cryptsoft.com> for use in the SSLftp project.
 *
 * Copyright (c) 1985, 1989, 1993, 1994
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

#include <sys/cdefs.h>

#ifndef lint
#if 0
static char sccsid[] = "@(#)ftp.c	8.6 (Berkeley) 10/27/94";
#else
static const char __RCSID[] = "$FreeBSD: src/usr.bin/ftp/ftp.c,v 1.28.2.5 2002/07/25 15:29:18 ume Exp $";
static const char __RCSID_SOURCE[] = "$NetBSD: ftp.c,v 1.29.2.1 1997/11/18 01:01:04 mellon Exp $";
#endif
#endif /* not lint */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <arpa/ftp.h>
#include <arpa/telnet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

#ifdef LINUX
#include <signal.h>
#endif /* LINUX */

#include "ftp_var.h"

int	data = -1;
#ifdef USE_SSL
int	pdata = -1; /* stub */
#endif /* USE_SSL */
int	abrtflag = 0;
sigjmp_buf	ptabort;
int	ptabflg;
int	ptflag = 0;

FILE	*cin, *cout;

union sockunion {
	struct sockinet {
/* If we don't have the sin_len member, but we want to use si_len, we must put
 * si_len at the end. WARNING! In this case we must to initialize si_len by the
 * value of sizeof(struct sockaddr_in) or sizeof(struct sockaddr_in6) every
 * time than it will be initialized by correspondent library function in
 * sin_len-aware systems.
 */
#ifndef LINUX /* BSD source */
		u_char si_len;
		u_char si_family;
		u_short si_port;
#else /* Linux port */
		sa_family_t si_family;
		unsigned short int si_port;
#ifdef INET6
		char padding[sizeof(struct sockaddr_in6) - sizeof(sa_family_t) -
			     sizeof(unsigned short int)];
#else /* !INET6 */
		char padding[sizeof(struct sockaddr_in) - sizeof(sa_family_t) -
			     sizeof(unsigned short int)];
#endif /* INET6 */
/* Uncomment next line only if you really want to use si_len */
/*		u_char si_len;*/
#endif /* Linux port */
	} su_si;
	struct sockaddr_in  su_sin;
#ifdef INET6
	struct sockaddr_in6 su_sin6;
#endif /* INET6 */
};
#define su_len		su_si.si_len
#define su_family	su_si.si_family
#define su_port		su_si.si_port

union sockunion myctladdr, hisctladdr, data_addr;

char *
hookup(host0, port)
	const char *host0;
	char *port;
{
	socklen_t len;
	int s, tos, error;
	struct addrinfo hints, *res, *res0;
	static char hostnamebuf[MAXHOSTNAMELEN];
	static char hostaddrbuf[NI_MAXHOST];
	char *host;

	if (*host0 == '[' && strrchr(host0, ']') != NULL) { /*IPv6 addr in []*/
		strncpy(hostnamebuf, host0 + 1, strlen(host0) - 2);
		hostnamebuf[strlen(host0) - 2] = '\0';
	} else {
		strncpy(hostnamebuf, host0, strlen(host0));
		hostnamebuf[strlen(host0)] = '\0';
	}
	host = hostnamebuf;
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_CANONNAME;
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	error = getaddrinfo(host, port, &hints, &res0);
	if (error) {
		warnx("%s: %s", host, gai_strerror(error));
		if (error == EAI_SYSTEM)
			warnx("%s: %s", host, strerror(errno));
		code = -1;
		return (0);
	}

	res = res0;
	if (res->ai_canonname)
		(void) strncpy(hostnamebuf, res->ai_canonname,
			       sizeof(hostnamebuf));
	hostname = hostnamebuf;
	while (1) {
		/*
		 * make sure that ai_addr is NOT an IPv4 mapped address.
		 * IPv4 mapped address complicates too many things in FTP
		 * protocol handling, as FTP protocol is defined differently
		 * between IPv4 and IPv6.
		 */
		ai_unmapped(res);
		s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (s < 0) {
			res = res->ai_next;
			if (res)
				continue;
			warn("socket");
			code = -1;
			return (0);
		}
		if (dobind) {
			struct addrinfo *bindres;
			int binderr = -1;

			for (bindres = bindres0;
			     bindres != NULL;
			     bindres = bindres->ai_next)
				if (bindres->ai_family == res->ai_family)
					break;
			if (bindres == NULL)
				bindres = bindres0;
			binderr = bind(s, bindres->ai_addr,
				       bindres->ai_addrlen);
			if (binderr == -1)
		      {
			res = res->ai_next;
			if (res) {
				(void)close(s);
				continue;
			}
			warn("bind");
			code = -1;
			goto bad;
		      }
		}
		if (connect(s, res->ai_addr, res->ai_addrlen) == 0)
			break;
		if (res->ai_next) {
			char hname[NI_MAXHOST];
			getnameinfo(res->ai_addr, res->ai_addrlen,
				    hname, sizeof(hname) - 1, NULL, 0,
				    NI_NUMERICHOST);
			warn("connect to address %s", hname);
			res = res->ai_next;
			getnameinfo(res->ai_addr, res->ai_addrlen,
				    hname, sizeof(hname) - 1, NULL, 0,
				    NI_NUMERICHOST);
			printf("Trying %s...\n", hname);
			(void)close(s);
			continue;
		}
		warn("connect");
		code = -1;
		goto bad;
	}
	memcpy(&hisctladdr, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res0);

	if (!getnameinfo((struct sockaddr *)&hisctladdr,
#ifdef LINUX /* Linux port */
	    SU_LEN(hisctladdr),
#else /* BSD source */
	    hisctladdr.su_len,
#endif /* BSD source */
	    hostaddrbuf, sizeof(hostaddrbuf) - 1, NULL, 0, NI_NUMERICHOST))
		hostaddr = hostaddrbuf;
	else {
		hostaddr = NULL;
		warnx("Abnormal error: connected to host with undeterminable address.");
		code = -1;
		goto bad;
	}

	len = sizeof(myctladdr);
	if (getsockname(s, (struct sockaddr *)&myctladdr, &len) < 0) {
		warn("getsockname");
		code = -1;
		goto bad;
	}
#ifdef IP_TOS
	if (myctladdr.su_family == AF_INET)
      {
	     tos = IPTOS_LOWDELAY;
	if (setsockopt(s, IPPROTO_IP, IP_TOS, (char *)&tos, sizeof(int)) < 0)
		warn("setsockopt TOS (ignored)");
      }
#endif
	cin = fdopen(s, "r");
	cout = fdopen(s, "w");
	if (cin == NULL || cout == NULL) {
		warnx("fdopen failed.");
		if (cin)
			(void)fclose(cin);
		if (cout)
			(void)fclose(cout);
		code = -1;
		goto bad;
	}
	if (verbose)
		printf("Connected to %s.\n", hostname);
	if (getreply(0) > 2) { 	/* read startup message from server */
		if (cin)
			(void)fclose(cin);
		if (cout)
			(void)fclose(cout);
		code = -1;
		goto bad;
	}
#ifdef SO_OOBINLINE
	{
	int on = 1;

	if (setsockopt(s, SOL_SOCKET, SO_OOBINLINE, (char *)&on, sizeof(on))
		< 0 && debug) {
			warn("setsockopt");
		}
	}
#endif /* SO_OOBINLINE */

	return (hostname);
bad:
	(void)close(s);
	return (NULL);
}

void
cmdabort(notused)
	int notused;
{

	alarmtimer(0);
	putchar('\n');
	(void)fflush(stdout);
	abrtflag++;
	if (ptflag)
		siglongjmp(ptabort, 1);
}

/*VARARGS*/
int
command(const char *fmt, ...)
{
	va_list ap;
	int r;
	sig_t oldintr;

#ifdef USE_SSL
	/* the size seems to be enough for normal use */
        char outputbuf[BUFSIZ + MAXPATHLEN + MAXHOSTNAMELEN];
#endif /* USE_SSL */

	abrtflag = 0;
	if (debug) {
		fputs("---> ", stdout);
		va_start(ap, fmt);
		if (strncmp("PASS ", fmt, 5) == 0)
			fputs("PASS XXXX", stdout);
		else if (strncmp("ACCT ", fmt, 5) == 0)
			fputs("ACCT XXXX", stdout);
		else
			vprintf(fmt, ap);
		va_end(ap);
		putchar('\n');
		(void)fflush(stdout);
	}
	if (cout == NULL) {
		warnx("No control connection for command.");
		code = -1;
		return (0);
	}
	oldintr = signal(SIGINT, cmdabort);
	va_start(ap, fmt);

#ifdef USE_SSL
        /* assemble the output into a buffer */
	vsnprintf(outputbuf, sizeof(outputbuf) - 2, fmt, ap);
	strcat(outputbuf, "\r\n");
	if (ssl_active_flag) {
		ssl_write(ssl_con, outputbuf, strlen(outputbuf));
        } else {
		fprintf(cout, "%s", outputbuf);
		fflush(cout);
	}
#else /* !USE_SSL */
	vfprintf(cout, fmt, ap);
#endif /* USE_SSL */

	va_end(ap);

#ifndef USE_SSL
	fputs("\r\n", cout);
	(void)fflush(cout);
#endif/* !USE_SSL */

	cpend = 1;
	r = getreply(!strcmp(fmt, "QUIT"));
	if (abrtflag && oldintr != SIG_IGN)
		(*oldintr)(SIGINT);
	(void)signal(SIGINT, oldintr);
	return (r);
}

char reply_string[BUFSIZ];		/* first line of previous reply */

int
getreply(expecteof)
	int expecteof;
{
	char current_line[BUFSIZ];	/* last line of previous reply */
	int c, n, line;
	int dig;
	int originalcode = 0, continuation = 0;
	sig_t oldintr;
	int pflag = 0;
	char *cp, *pt = pasv;
	char buf[16];

	oldintr = signal(SIGINT, cmdabort);
	for (line = 0 ;; line++) {
		dig = n = code = 0;
		cp = current_line;
		while ((c = GETC(cin)) != '\n') {
			if (c == IAC) {     /* handle telnet commands */
				switch (c = GETC(cin)) {
				case WILL:
				case WONT:
					c = GETC(cin);
					snprintf(buf, sizeof(buf),
						 "%c%c%c", IAC, DONT, c);

#ifdef USE_SSL
					if (ssl_active_flag)
						ssl_write(ssl_con, buf, 3);
					else
#endif /* USE_SSL */
						fwrite(buf, 3, 1, cout);
					(void)FFLUSH(cout);
					break;
				case DO:
				case DONT:
					c = GETC(cin);
					snprintf(buf, sizeof(buf),
						 "%c%c%c", IAC, WONT, c);

#ifdef USE_SSL
					if (ssl_active_flag)
						ssl_write(ssl_con, buf, 3);
					else
#endif /* USE_SSL */
						fwrite(buf, 3, 1, cout);
					(void)FFLUSH(cout);
					break;
				default:
					break;
				}
				continue;
			}
			dig++;
			if (c == EOF) {
				if (expecteof) {
					(void)signal(SIGINT, oldintr);
					code = 221;
					return (0);
				}
				lostpeer();
				if (verbose) {
					puts(
"421 Service not available, remote server has closed connection.");
					(void)fflush(stdout);
				}
				code = 421;
				return (4);
			}
			if (c != '\r' && (verbose > 0 ||
			    (verbose > -1 && n == '5' && dig > 4))) {
				if (proxflag &&
				   (dig == 1 || (dig == 5 && verbose == 0)))
					printf("%s:", hostname);
				(void)putchar(c);
			}
			if (dig < 4 && isdigit((unsigned char)c))
				code = code * 10 + (c - '0');
			switch (pflag) {
			case 0:
				if (code == 227 || code == 228) {
					/* result for PASV/LPSV */
					pflag = 1;
					/* fall through */
				} else if (code == 229) {
					/* result for EPSV */
					pflag = 100;
					break;
				} else
					break;
			case 1:
				if (!(dig > 4 && isdigit((unsigned char)c)))
					break;
				pflag = 2;
				/* fall through */
			case 2:
				if (c != '\r' && c != ')' &&
				    pt < &pasv[sizeof(pasv)-1])
					*pt++ = c;
				else {
					*pt = '\0';
					pflag = 3;
				}
				break;
			case 100:
				if (dig > 4 && c == '(')
					pflag = 2;
				break;
			}
			if (dig == 4 && c == '-') {
				if (continuation)
					code = 0;
				continuation++;
			}
			if (n == 0)
				n = c;
			if (cp < &current_line[sizeof(current_line) - 1])
				*cp++ = c;
		}
		if (verbose > 0 || (verbose > -1 && n == '5')) {
			(void)putchar(c);
			(void)fflush (stdout);
		}
		if (line == 0) {
			size_t len = cp - current_line;

			if (len > sizeof(reply_string))
				len = sizeof(reply_string);

			(void)strncpy(reply_string, current_line, len);
			reply_string[len] = '\0';
		}
		if (continuation && code != originalcode) {
			if (originalcode == 0)
				originalcode = code;
			continue;
		}
		*cp = '\0';
		if (n != '1')
			cpend = 0;
		(void)signal(SIGINT, oldintr);
		if (code == 421 || originalcode == 421)
			lostpeer();
		if (abrtflag && oldintr != cmdabort && oldintr != SIG_IGN)
			(*oldintr)(SIGINT);
		return (n - '0');
	}
}

int
empty(mask, sec)
	fd_set *mask;
	int sec;
{
	struct timeval t;

	t.tv_sec = (long) sec;
	t.tv_usec = 0;
	return (select(32, mask, NULL, NULL, &t));
}

sigjmp_buf	sendabort;

void
abortsend(notused)
	int notused;
{

	alarmtimer(0);
	mflag = 0;
	abrtflag = 0;
	puts("\nsend aborted\nwaiting for remote to finish abort.");
	(void)fflush(stdout);
	siglongjmp(sendabort, 1);
}

void
sendrequest(cmd, local, remote, printnames)
	const char *cmd, *local, *remote;
	int printnames;
{
	struct stat st;
	int c, d;
	FILE *fin, *dout;
	int (*closefunc) __P((FILE *));
	sig_t oldinti, oldintr, oldintp;
	volatile off_t hashbytes;
	char *lmode, buf[BUFSIZ], *bufp;
	int oprogress;

#ifdef __GNUC__			/* XXX: to shut up gcc warnings */
	(void)&fin;
	(void)&dout;
	(void)&closefunc;
	(void)&oldinti;
	(void)&oldintr;
	(void)&oldintp;
	(void)&lmode;
#endif

	hashbytes = mark;
	direction = "sent";
	dout = NULL;
	bytes = 0;
	filesize = -1;
	oprogress = progress;
	if (verbose && printnames) {
		if (local && *local != '-')
			printf("local: %s ", local);
		if (remote)
			printf("remote: %s\n", remote);
	}
	if (proxy) {
		proxtrans(cmd, local, remote);
		return;
	}
	if (curtype != type)
		changetype(type, 0);
	closefunc = NULL;
	oldintr = NULL;
	oldintp = NULL;
	oldinti = NULL;
	lmode = "w";
	if (sigsetjmp(sendabort,1)) {
		while (cpend) {
			(void)getreply(0);
		}
		if (data >= 0) {
			(void)close(data);
			data = -1;
		}
		if (oldintr)
			(void)signal(SIGINT, oldintr);
		if (oldintp)
			(void)signal(SIGPIPE, oldintp);
#ifndef LINUX /* BSD source */
		if (oldinti)
			(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
		code = -1;
		goto cleanupsend;
	}
	oldintr = signal(SIGINT, abortsend);
#ifndef LINUX /* BSD source */
	oldinti = signal(SIGINFO, psummary);
#endif /* BSD source */
	if (strcmp(local, "-") == 0) {
		fin = stdin;
		progress = 0;
	} else if (*local == '|') {
		oldintp = signal(SIGPIPE, SIG_IGN);
		fin = popen(local + 1, "r");
		if (fin == NULL) {
			warn("%s", local + 1);
			(void)signal(SIGINT, oldintr);
			(void)signal(SIGPIPE, oldintp);
#ifndef LINUX /* BSD source */
			(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
			code = -1;
			goto cleanupsend;
		}
		progress = 0;
		closefunc = pclose;
	} else {
		fin = fopen(local, "r");
		if (fin == NULL) {
			warn("local: %s", local);
			(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
			(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
			code = -1;
			goto cleanupsend;
		}
		closefunc = fclose;
		if (fstat(fileno(fin), &st) < 0 || !S_ISREG(st.st_mode)) {
			printf("%s: not a plain file.\n", local);
			(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
			(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
			fclose(fin);
			code = -1;
			goto cleanupsend;
		}
		filesize = st.st_size;
	}
	if (initconn()) {
		(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
		(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
		if (oldintp)
			(void)signal(SIGPIPE, oldintp);
		code = -1;
		if (closefunc != NULL)
			(*closefunc)(fin);
		goto cleanupsend;
	}
	if (sigsetjmp(sendabort,1))
		goto abort;

	if (restart_point &&
	    (strcmp(cmd, "STOR") == 0 || strcmp(cmd, "APPE") == 0)) {
		int rc;

		rc = -1;
		switch (curtype) {
		case TYPE_A:
			rc = fseeko(fin, restart_point, SEEK_SET);
			break;
		case TYPE_I:
		case TYPE_L:
			rc = lseek(fileno(fin), restart_point, SEEK_SET);
			break;
		}
		if (rc < 0) {
			warn("local: %s", local);
			if (closefunc != NULL)
				(*closefunc)(fin);
			goto cleanupsend;
		}
		if (command("REST %jd", (intmax_t)restart_point) !=
		    CONTINUE) {
			if (closefunc != NULL)
				(*closefunc)(fin);
			goto cleanupsend;
		}
		lmode = "r+w";
	}
	if (remote) {
		if (command("%s %s", cmd, remote) != PRELIM) {
			(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
			(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
			if (oldintp)
				(void)signal(SIGPIPE, oldintp);
			if (closefunc != NULL)
				(*closefunc)(fin);
			goto cleanupsend;
		}
	} else
		if (command("%s", cmd) != PRELIM) {
			(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
			(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
			if (oldintp)
				(void)signal(SIGPIPE, oldintp);
			if (closefunc != NULL)
				(*closefunc)(fin);
			goto cleanupsend;
		}
	dirchange = 1;
	dout = dataconn(lmode);
	if (dout == NULL)
		goto abort;
	progressmeter(-1);
	oldintp = signal(SIGPIPE, SIG_IGN);
	switch (curtype) {

	case TYPE_I:
	case TYPE_L:
		errno = d = 0;
		while ((c = read(fileno(fin), buf, sizeof(buf))) > 0) {
			bytes += c;
#ifdef USE_SSL
                        if (ssl_data_active_flag) {
				for (bufp = buf; c > 0; c -= d, bufp += d)
					if ((d = ssl_write(ssl_data_con, bufp, c)) <= 0)
						break;
			} else 
#endif /* USE_SSL */
			{
			for (bufp = buf; c > 0; c -= d, bufp += d)
				if ((d = write(fileno(dout), bufp, c)) <= 0)
					break;
			}
			if (hash && (!progress || filesize < 0) ) {
				while (bytes >= hashbytes) {
					(void)putchar('#');
					hashbytes += mark;
				}
				(void)fflush(stdout);
			}
		}
		if (hash && (!progress || filesize < 0) && bytes > 0) {
			if (bytes < mark)
				(void)putchar('#');
			(void)putchar('\n');
			(void)fflush(stdout);
		}
		if (c < 0)
			warn("local: %s", local);
		if (d < 0) {
			if (errno != EPIPE)
				warn("netout");
			bytes = -1;
		}
		break;

	case TYPE_A:
		while ((c = getc(fin)) != EOF) {
			if (c == '\n') {
				while (hash && (!progress || filesize < 0) &&
				    (bytes >= hashbytes)) {
					(void)putchar('#');
					(void)fflush(stdout);
					hashbytes += mark;
				}
				if (ferror(dout))
					break;
				(void)DATAPUTC('\r', dout);
				bytes++;
			}
			(void)DATAPUTC(c, dout);
			bytes++;
#if 0	/* this violates RFC */
			if (c == '\r') {
				(void)putc('\0', dout);
				bytes++;
			}
#endif
		}
		/* there wasn't a fflush here ... which is a little
		 * funny ... one shouldn't hurt though --tjh
		 */
		DATAFLUSH(dout);
		if (hash && (!progress || filesize < 0)) {
			if (bytes < hashbytes)
				(void)putchar('#');
			(void)putchar('\n');
			(void)fflush(stdout);
		}
		if (ferror(fin))
			warn("local: %s", local);
		if (ferror(dout)) {
			if (errno != EPIPE)
				warn("netout");
			bytes = -1;
		}
		break;
	}
	progressmeter(1);
	if (closefunc != NULL)
		(*closefunc)(fin);
	(void)fclose(dout);

#ifdef USE_SSL
	if (ssl_data_active_flag && (ssl_data_con != NULL)) {
		SSL_free(ssl_data_con);
		ssl_data_active_flag = 0;
		ssl_data_con = NULL;
	}
#endif /* USE_SSL */

	(void)getreply(0);
	(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
	(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
	if (oldintp)
		(void)signal(SIGPIPE, oldintp);
	if (bytes > 0)
		ptransfer(0);
	goto cleanupsend;
abort:
	(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
	(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
	if (oldintp)
		(void)signal(SIGPIPE, oldintp);
	if (!cpend) {
		code = -1;
		return;
	}
	if (data >= 0) {
		(void)close(data);
		data = -1;
	}
	if (dout)
		(void)fclose(dout);

#ifdef USE_SSL
	if (ssl_data_active_flag && (ssl_data_con != NULL)) {
		SSL_free(ssl_data_con);
		ssl_data_active_flag = 0;
		ssl_data_con = NULL;
	}
#endif /* USE_SSL */

	(void)getreply(0);
	code = -1;
	if (closefunc != NULL && fin != NULL)
		(*closefunc)(fin);
	if (bytes > 0)
		ptransfer(0);
cleanupsend:
	progress = oprogress;
	restart_point = 0;
}

sigjmp_buf	recvabort;

void
abortrecv(notused)
	int notused;
{

	alarmtimer(0);
	mflag = 0;
	abrtflag = 0;
	puts("\nreceive aborted\nwaiting for remote to finish abort.");
	(void)fflush(stdout);
	siglongjmp(recvabort, 1);
}

void
recvrequest(cmd, local, remote, lmode, printnames, ignorespecial)
	const char *cmd, *local, *remote, *lmode;
	int printnames, ignorespecial;
{
	FILE *fout, *din;
	int (*closefunc) __P((FILE *));
	sig_t oldinti, oldintr, oldintp;
	int c, d;
	volatile int is_retr, tcrflag, bare_lfs;
	static size_t bufsize;
	static char *buf;
	volatile off_t hashbytes;
	struct stat st;
	time_t mtime;
	struct timeval tval[2];
	int oprogress;
	int opreserve;

#ifdef __GNUC__			/* XXX: to shut up gcc warnings */
	(void)&local;
	(void)&fout;
	(void)&din;
	(void)&closefunc;
	(void)&oldinti;
	(void)&oldintr;
	(void)&oldintp;
#endif

	fout = NULL;
	din = NULL;
	oldinti = NULL;
	hashbytes = mark;
	direction = "received";
	bytes = 0;
	bare_lfs = 0;
	filesize = -1;
	oprogress = progress;
	opreserve = preserve;
	is_retr = (strcmp(cmd, "RETR") == 0);
	if (is_retr && verbose && printnames) {
		if (local && (ignorespecial || *local != '-'))
			printf("local: %s ", local);
		if (remote)
			printf("remote: %s\n", remote);
	}
	if (proxy && is_retr) {
		proxtrans(cmd, local, remote);
		return;
	}
	closefunc = NULL;
	oldintr = NULL;
	oldintp = NULL;
	tcrflag = !crflag && is_retr;
	if (sigsetjmp(recvabort,1)) {
		while (cpend) {
			(void)getreply(0);
		}
		if (data >= 0) {
			(void)close(data);
			data = -1;
		}
		if (oldintr)
			(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
		if (oldinti)
			(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
		progress = oprogress;
		preserve = opreserve;
		code = -1;
		return;
	}
	oldintr = signal(SIGINT, abortrecv);
#ifndef LINUX /* BSD source */
	oldinti = signal(SIGINFO, psummary);
#endif /* BSD source */
	if (ignorespecial || (strcmp(local, "-") && *local != '|')) {
		if (access(local, W_OK) < 0) {
			char *dir = strrchr(local, '/');

			if (errno != ENOENT && errno != EACCES) {
				warn("local: %s", local);
				(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
				(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
				code = -1;
				return;
			}
			if (dir != NULL)
				*dir = 0;
			d = access(dir == local ? "/" : dir ? local : ".", W_OK);
			if (dir != NULL)
				*dir = '/';
			if (d < 0) {
				warn("local: %s", local);
				(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
				(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
				code = -1;
				return;
			}
			if (!runique && errno == EACCES &&
			    chmod(local, 0600) < 0) {
				warn("local: %s", local);
				(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
				(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
				code = -1;
				return;
			}
			if (runique && errno == EACCES &&
			   (local = gunique(local)) == NULL) {
				(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
				(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
				code = -1;
				return;
			}
		}
		else if (runique && (local = gunique(local)) == NULL) {
			(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
			(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
			code = -1;
			return;
		}
	}
	if (!is_retr) {
		if (curtype != TYPE_A)
			changetype(TYPE_A, 0);
	} else {
		if (curtype != type)
			changetype(type, 0);
		filesize = remotesize(remote, 0);
	}
	if (initconn()) {
		(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
		(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
		code = -1;
		return;
	}
	if (sigsetjmp(recvabort,1))
		goto abort;
	if (is_retr && restart_point &&
	    command("REST %jd", (intmax_t)restart_point) != CONTINUE)
		return;
	if (remote) {
		if (command("%s %s", cmd, remote) != PRELIM) {
			(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
			(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
			return;
		}
	} else {
		if (command("%s", cmd) != PRELIM) {
			(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
			(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
			return;
		}
	}
	din = dataconn("r");
	if (din == NULL)
		goto abort;
	if (!ignorespecial && strcmp(local, "-") == 0) {
		fout = stdout;
		progress = 0;
		preserve = 0;
	} else if (!ignorespecial && *local == '|') {
		oldintp = signal(SIGPIPE, SIG_IGN);
		fout = popen(local + 1, "w");
		if (fout == NULL) {
			warn("%s", local+1);
			goto abort;
		}
		progress = 0;
		preserve = 0;
		closefunc = pclose;
	} else {
		fout = fopen(local, lmode);
		if (fout == NULL) {
			warn("local: %s", local);
			goto abort;
		}
		closefunc = fclose;
	}
	if (fstat(fileno(fout), &st) < 0 || st.st_blksize == 0)
		st.st_blksize = BUFSIZ;
	if (st.st_blksize > bufsize) {
		if (buf)
			(void)free(buf);
		buf = malloc((unsigned)st.st_blksize);
		if (buf == NULL) {
			warn("malloc");
			bufsize = 0;
			goto abort;
		}
		bufsize = st.st_blksize;
	}
	if (!S_ISREG(st.st_mode)) {
		progress = 0;
		preserve = 0;
	}
	progressmeter(-1);
	switch (curtype) {

	case TYPE_I:
	case TYPE_L:
		if (is_retr && restart_point &&
		    lseek(fileno(fout), restart_point, SEEK_SET) < 0) {
			warn("local: %s", local);
			progress = oprogress;
			preserve = opreserve;
			if (closefunc != NULL)
				(*closefunc)(fout);
			return;
		}
		errno = d = 0;
#ifdef USE_SSL
                if (ssl_data_active_flag) {
			while ((c = ssl_read(ssl_data_con, buf, bufsize)) > 0) {
				if ((d = write(fileno(fout), buf, c)) != c)
					break;
				bytes += c;
				if (hash) {
					while (bytes >= hashbytes) {
						putchar('#');
						hashbytes += HASHBYTES;
					}
					fflush(stdout);
				}
			}
			if (c < -1) {
				/* tell the user ... who else */
				ssl_log_err(bio_err, "ssl_read DATA error");
				warnx("TLS/SSL DATA connection error");
			}
		} else 
#endif /* USE_SSL */
		{
		while ((c = read(fileno(din), buf, bufsize)) > 0) {
			if ((d = write(fileno(fout), buf, c)) != c)
				break;
			bytes += c;
			if (hash && (!progress || filesize < 0)) {
				while (bytes >= hashbytes) {
					(void)putchar('#');
					hashbytes += mark;
				}
				(void)fflush(stdout);
			}
		}
		}
		if (hash && (!progress || filesize < 0) && bytes > 0) {
			if (bytes < mark)
				(void)putchar('#');
			(void)putchar('\n');
			(void)fflush(stdout);
		}
		if (c < 0) {
			if (errno != EPIPE)
				warn("netin");
			bytes = -1;
		}
		if (d < c) {
			if (d < 0)
				warn("local: %s", local);
			else
				warnx("%s: short write", local);
		}
		break;

	case TYPE_A:
		if (is_retr && restart_point) {
			int ch;
			long i, n;

			if (fseeko(fout, 0L, SEEK_SET) < 0)
				goto done;
			n = (long)restart_point;
			for (i = 0; i++ < n;) {
				if ((ch = getc(fout)) == EOF)
					goto done;
				if (ch == '\n')
					i++;
			}
			if (fseeko(fout, 0L, SEEK_CUR) < 0) {
done:
				warn("local: %s", local);
				progress = oprogress;
				preserve = opreserve;
				if (closefunc != NULL)
					(*closefunc)(fout);
				return;
			}
		}
		while ((c = DATAGETC(din)) != EOF) {
			if (c == '\n')
				bare_lfs++;
			while (c == '\r') {
				while (hash && (!progress || filesize < 0) &&
				    (bytes >= hashbytes)) {
					(void)putchar('#');
					(void)fflush(stdout);
					hashbytes += mark;
				}
				bytes++;
				if ((c = DATAGETC(din)) != '\n' || tcrflag) {
					if (ferror(fout))
						goto break2;
					(void)putc('\r', fout);
					if (c == '\0') {
						bytes++;
						goto contin2;
					}
					if (c == EOF)
						goto contin2;
				}
			}
			(void)putc(c, fout);
			bytes++;
	contin2:	;
		}
break2:
		if (bare_lfs) {
			printf(
"WARNING! %d bare linefeeds received in ASCII mode.\n", bare_lfs);
			puts("File may not have transferred correctly.");
		}
		if (hash && (!progress || filesize < 0)) {
			if (bytes < hashbytes)
				(void)putchar('#');
			(void)putchar('\n');
			(void)fflush(stdout);
		}
		if (ferror(din)) {
			if (errno != EPIPE)
				warn("netin");
			bytes = -1;
		}
		if (ferror(fout))
			warn("local: %s", local);
		break;
	}
	progressmeter(1);
	progress = oprogress;
	preserve = opreserve;
	if (closefunc != NULL)
		(*closefunc)(fout);
	(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
	(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
	if (oldintp)
		(void)signal(SIGPIPE, oldintp);
	(void)fclose(din);

#ifdef USE_SSL
	if (ssl_data_active_flag && (ssl_data_con != NULL)) {
		SSL_free(ssl_data_con);
		ssl_data_active_flag = 0;
		ssl_data_con = NULL;
	}
#endif /* USE_SSL */

	(void)getreply(0);
	if (bytes >= 0 && is_retr) {
		if (bytes > 0)
			ptransfer(0);
		if (preserve && (closefunc == fclose)) {
			mtime = remotemodtime(remote, 0);
			if (mtime != -1) {
				(void)gettimeofday(&tval[0], NULL);
				tval[1].tv_sec = mtime;
				tval[1].tv_usec = 0;
				if (utimes(local, tval) == -1) {
					printf(
				"Can't change modification time on %s to %s",
					    local, asctime(localtime(&mtime)));
				}
			}
		}
	}
	return;

abort:

/* abort using RFC959 recommended IP,SYNC sequence */

	progress = oprogress;
	preserve = opreserve;
	if (oldintp)
		(void)signal(SIGPIPE, oldintp);
	(void)signal(SIGINT, SIG_IGN);
	if (!cpend) {
		code = -1;
		(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
		(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
		return;
	}

	abort_remote(din);
	code = -1;
	if (data >= 0) {
		(void)close(data);
		data = -1;
	}
	if (closefunc != NULL && fout != NULL)
		(*closefunc)(fout);
	if (din)
		(void)fclose(din);

#ifdef USE_SSL
	if (ssl_data_active_flag && (ssl_data_con != NULL)) {
		SSL_free(ssl_data_con);
		ssl_data_active_flag = 0;
		ssl_data_con = NULL;
	}
	/* clear queued command replies if present */
	reset(0, NULL);
#endif /* USE_SSL */

	if (bytes > 0)
		ptransfer(0);
	(void)signal(SIGINT, oldintr);
#ifndef LINUX /* BSD source */
	(void)signal(SIGINFO, oldinti);
#endif /* BSD source */
}

/*
 * Need to start a listen on the data channel before we send the command,
 * otherwise the server's connect may fail.
 */
int
initconn()
{
	char *p, *a;
	socklen_t len;
	int result, tmpno = 0;
	int on = 1;
	int error, ports;
	u_int af;
	u_int hal, h[16];
	u_int pal, prt[2];
	char *pasvcmd;

#ifdef INET6
	if (myctladdr.su_family == AF_INET6
	 && (IN6_IS_ADDR_LINKLOCAL(&myctladdr.su_sin6.sin6_addr)
	  || IN6_IS_ADDR_SITELOCAL(&myctladdr.su_sin6.sin6_addr))) {
		warnx("use of scoped address can be troublesome");
	}
#endif

	if (passivemode) {
#ifdef __GNUC__			/* XXX: to shut up gcc warnings */
		(void)&pasvcmd;
#endif
		data_addr = myctladdr;
		data = socket(data_addr.su_family, SOCK_STREAM, 0);
		if (data < 0) {
			warn("socket");
			return (1);
		}
		if (dobind) {
			struct addrinfo *bindres;
			int binderr = -1;

			for (bindres = bindres0;
			     bindres != NULL;
			     bindres = bindres->ai_next)
				if (bindres->ai_family == data_addr.su_family)
					break;
			if (bindres == NULL)
				bindres = bindres0;
			binderr = bind(data, bindres->ai_addr,
				       bindres->ai_addrlen);
			if (binderr == -1)
		     {
			warn("bind");
			goto bad;
		     }
		}
		if ((options & SO_DEBUG) &&
		    setsockopt(data, SOL_SOCKET, SO_DEBUG, (char *)&on,
			       sizeof(on)) < 0)
			warn("setsockopt (ignored)");
		switch (data_addr.su_family) {
		case AF_INET:
			if (try_epsv4) {
				result = command(pasvcmd = "EPSV");
				if (code / 10 == 22 && code != 229) {
					puts("wrong server: EPSV return code must be 229");
					result = COMPLETE + 1;
				}
			} else
				result = COMPLETE + 1;
			if (result != COMPLETE) {
				try_epsv4 = 0;
				result = command(pasvcmd = "PASV");
			}
			break;
#ifdef INET6
		case AF_INET6:
			if (try_epsv6) {
				result = command(pasvcmd = "EPSV");
				if (code / 10 == 22 && code != 229) {
					puts("wrong server: EPSV return code must be 229");
					result = COMPLETE + 1;
				}
			} else
				result = COMPLETE + 1;
			if (result != COMPLETE) {
				try_epsv6 = 0;
				result = command(pasvcmd = "LPSV");
			}
			break;
#endif
		default:
			result = COMPLETE + 1;
		}
		if (result != COMPLETE) {
			puts("Passive mode refused.");
			goto bad;
		}

#define pack2(var, offset) \
	(((var[(offset) + 0] & 0xff) << 8) | ((var[(offset) + 1] & 0xff) << 0))
#define pack4(var, offset) \
    (((var[(offset) + 0] & 0xff) << 24) | ((var[(offset) + 1] & 0xff) << 16) \
     | ((var[(offset) + 2] & 0xff) << 8) | ((var[(offset) + 3] & 0xff) << 0))
		/*
		 * What we've got at this point is a string of comma
		 * separated one-byte unsigned integer values.
		 * In PASV case,
		 * The first four are the an IP address. The fifth is
		 * the MSB of the port number, the sixth is the LSB.
		 * From that we'll prepare a sockaddr_in.
		 * In other case, the format is more complicated.
		 */
		if (strcmp(pasvcmd, "PASV") == 0) {
			if (code / 10 == 22 && code != 227) {
				puts("wrong server: return code must be 227");
				error = 1;
				goto bad;
			}
			error = sscanf(pasv, "%d,%d,%d,%d,%d,%d",
			       &h[0], &h[1], &h[2], &h[3],
			       &prt[0], &prt[1]);
			if (error == 6) {
				error = 0;
				data_addr.su_sin.sin_addr.s_addr =
					htonl(pack4(h, 0));
			} else
				error = 1;
		} else if (strcmp(pasvcmd, "LPSV") == 0) {
			if (code / 10 == 22 && code != 228) {
				puts("wrong server: return code must be 228");
				error = 1;
				goto bad;
			}
			switch (data_addr.su_family) {
			case AF_INET:
				error = sscanf(pasv,
"%d,%d,%d,%d,%d,%d,%d,%d,%d",
				       &af, &hal,
				       &h[0], &h[1], &h[2], &h[3],
				       &pal, &prt[0], &prt[1]);
				if (error == 9 && af == 4 && hal == 4 && pal == 2) {
					error = 0;
					data_addr.su_sin.sin_addr.s_addr =
						htonl(pack4(h, 0));
				} else
					error = 1;
				break;
#ifdef INET6
			case AF_INET6:
				error = sscanf(pasv,
"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
					       &af, &hal,
					       &h[0], &h[1], &h[2], &h[3],
					       &h[4], &h[5], &h[6], &h[7],
					       &h[8], &h[9], &h[10], &h[11],
					       &h[12], &h[13], &h[14], &h[15],
					       &pal, &prt[0], &prt[1]);
				if (error != 21 || af != 6 || hal != 16 || pal != 2) {
					error = 1;
					break;
				}

				error = 0;
			    {
				u_int32_t *p32;
				p32 = (u_int32_t *)&data_addr.su_sin6.sin6_addr;
				p32[0] = htonl(pack4(h, 0));
				p32[1] = htonl(pack4(h, 4));
				p32[2] = htonl(pack4(h, 8));
				p32[3] = htonl(pack4(h, 12));
			    }
				break;
#endif
			default:
				error = 1;
			}
		} else if (strcmp(pasvcmd, "EPSV") == 0) {
			char delim[4];

			prt[0] = 0;
			if (code / 10 == 22 && code != 229) {
				puts("wrong server: return code must be 229");
				error = 1;
				goto bad;
			}
			error = sscanf(pasv, "%c%c%c%d%c",
				&delim[0], &delim[1], &delim[2],
				&prt[1], &delim[3]);
			if (error != 5) {
				error = 1;
				goto epsv_done;
			}
			if (delim[0] != delim[1] || delim[0] != delim[2]
			 || delim[0] != delim[3]) {
				error = 1;
				goto epsv_done;
			}

			data_addr = hisctladdr;
			/* quickhack */
			prt[0] = (prt[1] & 0xff00) >> 8;
			prt[1] &= 0xff;
			error = 0;
epsv_done:
			;
		} else
			error = 1;

		if (error) {
			puts(
"Passive mode address scan failure. Shouldn't happen!");
			goto bad;
		};

		data_addr.su_port = htons(pack2(prt, 0));
		if (connect(data, (struct sockaddr *)&data_addr,
#ifdef LINUX
			    SU_LEN(data_addr)) < 0) {
#else /* BSD source */
			    data_addr.su_len) < 0) {
#endif /* BSD source */
			warn("connect");
			goto bad;
		}
#ifdef IP_TOS
		if (data_addr.su_family == AF_INET)
	      {
		on = IPTOS_THROUGHPUT;
		if (setsockopt(data, IPPROTO_IP, IP_TOS, (char *)&on,
			       sizeof(int)) < 0)
			warn("setsockopt TOS (ignored)");
	      }
#endif
		return (0);
	}

noport:
	data_addr = myctladdr;
	if (sendport)
		data_addr.su_port = 0;	/* let system pick one */
	if (data != -1)
		(void)close(data);
	data = socket(data_addr.su_family, SOCK_STREAM, 0);
	if (data < 0) {
		warn("socket");
		if (tmpno)
			sendport = 1;
		return (1);
	}
	if (!sendport)
		if (setsockopt(data, SOL_SOCKET, SO_REUSEADDR, (char *)&on,
				sizeof(on)) < 0) {
			warn("setsockopt (reuse address)");
			goto bad;
		}
#ifdef IP_PORTRANGE
	if (data_addr.su_family == AF_INET)
      {
	
	ports = restricted_data_ports ? IP_PORTRANGE_HIGH : IP_PORTRANGE_DEFAULT;
	if (setsockopt(data, IPPROTO_IP, IP_PORTRANGE, (char *)&ports,
		       sizeof(ports)) < 0)
	    warn("setsockopt PORTRANGE (ignored)");
      }
#endif
#ifdef INET6
#ifdef IPV6_PORTRANGE
	if (data_addr.su_family == AF_INET6) {
		ports = restricted_data_ports ? IPV6_PORTRANGE_HIGH
			: IPV6_PORTRANGE_DEFAULT;
		if (setsockopt(data, IPPROTO_IPV6, IPV6_PORTRANGE,
			       (char *)&ports, sizeof(ports)) < 0)
		  warn("setsockopt PORTRANGE (ignored)");
	}
#endif
#endif

	if (bind(data, (struct sockaddr *)&data_addr,
#ifdef LINUX
		 SU_LEN(data_addr)) < 0) {
#else /* BSD source */
		 data_addr.su_len) < 0) {
#endif /* BSD source */
		warn("bind");
		goto bad;
	}
	if (options & SO_DEBUG &&
	    setsockopt(data, SOL_SOCKET, SO_DEBUG, (char *)&on,
			sizeof(on)) < 0)
		warn("setsockopt (ignored)");
	len = sizeof(data_addr);
	if (getsockname(data, (struct sockaddr *)&data_addr, &len) < 0) {
		warn("getsockname");
		goto bad;
	}
	if (listen(data, 1) < 0)
		warn("listen");
	if (sendport) {
		char hname[INET6_ADDRSTRLEN];
		int af;
		struct sockaddr_in data_addr4;
		union sockunion *daddr;

#ifdef INET6
		if (data_addr.su_family == AF_INET6 &&
		    IN6_IS_ADDR_V4MAPPED(&data_addr.su_sin6.sin6_addr)) {
			memset(&data_addr4, 0, sizeof(data_addr4));
#ifndef LINUX /* BSD source */
			data_addr4.sin_len = sizeof(struct sockaddr_in);
#endif /* BSD source */
			data_addr4.sin_family = AF_INET;
			data_addr4.sin_port = data_addr.su_port;
			memcpy((caddr_t)&data_addr4.sin_addr,
			       (caddr_t)&data_addr.su_sin6.sin6_addr.s6_addr[12],
			       sizeof(struct in_addr));
			daddr = (union sockunion *)&data_addr4;
		} else
#endif
		daddr = &data_addr;



#define	UC(b)	(((int)b)&0xff)

		switch (daddr->su_family) {
		case AF_INET:
			if (try_epsv4) {
				af = (daddr->su_family == AF_INET) ? 1 : 2;
				if (getnameinfo((struct sockaddr *)daddr,
#ifdef LINUX /* Linux port */
						SU_LEN(*daddr),
#else /* BSD source */
						daddr->su_len,
#endif /* BSD source */
						hname,
						sizeof(hname) - 1, NULL, 0,
						NI_NUMERICHOST)) {
					result = ERROR;
				} else {
					result = command("EPRT |%d|%s|%d|",
							af, hname,
							ntohs(daddr->su_port));
				}
			} else
				result = COMPLETE + 1;
			if (result != COMPLETE)
				try_epsv4 = 0;
			break;
#ifdef INET6
		case AF_INET6:
			if (try_epsv6) {
				af = (daddr->su_family == AF_INET) ? 1 : 2;
				if (daddr->su_family == AF_INET6)
					daddr->su_sin6.sin6_scope_id = 0;
				if (getnameinfo((struct sockaddr *)daddr,
#ifdef LINUX /* Linux port */
						SU_LEN(*daddr),
#else /* BSD source */
						daddr->su_len,
#endif /* BSD source */
						hname,
						sizeof(hname) - 1, NULL, 0,
						NI_NUMERICHOST)) {
					result = ERROR;
				} else {
					result = command("EPRT |%d|%s|%d|",
							af, hname,
							ntohs(daddr->su_port));
				}
			} else
				result = COMPLETE + 1;
			if (result != COMPLETE)
				try_epsv6 = 0;
			break;
#endif
		default:
			result = COMPLETE + 1;
			break;
		}
		if (result == COMPLETE)
			goto skip_port;

		p = (char *)&daddr->su_port;
		switch (daddr->su_family) {
		case AF_INET:
			a = (char *)&daddr->su_sin.sin_addr;
			result = command("PORT %d,%d,%d,%d,%d,%d",
					 UC(a[0]),UC(a[1]),UC(a[2]),UC(a[3]),
					 UC(p[0]), UC(p[1]));
			break;
#ifdef INET6
		case AF_INET6:
			a = (char *)&daddr->su_sin6.sin6_addr;
			result = command(
"LPRT %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
					 6, 16,
					 UC(a[0]),UC(a[1]),UC(a[2]),UC(a[3]),
					 UC(a[4]),UC(a[5]),UC(a[6]),UC(a[7]),
					 UC(a[8]),UC(a[9]),UC(a[10]),UC(a[11]),
					 UC(a[12]),UC(a[13]),UC(a[14]),UC(a[15]),
					 2, UC(p[0]), UC(p[1]));
			break;
#endif
		default:
			result = COMPLETE + 1; /* xxx */
		}
	skip_port:
		
		if (result == ERROR && sendport == -1) {
			sendport = 0;
			tmpno = 1;
			goto noport;
		}
		return (result != COMPLETE);
	}
	if (tmpno)
		sendport = 1;
#ifdef IP_TOS
	if (data_addr.su_family == AF_INET)
      {
	on = IPTOS_THROUGHPUT;
	if (setsockopt(data, IPPROTO_IP, IP_TOS, (char *)&on, sizeof(int)) < 0)
		warn("setsockopt TOS (ignored)");
      }
#endif
	return (0);
bad:
	(void)close(data), data = -1;
	if (tmpno)
		sendport = 1;
	return (1);
}

FILE *
dataconn(lmode)
	const char *lmode;
{
	union sockunion from;
	socklen_t fromlen;
	int s, tos;
#ifdef USE_SSL
	int ret;
	char *ssl_version;
	int ssl_bits;
	SSL_CIPHER *ssl_cipher;
	X509 *x509_ssl_data_con, *x509_ssl_con;
#endif /* USE_SSL */

#ifdef LINUX /* Linux port */
	fromlen = SU_LEN(myctladdr);
#else /* BSD source */
	fromlen = myctladdr.su_len;
#endif /* BSD source */

	if (passivemode) {
#ifdef USE_SSL
		ssl_data_active_flag = 0;
		if (ssl_active_flag && ssl_encrypt_data) {
			/* Do SSL */
			if (ssl_data_con != NULL) {
	    			SSL_free(ssl_data_con);
	    			ssl_data_con = NULL;
			}
			ssl_data_con = (SSL *)SSL_new(ssl_ctx);
			SSL_set_connect_state(ssl_data_con);

			SSL_set_fd(ssl_data_con, data);
			set_ssl_trace(ssl_data_con);

			/*
			 * This is the "magic" call that makes 
			 * this quick assuming Eric has this going
	        	 * okay! ;-)
	        	 */
			SSL_copy_session_id(ssl_data_con, ssl_con);

			/*
			 * We are doing I/O and not using select so 
	    	         * it is "safe" to read ahead.
	        	 */
			/* SSL_set_read_ahead(ssl_data_con,1);*/ /**/

			if (debug)
				warnx("===> START SSL connect on DATA");

			if ((ret=SSL_connect(ssl_data_con)) <= 0) {
				/* tell the user ... who else */
	    			ssl_log_err(bio_err,
					    "SSL_connect DATA error %d", ret);
	    			warnx("TLS/SSL DATA connection failed");

				/* abort time methinks ... */
				close(data);
				return NULL;
			} else {
				if (ssl_debug_flag) {
					ssl_version = SSL_get_cipher_version(ssl_data_con);
					ssl_cipher = SSL_get_current_cipher(ssl_data_con);
					SSL_CIPHER_get_bits(ssl_cipher, &ssl_bits);
					ssl_log_msgn(bio_err,
					    "[DATA: %s, cipher %s, %d bits]",
					    ssl_version, 
					    SSL_CIPHER_get_name(ssl_cipher),
					    ssl_bits);
				}

				/*
				 * Get server certificates of control and data
				 * connections.
				 */
				x509_ssl_con = SSL_get_peer_certificate(ssl_con);
				x509_ssl_data_con = SSL_get_peer_certificate(ssl_data_con);

				/*
				 * Check the certificates if server certificate
				 * is present for control connection.
				 */
				switch (ssl_X509_cmp(x509_ssl_con,
				    x509_ssl_data_con)) {
				case -3:
					warnx("server did not present a certificate for data connection");

					/* Drop an established TLS/SSL connection. */
					SSL_free(ssl_data_con);
					ssl_data_con = NULL;
					close(data);
					data = -1;

					return NULL;
				case 0:
				        warnx("server certificates for control and data connections are different");

					/* Drop an established TLS/SSL connection. */
					SSL_free(ssl_data_con);
					ssl_data_con = NULL;
					close(data);
					data = -1;

					return NULL;
				default:
					break;
				}

				X509_free(x509_ssl_con);
				X509_free(x509_ssl_data_con);

				ssl_data_active_flag = 1;
			}

			if (debug)
				warnx("===> DONE SSL connect on DATA %d", data);
		}
#endif /* USE_SSL */

		return (fdopen(data, lmode));
	}
	s = accept(data, (struct sockaddr *) &from, &fromlen);
	if (s < 0) {
		warn("accept");
		(void)close(data), data = -1;
		return (NULL);
	}
	(void)close(data);
	data = s;
#ifdef IP_TOS
	if (data_addr.su_family == AF_INET)
      {
	tos = IPTOS_THROUGHPUT;
	if (setsockopt(s, IPPROTO_IP, IP_TOS, (char *)&tos, sizeof(int)) < 0)
		warn("setsockopt TOS (ignored)");
      }
#endif

#ifdef USE_SSL
	ssl_data_active_flag = 0;
	if (ssl_active_flag && ssl_encrypt_data) {
		/* Do SSL */
		if (ssl_data_con != NULL) {
			SSL_free(ssl_data_con);
			ssl_data_con = NULL;
		}
		ssl_data_con = (SSL *)SSL_new(ssl_ctx);
		SSL_set_connect_state(ssl_data_con);

		SSL_set_fd(ssl_data_con, data);
		set_ssl_trace(ssl_data_con);

		/*
		 * This is the "magic" call that makes 
		 * this quick assuming Eric has this going
		 * okay! ;-)
		 */
		SSL_copy_session_id(ssl_data_con, ssl_con);

		/*
		 * We are doing I/O and not using select so 
		 * it is "safe" to read ahead.
		 */
		/* SSL_set_read_ahead(ssl_data_con,1);*/

		if (debug)
			warnx("===> START SSL connect on DATA");

		if ((ret = SSL_connect(ssl_data_con)) <= 0) {
			/* tell the user ... who else */
	    		ssl_log_err(bio_err, "SSL_connect DATA error %d", ret);
	    		warnx("TLS/SSL DATA connection failed");

			/* abort time methinks ... */
			close(data);
			return NULL;
		} else {
			if (ssl_debug_flag) {
				ssl_version = SSL_get_cipher_version(ssl_data_con);
				ssl_cipher = SSL_get_current_cipher(ssl_data_con);
				SSL_CIPHER_get_bits(ssl_cipher, &ssl_bits);
				ssl_log_msgn(bio_err,
				    "[DATA: %s, cipher %s, %d bits]",
				    ssl_version,
				    SSL_CIPHER_get_name(ssl_cipher), ssl_bits);
			}

			/*
			 * Get server certificates of control and data
			 * connections.
			 */
			x509_ssl_con = SSL_get_peer_certificate(ssl_con);
			x509_ssl_data_con = SSL_get_peer_certificate(ssl_data_con);

			/*
			 * Check the certificates if server certificate
			 * is present for control connection.
			 */
			switch (ssl_X509_cmp(x509_ssl_con, x509_ssl_data_con)) {
			case -3:
				warnx("server did not present a certificate for data connection");

				/* Drop an established TLS/SSL connection. */
				SSL_free(ssl_data_con);
				ssl_data_con = NULL;
				close(data);
				data = -1;

				return NULL;
			case 0:
				warnx("server certificates for control and data connections are different");

				/* Drop an established TLS/SSL connection. */
				SSL_free(ssl_data_con);
				ssl_data_con = NULL;
				close(data);
				data = -1;

				return NULL;
			default:
				break;
			}

			X509_free(x509_ssl_con);
			X509_free(x509_ssl_data_con);

			ssl_data_active_flag = 1;
		}

		if (debug)
			warnx("===> DONE SSL connect on DATA %d", data);
	}
#endif /* USE_SSL */

	return (fdopen(data, lmode));
}

void
psummary(notused)
	int notused;
{

	if (bytes > 0)
		ptransfer(1);
}

void
psabort(notused)
	int notused;
{

	alarmtimer(0);
	abrtflag++;
}

void
pswitch(flag)
	int flag;
{
	sig_t oldintr;
	static struct comvars {
		int connect;
		char name[MAXHOSTNAMELEN];
		char addr[NI_MAXHOST];
		union sockunion mctl;
		union sockunion hctl;
		FILE *in;
		FILE *out;
		int tpe;
		int curtpe;
		int cpnd;
		int sunqe;
		int runqe;
		int mcse;
		int ntflg;
		char nti[17];
		char nto[17];
		int mapflg;
		char mi[MAXPATHLEN];
		char mo[MAXPATHLEN];
#ifdef USE_SSL
		SSL *ssl_con;
		int ssl_active_flag;
		int ssl_encrypt_data;
		int ssl_compat_flag;
		int PBSZ_used_flag;
#endif /* USE_SSL */
	} proxstruct, tmpstruct;
	struct comvars *ip, *op;

	abrtflag = 0;
	oldintr = signal(SIGINT, psabort);
	if (flag) {
		if (proxy)
			return;
		ip = &tmpstruct;
		op = &proxstruct;
		proxy++;
	} else {
		if (!proxy)
			return;
		ip = &proxstruct;
		op = &tmpstruct;
		proxy = 0;
	}
	ip->connect = connected;
	connected = op->connect;
	if (hostname) {
		(void)strncpy(ip->name, hostname, sizeof(ip->name) - 1);
		ip->name[sizeof(ip->name) - 1] = '\0';
	} else
		ip->name[0] = '\0';
	hostname = op->name;
	if (hostaddr) {
		(void)strncpy(ip->addr, hostaddr, sizeof(ip->addr) - 1);
		ip->addr[sizeof(ip->addr) - 1] = '\0';
	} else
		ip->addr[0] = '\0';
	hostaddr = op->addr;
	ip->hctl = hisctladdr;
	hisctladdr = op->hctl;
	ip->mctl = myctladdr;
	myctladdr = op->mctl;
	ip->in = cin;
	cin = op->in;
	ip->out = cout;
	cout = op->out;
	ip->tpe = type;
	type = op->tpe;
	ip->curtpe = curtype;
	curtype = op->curtpe;
	ip->cpnd = cpend;
	cpend = op->cpnd;
	ip->sunqe = sunique;
	sunique = op->sunqe;
	ip->runqe = runique;
	runique = op->runqe;
	ip->mcse = mcase;
	mcase = op->mcse;
	ip->ntflg = ntflag;
	ntflag = op->ntflg;
	(void)strncpy(ip->nti, ntin, sizeof(ip->nti) - 1);
	(ip->nti)[sizeof(ip->nti) - 1] = '\0';
	(void)strcpy(ntin, op->nti);
	(void)strncpy(ip->nto, ntout, sizeof(ip->nto) - 1);
	(ip->nto)[sizeof(ip->nto) - 1] = '\0';
	(void)strcpy(ntout, op->nto);
	ip->mapflg = mapflag;
	mapflag = op->mapflg;
	(void)strncpy(ip->mi, mapin, sizeof(ip->mi) - 1);
	(ip->mi)[sizeof(ip->mi) - 1] = '\0';
	(void)strcpy(mapin, op->mi);
	(void)strncpy(ip->mo, mapout, sizeof(ip->mo) - 1);
	(ip->mo)[sizeof(ip->mo) - 1] = '\0';
	(void)strcpy(mapout, op->mo);
#ifdef USE_SSL
	ip->ssl_con = ssl_con;
	ssl_con = op->ssl_con;
	ip->ssl_active_flag = ssl_active_flag;
	ssl_active_flag = op->ssl_active_flag;
	ip->ssl_encrypt_data = ssl_encrypt_data;
	ssl_encrypt_data = op->ssl_encrypt_data;
	ip->ssl_compat_flag = ssl_compat_flag;
	ssl_compat_flag = op->ssl_compat_flag;
	ip->PBSZ_used_flag = PBSZ_used_flag;
	PBSZ_used_flag = op->PBSZ_used_flag;
#endif /* USE_SSL */
	(void)signal(SIGINT, oldintr);
	if (abrtflag) {
		abrtflag = 0;
		(*oldintr)(SIGINT);
	}
}

void
abortpt(notused)
	int notused;
{

	alarmtimer(0);
	putchar('\n');
	(void)fflush(stdout);
	ptabflg++;
	mflag = 0;
	abrtflag = 0;
	siglongjmp(ptabort, 1);
}

void
proxtrans(cmd, local, remote)
	const char *cmd, *local, *remote;
{
	sig_t oldintr;
	int prox_type, nfnd;
	volatile int secndflag;
	char *cmd2;
	fd_set mask;

#ifdef __GNUC__			/* XXX: to shut up gcc warnings */
	(void)&oldintr;
	(void)&cmd2;
#endif

	oldintr = NULL;
	secndflag = 0;
	if (strcmp(cmd, "RETR"))
		cmd2 = "RETR";
	else
		cmd2 = runique ? "STOU" : "STOR";
	if ((prox_type = type) == 0) {
		if (unix_server && unix_proxy)
			prox_type = TYPE_I;
		else
			prox_type = TYPE_A;
	}
	if (curtype != prox_type)
		changetype(prox_type, 1);
	if (try_epsv4 && command("EPSV") != COMPLETE)
		try_epsv4 = 0;
	if (!try_epsv4 && command("PASV") != COMPLETE) {
		puts("proxy server does not support third party transfers.");
		return;
	}
	pswitch(0);
	if (!connected) {
		puts("No primary connection.");
		pswitch(1);
		code = -1;
		return;
	}
	if (curtype != prox_type)
		changetype(prox_type, 1);
	if (command("PORT %s", pasv) != COMPLETE) {
		pswitch(1);
		return;
	}
	if (sigsetjmp(ptabort,1))
		goto abort;
	oldintr = signal(SIGINT, abortpt);
	if (command("%s %s", cmd, remote) != PRELIM) {
		(void)signal(SIGINT, oldintr);
		pswitch(1);
		return;
	}
	sleep(2);
	pswitch(1);
	secndflag++;
	if (command("%s %s", cmd2, local) != PRELIM)
		goto abort;
	ptflag++;
	(void)getreply(0);
	pswitch(0);
	(void)getreply(0);
	(void)signal(SIGINT, oldintr);
	pswitch(1);
	ptflag = 0;
	printf("local: %s remote: %s\n", local, remote);
	return;
abort:
	(void)signal(SIGINT, SIG_IGN);
	ptflag = 0;
	if (strcmp(cmd, "RETR") && !proxy)
		pswitch(1);
	else if (!strcmp(cmd, "RETR") && proxy)
		pswitch(0);
	if (!cpend && !secndflag) {  /* only here if cmd = "STOR" (proxy=1) */
		if (command("%s %s", cmd2, local) != PRELIM) {
			pswitch(0);
			if (cpend)
				abort_remote((FILE *) NULL);
		}
		pswitch(1);
		if (ptabflg)
			code = -1;
		(void)signal(SIGINT, oldintr);
		return;
	}
	if (cpend)
		abort_remote((FILE *) NULL);
	pswitch(!proxy);
	if (!cpend && !secndflag) {  /* only if cmd = "RETR" (proxy=1) */
		if (command("%s %s", cmd2, local) != PRELIM) {
			pswitch(0);
			if (cpend)
				abort_remote((FILE *) NULL);
			pswitch(1);
			if (ptabflg)
				code = -1;
			(void)signal(SIGINT, oldintr);
			return;
		}
	}
	if (cpend)
		abort_remote((FILE *) NULL);
	pswitch(!proxy);
	if (cpend) {
		FD_ZERO(&mask);
		FD_SET(fileno(cin), &mask);
		if ((nfnd = empty(&mask, 10)) <= 0) {
			if (nfnd < 0) {
				warn("abort");
			}
			if (ptabflg)
				code = -1;
			lostpeer();
		}
		(void)getreply(0);
		(void)getreply(0);
	}
	if (proxy)
		pswitch(0);
	pswitch(1);
	if (ptabflg)
		code = -1;
	(void)signal(SIGINT, oldintr);
}

void
reset(argc, argv)
	int argc;
	char *argv[];
{
	fd_set mask;
	int nfnd = 1;

	FD_ZERO(&mask);
	while (nfnd > 0) {
		FD_SET(fileno(cin), &mask);
		if ((nfnd = empty(&mask, 0)) < 0) {
			warn("reset");
			code = -1;
			lostpeer();
		}
		else if (nfnd) {
			(void)getreply(0);
		}
	}
}

char *
gunique(local)
	const char *local;
{
	static char new[MAXPATHLEN];
	char *cp = strrchr(local, '/');
	int d, count=0;
	char ext = '1';

	if (cp)
		*cp = '\0';
	d = access(cp == local ? "/" : cp ? local : ".", W_OK);
	if (cp)
		*cp = '/';
	if (d < 0) {
		warn("local: %s", local);
		return (NULL);
	}
	(void)strcpy(new, local);
	cp = new + strlen(new);
	*cp++ = '.';
	while (!d) {
		if (++count == 100) {
			puts("runique: can't find unique file name.");
			return (NULL);
		}
		*cp++ = ext;
		*cp = '\0';
		if (ext == '9')
			ext = '0';
		else
			ext++;
		if ((d = access(new, F_OK)) < 0)
			break;
		if (ext != '0')
			cp--;
		else if (*(cp - 2) == '.')
			*(cp - 1) = '1';
		else {
			*(cp - 2) = *(cp - 2) + 1;
			cp--;
		}
	}
	return (new);
}

void
abort_remote(din)
	FILE *din;
{
	char buf[BUFSIZ];
	int nfnd;
	fd_set mask;

#ifdef USE_SSL
	if (!ssl_active_flag)
#endif /*USE_SSL*/
	if (cout == NULL) {
		warnx("Lost control connection for abort.");
		if (ptabflg)
			code = -1;
		lostpeer();
		return;
	}
	/*
	 * send IAC in urgent mode instead of DM because 4.3BSD places oob mark
	 * after urgent byte rather than before as is protocol now
	 */
#ifdef USE_SSL
	if (!ssl_active_flag) {
#endif /*USE_SSL*/
	snprintf(buf, sizeof(buf), "%c%c%c", IAC, IP, IAC);
	if (send(fileno(cout), buf, 3, MSG_OOB) != 3)
		warn("abort");
	fprintf(cout, "%cABOR\r\n", DM);
	(void)fflush(cout);
#ifdef USE_SSL
	} else {
		snprintf(buf, sizeof(buf), "%c%c%c", IAC, IP, IAC);
		ssl_write(ssl_con, buf, strlen(buf));
		snprintf(buf, sizeof(buf), "%cABOR\r\n", DM);
		ssl_write(ssl_con, buf, strlen(buf));
	}
#endif /*USE_SSL*/

	FD_ZERO(&mask);

	FD_SET(fileno(cin), &mask);

	if (din) {
		FD_SET(fileno(din), &mask);
	}
	if ((nfnd = empty(&mask, 10)) <= 0) {
		if (nfnd < 0) {
			warn("abort");
		}
		if (ptabflg)
			code = -1;
#ifdef USE_SSL
		/*
		 * Check the possible exception: the abort command is received
		 * during an initialization of the TLS/SSL session on the data
		 * connection.
		 */
		if (ssl_active_flag && ssl_encrypt_data &&
		    !ssl_data_active_flag && (ssl_data_con != NULL)) {
			/* Drop the SSL connection. */
			SSL_free(ssl_data_con);
			ssl_data_active_flag = 0;
			ssl_data_con = NULL;

			/* close socket */
			if (data >= 0) {
				shutdown(data, 1 + 1);
				close(data);
				data = -1;
			}
		} else
#endif /* USE_SSL */
		lostpeer();
	}
	if (din && FD_ISSET(fileno(din), &mask)) {
		while (read(fileno(din), buf, BUFSIZ) > 0)
			/* LOOP */;
	}
	if (getreply(0) == ERROR && code == 552) {
		/* 552 needed for nic style abort */
		(void)getreply(0);
	}
	(void)getreply(0);
}

void
ai_unmapped(ai)
	struct addrinfo *ai;
{
	struct sockaddr_in6 *sin6;
	struct sockaddr_in sin;

	if (ai->ai_family != AF_INET6)
		return;
	if (ai->ai_addrlen != sizeof(struct sockaddr_in6) ||
	    sizeof(sin) > ai->ai_addrlen)
		return;
	sin6 = (struct sockaddr_in6 *)ai->ai_addr;
	if (!IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr))
		return;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
#ifndef LINUX /* BSD source */
	sin.sin_len = sizeof(struct sockaddr_in);
#endif /* BSD source */
	memcpy(&sin.sin_addr, &sin6->sin6_addr.s6_addr[12],
	    sizeof(sin.sin_addr));
	sin.sin_port = sin6->sin6_port;

	ai->ai_family = AF_INET;
#ifdef LINUX
	memcpy(ai->ai_addr, &sin, sizeof(struct sockaddr_in));
	ai->ai_addrlen = sizeof(struct sockaddr_in);
#else /* BSD source */
	memcpy(ai->ai_addr, &sin, sin.sin_len);
	ai->ai_addrlen = sin.sin_len;
#endif /* BSD source */
}
