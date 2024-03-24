/* $FreeBSD: src/usr.bin/ftp/main.c,v 1.25.2.4 2002/08/27 09:55:08 yar Exp $	*/
/*	$NetBSD: main.c,v 1.26 1997/10/14 16:31:22 christos Exp $	*/

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
static const char __COPYRIGHT[] = "@(#) Copyright (c) 1985, 1989, 1993, 1994\
\tThe Regents of the University of California.  All rights reserved.";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)main.c	8.6 (Berkeley) 10/9/94";
#else
static const char __RCSID[] = "$FreeBSD: src/usr.bin/ftp/main.c,v 1.25.2.4 2002/08/27 09:55:08 yar Exp $";
static const char __RCSID_SOURCE[] = "$NetBSD: main.c,v 1.26 1997/10/14 16:31:22 christos Exp $";
#endif
#endif /* not lint */

/*
 * FTP User Program -- Command Interface.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <histedit.h>
#include <locale.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#ifdef LINUX
#include <signal.h>
#endif /* LINUX */

#include <port_base.h>

#include "ftp_var.h"
#include "pathnames.h"

#ifdef USE_SSL /*SKYNICK XXX*/
long
bio_dump_cb(bio, cmd, argp, argi, argl, ret)
	BIO *bio;
	int cmd;
	char *argp;
	int argi;
	long argl;
	long ret;
{
	BIO *out;

/*
	out=(BIO *)BIO_get_callback_arg(bio);
*/
	out = bio_err;
	if (out == NULL) return(ret);

	if (cmd == (BIO_CB_READ | BIO_CB_RETURN)) {
/*		BIO_printf(out, "read from %08X (%d bytes => %ld (%X))\n",
			  bio, argi, ret, ret);*/
		BIO_dump(out,argp,(int)ret);
		(void)BIO_flush(out);
	} else if (cmd == (BIO_CB_WRITE | BIO_CB_RETURN)) {
/*		BIO_printf(out, "write to %08X (%d bytes => %ld (%X))\n",
			  bio, argi, ret, ret);*/
		BIO_dump(out, argp, (int)ret);
		(void)BIO_flush(out);
	}
	return((cmd & BIO_CB_RETURN) ? ret : 1);
}

int
set_ssl_trace(SSL *con) 
{
	if (con != NULL) {
		if (ssl_debug_flag) {
			BIO_set_callback(SSL_get_rbio(con), bio_dump_cb);
			BIO_set_callback_arg(SSL_get_rbio(con), (char *)bio_err);
		}
	}
	return 0;
}
#endif /* USE_SSL */

#ifdef INET6
int family = AF_UNSPEC;
#else /* !INET6 */
int family = AF_INET;
#endif /* INET6 */

int main __P((int, char **));

int
main(argc, argv)
	int argc;
	char *argv[];
{
	int ch, top, rval;
	struct passwd *pw = NULL;
	char *cp, homedir[MAXPATHLEN], *s;
	int dumbterm;
	char *src_addr = NULL;
#ifdef USE_SSL
	char *optname;
	size_t optnamelen;
#endif /* USE_SSL */

	(void) setlocale(LC_ALL, "");

	ftpport = "ftp";
	httpport = "http";
	gateport = NULL;
	cp = getenv("FTPSERVERPORT");
	if (cp != NULL)
		asprintf(&gateport, "%s", cp);
	if (!gateport)
		asprintf(&gateport, "ftpgate");
	doglob = 1;
	interactive = 1;
	autologin = 1;
	passivemode = 0;
	epsv4 = 1;
	try_epsv4 = epsv4;	/* so status w/o connection isn't bogus */
	epsv6 = 1;
	try_epsv6 = epsv6;	/* so status w/o connection isn't bogus */
	restricted_data_ports = 1;
	preserve = 1;
	verbose = 0;
	progress = 0;
	gatemode = 0;
	editing = 0;
	el = NULL;
	hist = history_init();
	mark = HASHBYTES;
	marg_sl = sl_init();
	if ((tmpdir = getenv("TMPDIR")) == NULL)
		tmpdir = _PATH_TMP;

	cp = strrchr(argv[0], '/');
	cp = (cp == NULL) ? argv[0] : cp + 1;
	if ((s = getenv("FTP_PASSIVE_MODE")) != NULL
	    && strcasecmp(s, "no") != 0)
		passivemode = 1;
	if (strcmp(cp, "pftp") == 0)
		passivemode = 1;
	else if (strcmp(cp, "gate-ftp") == 0)
		gatemode = 1;

	gateserver = getenv("FTPSERVER");
	if (gateserver == NULL || *gateserver == '\0')
		gateserver = GATE_SERVER;
	if (gatemode) {
		if (*gateserver == '\0') {
			warnx(
"Neither $FTPSERVER nor GATE_SERVER is defined; disabling gate-ftp");
			gatemode = 0;
		}
	}

	cp = getenv("TERM");
	if (cp == NULL || strcmp(cp, "dumb") == 0)
		dumbterm = 1;
	else
		dumbterm = 0;
	fromatty = isatty(fileno(stdin));
	if (fromatty) {
		verbose = 1;		/* verbose if from a tty */
		if (! dumbterm)
			editing = 1;	/* editing mode on if tty is usable */
	}
	if (isatty(fileno(stdout)) && !dumbterm)
		progress = 1;		/* progress bar on if tty is usable */

	while ((ch = getopt(argc, argv, "46adeginpP:s:tUvVz:")) != -1) {
		switch (ch) {
		case '4':
			family = AF_INET;
			break;
#ifdef INET6
		case '6':
			family = AF_INET6;
			break;
#endif
		case 'a':
			anonftp = 1;
			break;

		case 'd':
			options |= SO_DEBUG;
			debug++;
			break;

		case 'e':
			editing = 0;
			break;

		case 'g':
			doglob = 0;
			break;

		case 'i':
			interactive = 0;
			break;

		case 'n':
			autologin = 0;
			break;

		case 'p':
			passivemode = 1;
			break;

		case 'P':
			ftpport = optarg;
			break;

		case 's':
			dobind = 1;
			src_addr = optarg;
			break;

		case 't':
			trace = 1;
			break;

		case 'U':
		        restricted_data_ports = 0;
			break;

		case 'v':
			verbose = 1;
			break;

		case 'V':
			verbose = 0;
			break;

#ifdef USE_SSL
		case 'z':
			if ((strcmp(optarg, "tls") == 0) || 
			    (strcmp(optarg, "nossl") == 0)) {
				SSL_secure_flags_ON(SSL_USE_TLS);
				SSL_secure_flags_OFF(SSL_USE_COMPAT);
			}
			if ((strcmp(optarg, "ssl") == 0) || 
			    (strcmp(optarg, "notls") == 0)) {
				SSL_secure_flags_ON(SSL_USE_COMPAT);
				SSL_secure_flags_OFF(SSL_USE_TLS);
			}
			if (strcmp(optarg, "secure") == 0) {
				SSL_secure_flags_ON(SSL_ENABLED);
				SSL_secure_flags_OFF(SSL_USE_NONSECURE);
			}
			/* disable *all* ssl stuff */
			if (strcmp(optarg, "nosecure") == 0) {
				SSL_secure_flags_ON(SSL_USE_NONSECURE);
				SSL_secure_flags_OFF(SSL_ENABLED);
			}
			optname = "verify=";
			optnamelen = strlen(optname);
			if (strncmp(optarg, optname, optnamelen) == 0) {
		    		switch (atoi(optarg + optnamelen)) {
				case 0:
					ssl_verify_flag = SSL_VERIFY_NONE;
					break;
				case 1:
					ssl_verify_flag = SSL_VERIFY_PEER;
					break;
				default:
					warnx("unknown verify level ignored: %d",
					atoi(optarg + optnamelen));
				}
			}
			optname = "cert=";
			optnamelen = strlen(optname);
			if (strncmp(optarg, optname, optnamelen) == 0) {
				ssl_cert_file = optarg + optnamelen;
			}
			optname = "key=";
			optnamelen = strlen(optname);
			if (strncmp(optarg, optname, optnamelen) == 0) {
				ssl_key_file = optarg + optnamelen;
			}
			optname = "logfile=";
			optnamelen = strlen(optname);
			if (strncmp(optarg, optname, optnamelen) == 0) {
				ssl_log_file = optarg + optnamelen;
			}
			if (strcmp(optarg, "debug") == 0 ) {
				ssl_debug_flag = 1;
			}
			optname = "cipher=";
			optnamelen = strlen(optname);
			if (strncmp(optarg, optname, optnamelen) == 0) {
				ssl_cipher_list = optarg + optnamelen;
			}
			optname = "CAfile=";
			optnamelen = strlen(optname);
			if (strncmp(optarg, optname, optnamelen) == 0) {
				ssl_CA_file = optarg + optnamelen;
			}
			optname = "CApath=";
			optnamelen = strlen(optname);
			if (strncmp(optarg, optname, optnamelen) == 0) {
				ssl_CA_path = optarg + optnamelen;
			}
			optname = "CRLfile=";
			optnamelen = strlen(optname);
			if (strncmp(optarg, optname, optnamelen) == 0) {
				ssl_CRL_file = optarg + optnamelen;
			}
			optname = "CRLpath=";
			optnamelen = strlen(optname);
			if (strncmp(optarg, optname, optnamelen) == 0) {
				ssl_CRL_path = optarg + optnamelen;
			}
			if (strcmp(optarg, "noprot") == 0) {
				ssl_tryprot_flag = 0;
			}
			if (strcmp(optarg, "verbose") == 0) {
				ssl_verbose_flag = 1;
			}
			break;
#endif /* USE_SSL */

		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	cpend = 0;	/* no pending replies */
	proxy = 0;	/* proxy not active */
	crflag = 1;	/* strip c.r. on ascii gets */
	sendport = -1;	/* not using ports */

	if (dobind) {
		struct addrinfo hints;
		struct addrinfo *res;
		int error;

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = family;
		hints.ai_socktype = SOCK_STREAM;
		error = getaddrinfo(src_addr, NULL, &hints, &res);
		if (error) {
			warnx("%s: %s", src_addr, gai_strerror(error));
			if (error == EAI_SYSTEM)
				warnx("%s", strerror(errno));
			exit(1);
		}
		bindres0 = res;
	}

	/*
	 * Set up the home directory in case we're globbing.
	 */
	cp = getlogin();
	if (cp != NULL) {
		pw = getpwnam(cp);
	}
	if (pw == NULL)
		pw = getpwuid(getuid());
	if (pw != NULL) {
		home = homedir;
		(void)strcpy(home, pw->pw_dir);
	}

	setttywidth(0);
	(void)signal(SIGWINCH, setttywidth);

#ifdef USE_SSL
        if (ssl_secure_flags & SSL_ENABLED) {
		if (!do_ssleay_init(0)) {
			warnx("TLS/SSL initialization failed");
			if (ssl_debug_flag) {
				/*
				 * Print information about all recorded errors.
				 */
				ssl_log_msgn(bio_err,
					"displaying the rest of SSL errors");
				while (ERR_peek_error()) {
					ssl_log_warn_debug(bio_err,
							  "error queue");
				}
				ssl_log_msgn(bio_err,
					"error queue flushed, exit");
			}
			exit(1);
		}
	}
#endif /* USE_SSL */

#ifdef __GNUC__			/* XXX: to shut up gcc warnings */
	(void)&argc;
	(void)&argv;
#endif

	if (argc > 0) {
		if (strchr(argv[0], ':') != NULL && ! isipv6addr(argv[0])) {
			anonftp = 1;	/* Handle "automatic" transfers. */
			rval = auto_fetch(argc, argv);
			if (rval >= 0)		/* -1 == connected and cd-ed */
				exit(rval);
		} else {
			char *xargv[4], **xargp = xargv;

#ifdef __GNUC__			/* XXX: to shut up gcc warnings */
			(void)&xargp;
#endif
			if (sigsetjmp(toplevel,1))
				exit(0);
			(void)signal(SIGINT, (sig_t)intr);
			(void)signal(SIGPIPE, (sig_t)lostpeer);
			*xargp++ = __progname;
			*xargp++ = argv[0];		/* host */
			if (argc > 1)
				*xargp++ = argv[1];	/* port */
			*xargp = NULL;
			setpeer(xargp-xargv, xargv);
		}
	}
	controlediting();
	top = sigsetjmp(toplevel,1) == 0;
	if (top) {
		(void)signal(SIGINT, (sig_t)intr);
		(void)signal(SIGPIPE, (sig_t)lostpeer);
	}
	for (;;) {
		cmdscanner(top);
		top = 1;
	}
}

void
intr()
{

	alarmtimer(0);
	siglongjmp(toplevel, 1);
}

void
lostpeer()
{

	alarmtimer(0);
	if (connected) {
		if (cout != NULL) {
			(void)shutdown(fileno(cout), 1+1);
			(void)fclose(cout);
			cout = NULL;
		}
		if (data >= 0) {
			(void)shutdown(data, 1+1);
			(void)close(data);
			data = -1;
		}
		connected = 0;
	}
	pswitch(1);
	if (connected) {
		if (cout != NULL) {
			(void)shutdown(fileno(cout), 1+1);
			(void)fclose(cout);
			cout = NULL;
		}
		connected = 0;
	}
	proxflag = 0;
	pswitch(0);
}

/*
 * Generate a prompt
 */
char *
prompt()
{
	return ("ftps> ");
}

/*
 * Command parser.
 */
void
cmdscanner(top)
	int top;
{
	struct cmd *c;
	int num;
	HistEvent hev;

	if (!top 
	    && !editing
	    )
		(void)putchar('\n');
	for (;;) {
		if (!editing) {
			if (fromatty) {
				fputs(prompt(), stdout);
				(void)fflush(stdout);
			}
			if (fgets(line, sizeof(line), stdin) == NULL)
				quit(0, 0);
			num = strlen(line);
			if (num == 0)
				break;
			if (line[--num] == '\n') {
				if (num == 0)
					break;
				line[num] = '\0';
			} else if (num == sizeof(line) - 2) {
				puts("sorry, input line too long.");
				while ((num = getchar()) != '\n' && num != EOF)
					/* void */;
				break;
			} /* else it was a line without a newline */
		} else {
			const char *buf;
			cursor_pos = NULL;

			if ((buf = el_gets(el, &num)) == NULL || num == 0)
				quit(0, 0);
			if (buf[--num] == '\n') {
				if (num == 0)
					break;
			} else if (num >= sizeof(line)) {
				puts("sorry, input line too long.");
				break;
			}
			memcpy(line, buf, num);
			line[num] = '\0';
			history(hist, &hev, H_ENTER, buf);
		}

		makeargv();
		if (margc == 0)
			continue;
#if 0	/* XXX: don't want el_parse */
		/*
		 * el_parse returns -1 to signal that it's not been handled
		 * internally.
		 */
		if (el_parse(el, margc, margv) != -1)
			continue;
#endif
		c = getcmd(margv[0]);
		if (c == (struct cmd *)-1) {
			puts("?Ambiguous command.");
			continue;
		}
		if (c == 0) {
			puts("?Invalid command.");
			continue;
		}
		if (c->c_conn && !connected) {
			puts("Not connected.");
			continue;
		}
		confirmrest = 0;
		(*c->c_handler)(margc, margv);
		if (bell && c->c_bell)
			(void)putchar('\007');
		if (c->c_handler != help)
			break;
	}
	(void)signal(SIGINT, (sig_t)intr);
	(void)signal(SIGPIPE, (sig_t)lostpeer);
}

struct cmd *
getcmd(name)
	const char *name;
{
	const char *p, *q;
	struct cmd *c, *found;
	int nmatches, longest;

	if (name == NULL)
		return (0);

	longest = 0;
	nmatches = 0;
	found = 0;
	for (c = cmdtab; (p = c->c_name) != NULL; c++) {
		for (q = name; *q == *p++; q++)
			if (*q == 0)		/* exact match? */
				return (c);
		if (!*q) {			/* the name was a prefix */
			if (q - name > longest) {
				longest = q - name;
				nmatches = 1;
				found = c;
			} else if (q - name == longest)
				nmatches++;
		}
	}
	if (nmatches > 1)
		return ((struct cmd *)-1);
	return (found);
}

/*
 * Slice a string up into argc/argv.
 */

int slrflag;

void
makeargv()
{
	char *argp;

	stringbase = line;		/* scan from first of buffer */
	argbase = argbuf;		/* store from first of buffer */
	slrflag = 0;
	marg_sl->sl_cur = 0;		/* reset to start of marg_sl */
	for (margc = 0; ; margc++) {
		argp = slurpstring();
		sl_add(marg_sl, argp);
		if (argp == NULL)
			break;
	}
	if (cursor_pos == line) {
		cursor_argc = 0;
		cursor_argo = 0;
	} else if (cursor_pos != NULL) {
		cursor_argc = margc;
		cursor_argo = strlen(margv[margc-1]);
	}
}

#define INC_CHKCURSOR(x)	{ (x)++ ; \
				if (x == cursor_pos) { \
					cursor_argc = margc; \
					cursor_argo = ap-argbase; \
					cursor_pos = NULL; \
				} }
						

/*
 * Parse string into argbuf;
 * implemented with FSM to
 * handle quoting and strings
 */
char *
slurpstring()
{
	int got_one = 0;
	char *sb = stringbase;
	char *ap = argbase;
	char *tmp = argbase;		/* will return this if token found */

	if (*sb == '!' || *sb == '$') {	/* recognize ! as a token for shell */
		switch (slrflag) {	/* and $ as token for macro invoke */
			case 0:
				slrflag++;
				INC_CHKCURSOR(stringbase);
				return ((*sb == '!') ? "!" : "$");
				/* NOTREACHED */
			case 1:
				slrflag++;
				altarg = stringbase;
				break;
			default:
				break;
		}
	}

S0:
	switch (*sb) {

	case '\0':
		goto OUT;

	case ' ':
	case '\t':
		INC_CHKCURSOR(sb);
		goto S0;

	default:
		switch (slrflag) {
			case 0:
				slrflag++;
				break;
			case 1:
				slrflag++;
				altarg = sb;
				break;
			default:
				break;
		}
		goto S1;
	}

S1:
	switch (*sb) {

	case ' ':
	case '\t':
	case '\0':
		goto OUT;	/* end of token */

	case '\\':
		INC_CHKCURSOR(sb);
		goto S2;	/* slurp next character */

	case '"':
		INC_CHKCURSOR(sb);
		goto S3;	/* slurp quoted string */

	default:
		*ap = *sb;	/* add character to token */
		ap++;
		INC_CHKCURSOR(sb);
		got_one = 1;
		goto S1;
	}

S2:
	switch (*sb) {

	case '\0':
		goto OUT;

	default:
		*ap = *sb;
		ap++;
		INC_CHKCURSOR(sb);
		got_one = 1;
		goto S1;
	}

S3:
	switch (*sb) {

	case '\0':
		goto OUT;

	case '"':
		INC_CHKCURSOR(sb);
		goto S1;

	default:
		*ap = *sb;
		ap++;
		INC_CHKCURSOR(sb);
		got_one = 1;
		goto S3;
	}

OUT:
	if (got_one)
		*ap++ = '\0';
	argbase = ap;			/* update storage pointer */
	stringbase = sb;		/* update scan pointer */
	if (got_one) {
		return (tmp);
	}
	switch (slrflag) {
		case 0:
			slrflag++;
			break;
		case 1:
			slrflag++;
			altarg = NULL;
			break;
		default:
			break;
	}
	return (NULL);
}

/*
 * Help command.
 * Call each command handler with argc == 0 and argv[0] == name.
 */
void
help(argc, argv)
	int argc;
	char *argv[];
{
	struct cmd *c;

	if (argc == 1) {
		StringList *buf;

		buf = sl_init();
		printf("%sommands may be abbreviated.  Commands are:\n\n",
		    proxy ? "Proxy c" : "C");
		for (c = cmdtab; c < &cmdtab[NCMDS]; c++)
			if (c->c_name && (!proxy || c->c_proxy))
				sl_add(buf, c->c_name);
		list_vertical(buf);
		sl_free(buf, 0);
		return;
	}

#define HELPINDENT ((int) sizeof("disconnect"))

	while (--argc > 0) {
		char *arg;

		arg = *++argv;
		c = getcmd(arg);
		if (c == (struct cmd *)-1)
			printf("?Ambiguous help command %s\n", arg);
		else if (c == NULL)
			printf("?Invalid help command %s\n", arg);
		else
			printf("%-*s\t%s\n", HELPINDENT,
				c->c_name, c->c_help);
	}
}

void
usage()
{
	(void)fprintf(stderr,
	    "usage:\n"
#ifdef USE_SSL
	    "    %s [-46adeginptUvV] [-P port] [-s src_addr] [-z securemode] [host [port]]\n"
#else
	    "    %s [-46adeginptUvV] [-P port] [-s src_addr] [host [port]]\n"
#endif
	    "    %s host:path[/]\n"
	    "    %s ftp://host[:port]/path[/]\n"
	    "    %s http://host[:port]/file\n",
	    __progname, __progname, __progname, __progname);
	exit(1);
}
