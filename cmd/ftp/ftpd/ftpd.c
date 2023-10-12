/*
 * Copyright (c) 1985, 1988, 1990, 1992, 1993, 1994
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

#if 0
#ifndef lint
static char copyright[] =
"@(#) Copyright (c) 1985, 1988, 1990, 1992, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */
#endif

#ifndef lint
#if 0
static char sccsid[] = "@(#)ftpd.c	8.4 (Berkeley) 4/16/94";
#endif
#endif /* not lint */

#if 0
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/libexec/ftpd/ftpd.c,v 1.201 2005/01/10 12:19:11 yar Exp $");
#endif

/*
 * FTP server.
 */
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#define	FTP_NAMES
#include <arpa/ftp.h>
#include <arpa/inet.h>
#include <arpa/telnet.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>

#ifdef LINUX /* Linux port */
#include "bsdglob.h"
#else /* BSD source */
#include <glob.h>
#endif /* BSD source */

#include <limits.h>
#include <netdb.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#ifdef LINUX /* Linux port */
#include <crypt.h> /* for crypt() instead of unistd.h ??? */
#ifdef USE_SENDFILE
#include <sys/sendfile.h>
#endif /* USE_SENDFILE */
#endif /* Linux port */

#include <unistd.h>
#include <libutil.h>

#ifdef HAVE_OPIE
#include <opie.h>
#endif

#ifdef	LOGIN_CAP
#include <login_cap.h>
#endif

#ifdef HAVE_SHADOW_H
#include <shadow.h>
#endif /* HAVE_SHADOW_H */

#ifdef USE_PAM
#include <security/pam_appl.h>
#endif

#ifdef TCPWRAPPERS
#include <tcpd.h>
#endif /* TCPWRAPPERS */

#include "pathnames.h"
#include "extern.h"

#include <stdarg.h>

#include <port_base.h>
#include "ssl_port_ftpd.h"

#ifdef USE_SSL                         
#include "sslapp.h"

FILE	*cin, *cout;
char	ssl_file_path[MAXPATHLEN];
#endif /* USE_SSL */

#ifdef	INTERNAL_LS
#ifdef USE_SSL
static char version[] = "Version 6.00LS+TLS";
#else /* !USE_SSL */
static char version[] = "Version 6.00LS";
#endif /* USE_SSL */
#undef main
#else
#ifdef USE_SSL
static char version[] = "Version 6.00+TLS";
#else /* !USE_SSL */
static char version[] = "Version 6.00";
#endif /* USE_SSL */
#endif

extern	off_t restart_point;
extern	char cbuf[];

union sockunion ctrl_addr;
union sockunion data_source;
union sockunion data_dest;
union sockunion his_addr;
union sockunion pasv_addr;

int	daemon_mode;
int	data;
int	dataport;
int	hostinfo = 1;	/* print host-specific info in messages */
int	logged_in;
struct	passwd *pw;
char	*homedir;
int	ftpdebug;
int	timeout = 900;    /* timeout after 15 minutes of inactivity */
int	maxtimeout = 7200;/* don't allow idle time to be set beyond 2 hours */
int	logging;
int	restricted_data_ports = 1;
int	ftpdparanoid = 1; /* be extra careful about security */
int	anon_only = 0;    /* Only anonymous ftp allowed */
int	guest;
int	dochroot;
char	*chrootdir;
char	rchrootdir[MAXPATHLEN + 1];
int	dowtmp = 1;
int	stats;
int	statfd = -1;
int	type;
int	form;
int	stru;			/* avoid C keyword */
int	mode;
int	usedefault = 1;		/* for data transfers */
int	pdata = -1;		/* for passive mode */
int	readonly = 0;		/* Server is in readonly mode.	*/
int	noepsv = 0;		/* EPSV command is disabled.	*/
int	noretr = 0;		/* RETR command is disabled.	*/
int	noguestretr = 0;	/* RETR command is disabled for anon users. */
int	noguestmkd = 0;		/* MKD command is disabled for anon users. */
int	noguestmod = 1;		/* anon users may not modify existing files. */

static volatile sig_atomic_t recvurg = 0;
static volatile sig_atomic_t recvlostconn = 0;
sig_atomic_t transflag;
off_t	file_size;
off_t	byte_count;
#if !defined(CMASK) || CMASK == 0
#undef CMASK
#define CMASK 022
#endif
int	defumask = CMASK;		/* default umask value */
char	tmpline[7];
char	*hostname;
int	epsvall = 0;

#ifdef VIRTUAL_HOSTING
char	*ftpuser;

static struct ftphost {
	struct ftphost	*next;
	struct addrinfo *hostinfo;
	char		*hostname;
	char		*anonuser;
	char		*statfile;
	char		*welcome;
	char		*loginmsg;
	char		*anondir;
} *thishost, *firsthost;

#endif
char	remotehost[NI_MAXHOST];
char	*ident = NULL;

static char	ttyline[20];
char		*tty = ttyline;		/* for klogin */

#ifdef USE_PAM
static int	auth_pam(struct passwd**, const char*);
pam_handle_t	*pamh = NULL;
static void	ftpd_openlog();
#endif

#ifdef HAVE_OPIE
static struct	opie opiedata;
static char	opieprompt[OPIE_CHALLENGE_MAX+1];
static int	pwok;
#endif

#ifdef TCPWRAPPERS
static int	check_host(struct sockaddr *);
int		allow_severity = LOG_INFO;
int		deny_severity = LOG_NOTICE;
#endif /* TCPWRAPPERS */

/*
 * wu-ftpd style xferlog
 */
#define XFERLOG_ORIG	1 /* use original format */
#define XFERLOG_EXT	2 /* use extended format */
int	xferlog_stat = 0; /* xferlog format for stat file */
int	xferlog_syslog = 0; /* xferlog format for syslog */
/*
 * Options for xferlog
 */
/* Use pathnames relative to the real root dir: */
int	xferlog_wu_realroot = 0;   /* for wu-orig/wu-ext formats */
int	xferlog_anon_realroot = 0; /* for the "anon" format */

char	*pid_file = NULL;

/*
 * Override the IP address that will be advertised to IPv4 clients in response
 * to the PASV/LPSV commands.
 */
int		tun_pasvip_flag = 0;
union		sockunion tun_pasvip_addr;
static int	set_pasvip_addr();

/*
 * Define the service name
 */
#define _SERVICE_NAME "ftpd"

/*
 * Limit number of pathnames that glob can return.
 * A limit of 0 indicates the number of pathnames is unlimited.
 */
#define MAXGLOBARGS	16384

/*
 * Timeout intervals for retrying connections
 * to hosts that don't accept PORT cmds.  This
 * is a kludge, but given the problems with TCP...
 */
#define	SWAITMAX	90	/* wait at most 90 seconds */
#define	SWAITINT	5	/* interval between retries */

int	swaitmax = SWAITMAX;
int	swaitint = SWAITINT;

#ifdef SETPROCTITLE
#ifdef OLD_SETPROCTITLE
char	**Argv = NULL;		/* pointer to argument vector */
char	*LastArgv = NULL;	/* end of argv */
#endif /* OLD_SETPROCTITLE */
char	proctitle[LINE_MAX];	/* initial part of title */
#endif /* SETPROCTITLE */

#define LOGCMD(cmd, file)		logcmd((cmd), (file), NULL, -1)
#define LOGCMD2(cmd, file1, file2)	logcmd((cmd), (file1), (file2), -1)
#define LOGBYTES(cmd, file, cnt)	logcmd((cmd), (file), NULL, (cnt))

#ifdef VIRTUAL_HOSTING
static void	 inithosts(void);
static void	 selecthost(union sockunion *);
#endif
static void	 ack(char *);
static void	 sigurg(int);
static int	 myoob(void);
static int	 checkuser(char *, char *, int, char **);
static FILE	*dataconn(char *, off_t, char *);
static void	 dolog(struct sockaddr *);
static void	 end_login(void);
static FILE	*getdatasock(char *);
static int	 guniquefd(char *, char **);
static void	 lostconn(int);
static void	 sigquit(int);
static int	 receive_data(FILE *, FILE *);
static int	 send_data(FILE *, FILE *, size_t, off_t, int);
static struct passwd *
		 sgetpwnam(char *);
static char	*sgetsave(char *);
static void	 reapchild(int);
static void	 appendf(char **, char *, ...);
static void	 logcmd(char *, char *, char *, off_t);
static void	 logxfer(char *, char *, off_t, time_t, time_t, int, int);
static void	 logxfer_anon(char *, off_t, time_t, time_t);
static void	 logxfer_wuftpd(char *, char *, off_t, time_t, time_t, int, int);
static char	*doublequote(char *);
static int	*socksetup(int, char *, const char *);

int
main(int argc, char *argv[], char **envp)
{
	int addrlen, ch, on = 1, tos;
	char *cp, line[LINE_MAX];
	FILE *fd;
	char	*bindname = NULL;
	const char *bindport = "ftp";
#ifdef INET6
	int	family = AF_UNSPEC;
#else /* !INET6 */
	int	family = AF_INET;
#endif /* INET6 */
	struct sigaction sa;
	char *tun_pasvip_str = NULL;

	tzset();		/* in case no timezone database in ~ftp */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

#ifdef OLD_SETPROCTITLE
	/*
	 *  Save start and extent of argv for setproctitle.
	 */
	Argv = argv;
	while (*envp)
		envp++;
	LastArgv = envp[-1] + strlen(envp[-1]);
#endif /* OLD_SETPROCTITLE */

	/*
	 * Prevent diagnostic messages from appearing on stderr.
	 * We run as a daemon or from inetd; in both cases, there's
	 * more reason in logging to syslog.
	 */
	(void) freopen(_PATH_DEVNULL, "w", stderr);
	opterr = 0;

#ifdef USE_PAM
	ftpd_openlog();
#else /* Original code */
	/*
	 * LOG_NDELAY sets up the logging connection immediately,
	 * necessary for anonymous ftp's that chroot and can't do it later.
	 */
	openlog(_SERVICE_NAME, LOG_PID | LOG_NDELAY, LOG_FTP);
#endif /* USE_PAM */

	while ((ch = getopt(argc, argv,
	                    "46a:AdDEhlL:mMoOp:P:rRS:t:T:u:UvWX:z:")) != -1) {
		switch (ch) {
			char *optname;
			size_t optnamelen;
		case '4':
			family = (family == AF_INET6) ? AF_UNSPEC : AF_INET;
			break;

#ifdef INET6
		case '6':
			family = (family == AF_INET) ? AF_UNSPEC : AF_INET6;
			break;
#endif

		case 'a':
			optname = "bind=";
			optnamelen = strlen(optname);
			if (strncmp(optarg, optname, optnamelen) == 0) {
			    bindname = optarg + optnamelen;
			    break;
			}
			optname = "pasvip=";
			optnamelen = strlen(optname);
			if (strncmp(optarg, optname, optnamelen) == 0) {
			    tun_pasvip_str = optarg + optnamelen;
			    tun_pasvip_flag = 1;
			    break;
			}
			syslog(LOG_WARNING, "bad value for -a");
			break;

		case 'A':
			anon_only = 1;
			break;

		case 'd':
			ftpdebug++;
			break;

		case 'D':
			daemon_mode++;
			break;

		case 'E':
			noepsv = 1;
			break;

		case 'h':
			hostinfo = 0;
			break;

		case 'l':
			logging++;	/* > 1 == extra logging */
			break;

		case 'L':
			if (strcmp(optarg, "anon-abs") == 0) {
			    xferlog_anon_realroot = 1;
			    break;
			}
			if (strcmp(optarg, "wu-abs") == 0) {
			    xferlog_wu_realroot = 1;
			    break;
			}
			syslog(LOG_WARNING, "bad value for -L");
			break;

		case 'm':
			noguestmod = 0;
			break;

		case 'M':
			noguestmkd = 1;
			break;

		case 'o':
			noretr = 1;
			break;

		case 'O':
			noguestretr = 1;
			break;

		case 'p':
			pid_file = optarg;
			break;

		case 'P':
			bindport = optarg;
			break;

		case 'r':
			readonly = 1;
			break;

		case 'R':
			ftpdparanoid = 0;
			break;

		case 'S':
			if (strcmp(optarg, "anon") == 0) {
			    stats = 1;
			    break;
			}
			if (strcmp(optarg, "wu-orig") == 0) {
			    xferlog_stat = XFERLOG_ORIG;
			    break;
			}
			if (strcmp(optarg, "wu-ext") == 0) {
			    xferlog_stat = XFERLOG_EXT;
			    break;
			}
			syslog(LOG_WARNING, "bad value for -S");
			break;

		case 't':
			timeout = atoi(optarg);
			if (maxtimeout < timeout)
				maxtimeout = timeout;
			break;

		case 'T':
			maxtimeout = atoi(optarg);
			if (timeout > maxtimeout)
				timeout = maxtimeout;
			break;

		case 'u':
		    {
			long val = 0;

			val = strtol(optarg, &optarg, 8);
			if (*optarg != '\0' || val < 0)
				syslog(LOG_WARNING, "bad value for -u");
			else
				defumask = val;
			break;
		    }
		case 'U':
			restricted_data_ports = 0;
			break;

		case 'v':
			ftpdebug++;
			break;

		case 'W':
			dowtmp = 0;
			break;

		case 'X':
			if (strcmp(optarg, "wu-orig") == 0) {
			    xferlog_syslog = XFERLOG_ORIG;
			    break;
			}
			if (strcmp(optarg, "wu-ext") == 0) {
			    xferlog_syslog = XFERLOG_EXT;
			    break;
			}
			syslog(LOG_WARNING, "bad value for -X");
			break;

#ifdef USE_SSL
		case 'z':
			if ( (strcmp(optarg, "tls") == 0) ||
			     (strcmp(optarg, "nossl") == 0) ) {
			    SSL_secure_flags_ON(SSL_USE_TLS);
			    SSL_secure_flags_OFF(SSL_USE_COMPAT);
			}
			if ( (strcmp(optarg, "ssl") == 0) ||
			     (strcmp(optarg, "notls") == 0) ) {
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
			if (strcmp(optarg, "refnu") == 0) {
			    ssl_rpnu_flag = 1;
			}
			if (strcmp(optarg, "defau") == 0) {
			    ssl_dpau_flag = 1;
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
			    case 2:
				ssl_verify_flag = SSL_VERIFY_PEER |
				    SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
				break;
			    default:
				syslog(LOG_WARNING,
				    "unknown verify level ignored: %d",
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
			if (strcmp(optarg, "apbu") == 0) {
			    ssl_apbu_flag = 1;
			}
			if (strcmp(optarg, "uorc") == 0) {
			    ssl_uorc_flag = 1;
			}
			optname = "auth=";
			optnamelen = strlen(optname);
			if (strncmp(optarg, optname, optnamelen) == 0) {
		    	    switch (atoi(optarg + optnamelen)) {
			    case 0:
				x509_auth_flag = X509_AUTH_DISABLED;
				break;
			    case 1:
				x509_auth_flag = X509_AUTH_SUFFICIENT;
				break;
			    case 2:
				x509_auth_flag = X509_AUTH_REQUIRED;
				break;
			    case 3:
				x509_auth_flag = X509_AND_STANDARD_AUTH;
				break;
			    default:
				syslog(LOG_WARNING,
				    "unknown X.509 auth level ignored: %d",
				    atoi(optarg + optnamelen));
			    }
			}
			if (strcmp(optarg, "verbose") == 0) {
			    ssl_verbose_flag = 1;
			}
			break;
#endif /* USE_SSL */

		default:
			syslog(LOG_WARNING, "unknown flag -%c ignored", optopt);
			break;
		}
	}

	/* check option dependencies */
#ifdef USE_SSL
	if ((ssl_log_file == NULL) && ssl_debug_flag) {
		syslog(LOG_WARNING, "no TLS/SSL log file specified");
		exit(1);
	}
#endif /* USE_SSL */

#ifdef USE_SSL
	/*
	 * Make sure we have access to the required certificate and key files
	 * before we chroot and do the other "muck" for anon-ftp style setup...
	 */
	/*
	 * Keep the macros that are common between the client and the server
	 * happy.
	 */
	cin = stdin;
	cout = stderr;

	/* Do things the "default" way. */
	ssl_logerr_syslog = 1;
	if (ssl_cert_file == NULL) {
		snprintf(ssl_file_path, sizeof(ssl_file_path), "%s/%s",
		    X509_get_default_cert_dir(), "ftpd.pem");
		ssl_cert_file = ssl_file_path;
	}

	if (!do_ssleay_init(1)) {
		syslog(LOG_ERR, "TLS/SSL initialization failed");
		exit(1);
	}
#endif /* USE_SSL */

#ifdef VIRTUAL_HOSTING
	inithosts();
#endif

	if (daemon_mode) {
		int *ctl_sock, fd, maxfd = -1, nfds, i;
		fd_set defreadfds, readfds;
		pid_t pid;

		/* Set the IP address for passive mode. */
		if (tun_pasvip_flag) {
			if (!set_pasvip_addr(tun_pasvip_str))
				exit(1);
		}

		/*
		 * Detach from parent.
		 */
		if (daemon(1, 1) < 0) {
			syslog(LOG_ERR, "failed to become a daemon");
			exit(1);
		}
		sa.sa_handler = reapchild;
		(void)sigaction(SIGCHLD, &sa, NULL);

		/*
		 * Open a socket, bind it to the FTP port, and start
		 * listening.
		 */
		ctl_sock = socksetup(family, bindname, bindport);
		if (ctl_sock == NULL)
			exit(1);

		FD_ZERO(&defreadfds);
		for (i = 1; i <= *ctl_sock; i++) {
			FD_SET(ctl_sock[i], &defreadfds);
			if (listen(ctl_sock[i], 32) < 0) {
				syslog(LOG_ERR, "control listen: %m");
				exit(1);
			}
			if (maxfd < ctl_sock[i])
				maxfd = ctl_sock[i];
		}

		/*
		 * Atomically write process ID
		 */
		if (pid_file)
		{
			int fd;
			char buf[20];

			fd = open(pid_file, O_CREAT | O_WRONLY | O_TRUNC
				| O_NONBLOCK
#ifndef LINUX /* BSD source */
				| O_EXLOCK
#endif /* BSD source */
				, 0644);
			if (fd < 0) {
				if (errno == EAGAIN)
					syslog(LOG_ERR,
					    "%s: already locked", pid_file);
				else
					syslog(LOG_ERR, "%s: %m", pid_file);
				exit(1);
			}
			snprintf(buf, sizeof(buf),
				"%lu\n", (unsigned long) getpid());
			if (write(fd, buf, strlen(buf)) < 0) {
				syslog(LOG_ERR, "%s: write: %m", pid_file);
				exit(1);
			}
			/* Leave the pid file open and locked */
		}
		/*
		 * Loop forever accepting connection requests and forking off
		 * children to handle them.
		 */
		while (1) {
			FD_COPY(&defreadfds, &readfds);
			nfds = select(maxfd + 1, &readfds, NULL, NULL, 0);
			if (nfds <= 0) {
				if (nfds < 0 && errno != EINTR)
					syslog(LOG_WARNING, "select: %m");
				continue;
			}

			pid = -1;
                        for (i = 1; i <= *ctl_sock; i++)
				if (FD_ISSET(ctl_sock[i], &readfds)) {
					addrlen = sizeof(his_addr);
					fd = accept(ctl_sock[i],
					    (struct sockaddr *)&his_addr,
					    &addrlen);
					if (fd >= 0) {
						if ((pid = fork()) == 0) {
							/* child */
							(void) dup2(fd, 0);
							(void) dup2(fd, 1);
							close(ctl_sock[i]);
						} else
							close(fd);
					}
				}
			if (pid == 0)
				break;
		}
	} else {
		addrlen = sizeof(his_addr);
		if (getpeername(0, (struct sockaddr *)&his_addr, &addrlen) < 0) {
			syslog(LOG_ERR, "getpeername (%s): %m",argv[0]);
			exit(1);
		}
	}

	sa.sa_handler = SIG_DFL;
	(void)sigaction(SIGCHLD, &sa, NULL);

	sa.sa_handler = sigurg;
	sa.sa_flags = SA_RESTART;	/* default BSD style */
	(void)sigaction(SIGURG, &sa, NULL);
#ifdef USE_SSL /* "pseudo-OOB" with SSL */
	(void)sigaction(SIGIO, &sa, NULL);
#endif /*USE_SSL*/

	sigfillset(&sa.sa_mask);	/* block all signals in handler */
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = sigquit;
	(void)sigaction(SIGHUP, &sa, NULL);
	(void)sigaction(SIGINT, &sa, NULL);
	(void)sigaction(SIGQUIT, &sa, NULL);
	(void)sigaction(SIGTERM, &sa, NULL);

	sa.sa_handler = lostconn;
	(void)sigaction(SIGPIPE, &sa, NULL);

	addrlen = sizeof(ctrl_addr);
	if (getsockname(0, (struct sockaddr *)&ctrl_addr, &addrlen) < 0) {
		syslog(LOG_ERR, "getsockname (%s): %m",argv[0]);
		exit(1);
	}
	dataport = ntohs(ctrl_addr.su_port) - 1; /* as per RFC 959 */
#ifdef VIRTUAL_HOSTING
	/* select our identity from virtual host table */
	selecthost(&ctrl_addr);
#endif
#ifdef IP_TOS
	if (ctrl_addr.su_family == AF_INET)
      {
	tos = IPTOS_LOWDELAY;
	if (setsockopt(0, IPPROTO_IP, IP_TOS, &tos, sizeof(int)) < 0)
		syslog(LOG_WARNING, "control setsockopt (IP_TOS): %m");
      }
#endif
	/*
	 * Disable Nagle on the control channel so that we don't have to wait
	 * for peer's ACK before issuing our next reply.
	 */
	if (setsockopt(0, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) < 0)
		syslog(LOG_WARNING, "control setsockopt (TCP_NODELAY): %m");

	data_source.su_port = htons(ntohs(ctrl_addr.su_port) - 1);

	/* set this here so klogin can use it... */
	(void)snprintf(ttyline, sizeof(ttyline), "ftp%d", getpid());

	/* Try to handle urgent data inline */
#ifdef SO_OOBINLINE
	if (setsockopt(0, SOL_SOCKET, SO_OOBINLINE, &on, sizeof(on)) < 0)
		syslog(LOG_WARNING, "control setsockopt (SO_OOBINLINE): %m");
#endif

#ifdef	F_SETOWN
	if (fcntl(fileno(stdin), F_SETOWN, getpid()) == -1)
		syslog(LOG_ERR, "fcntl F_SETOWN: %m");
#endif
#ifdef	USE_SSL /* "pseudo-OOB" with SSL */
	if (fcntl(fileno(stdin), F_SETFL, O_ASYNC) == -1)
		syslog(LOG_ERR, "fcntl F_SETFL: %m");
#endif /*USE_SSL*/

	dolog((struct sockaddr *)&his_addr);
#ifdef TCPWRAPPERS
	/* If ftpd runs in the daemon mode, this code is reachable only in the
	 * child process. */
	if (daemon_mode) {
		/* Use tcp_wrappers to filter incoming requests. */
		if (!check_host((struct sockaddr *)&his_addr))
			dologout(0);
	}
#endif /* TCPWRAPPERS */

	/* Set the IP address for passive mode. */
	if (tun_pasvip_flag && !daemon_mode) {
		if (!set_pasvip_addr(tun_pasvip_str)) {
			fatalerror("Can't set an IP address for passive mode");
		}
	}

	/*
	 * Set up default state
	 */
	data = -1;
	type = TYPE_A;
	form = FORM_N;
	stru = STRU_F;
	mode = MODE_S;
	tmpline[0] = '\0';

	/* If logins are disabled, print out the message. */
	if ((fd = fopen(_PATH_NOLOGIN,"r")) != NULL) {
		while (fgets(line, sizeof(line), fd) != NULL) {
			if ((cp = strchr(line, '\n')) != NULL)
				*cp = '\0';
			lreply(530, "%s", line);
		}
		(void) FFLUSH(stdout);
		(void) fclose(fd);
		reply(530, "System not available.");
		if (logging)
			syslog(LOG_INFO, "logins are disallowed systemwide");
		dologout(0);
	}
#ifdef VIRTUAL_HOSTING
	fd = fopen(thishost->welcome, "r");
#else
	fd = fopen(_PATH_FTPWELCOME, "r");
#endif
	if (fd != NULL) {
		while (fgets(line, sizeof(line), fd) != NULL) {
			if ((cp = strchr(line, '\n')) != NULL)
				*cp = '\0';
			lreply(220, "%s", line);
		}
		(void) FFLUSH(stdout);
		(void) fclose(fd);
		/* reply(220,) must follow */
	}
#ifndef VIRTUAL_HOSTING
	if ((hostname = malloc(MAXHOSTNAMELEN)) == NULL)
		fatalerror("Ran out of memory.");
	if (gethostname(hostname, MAXHOSTNAMELEN - 1) < 0)
		hostname[0] = '\0';
	hostname[MAXHOSTNAMELEN - 1] = '\0';
#endif
	if (hostinfo)
		reply(220, "%s FTP server (%s) ready.", hostname, version);
	else
		reply(220, "FTP server ready.");
	for (;;)
		(void) yyparse();
	/* NOTREACHED */
}

static void
lostconn(int signo)
{

	recvlostconn = 1;
	dologout(1);
}

static void
sigquit(int signo)
{

	syslog(LOG_ERR, "got signal %d", signo);
	dologout(1);
}

#ifdef VIRTUAL_HOSTING
/*
 * read in virtual host tables (if they exist)
 */

static void
inithosts(void)
{
	int insert;
	size_t len;
	FILE *fp;
	char *cp, *mp, *line;
	char *hostname;
	char *vhost, *anonuser, *statfile, *welcome, *loginmsg, *anondir;
	struct ftphost *hrp, *lhrp;
	struct addrinfo hints, *res, *ai;

	/*
	 * Fill in the default host information
	 */
	if ((hostname = malloc(MAXHOSTNAMELEN)) == NULL)
		fatalerror("Ran out of memory.");
	if (gethostname(hostname, MAXHOSTNAMELEN - 1) < 0)
		hostname[0] = '\0';
	hostname[MAXHOSTNAMELEN - 1] = '\0';
	if ((hrp = malloc(sizeof(struct ftphost))) == NULL)
		fatalerror("Ran out of memory.");
	hrp->hostname = hostname;
	hrp->hostinfo = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_CANONNAME;
	hints.ai_family = AF_UNSPEC;
	if (getaddrinfo(hrp->hostname, NULL, &hints, &res) == 0)
		hrp->hostinfo = res;
	hrp->statfile = _PATH_FTPDSTATFILE;
	hrp->welcome  = _PATH_FTPWELCOME;
	hrp->loginmsg = _PATH_FTPLOGINMESG;
	hrp->anonuser = "ftp";
	hrp->anondir  = NULL;
	hrp->next = NULL;
	thishost = firsthost = lhrp = hrp;
	if ((fp = fopen(_PATH_FTPHOSTS, "r")) != NULL) {
		int gothost;

		while ((line = fgetln(fp, &len)) != NULL) {
			int	i;

			/* skip comments */
			if (line[0] == '#')
				continue;
			if (line[len - 1] == '\n') {
				line[len - 1] = '\0';
				mp = NULL;
			} else {
				if ((mp = malloc(len + 1)) == NULL)
					fatalerror("Ran out of memory.");
				memcpy(mp, line, len);
				mp[len] = '\0';
				line = mp;
			}
			cp = strtok(line, " \t");
			/* skip empty lines */
			if (cp == NULL)
				goto nextline;
			vhost = cp;

			/* set defaults */
			anonuser = "ftp";
			anondir  = NULL;
			statfile = _PATH_FTPDSTATFILE;
			welcome  = _PATH_FTPWELCOME;
			loginmsg = _PATH_FTPLOGINMESG;

			/*
			 * Preparse the line so we can use its info
			 * for all the addresses associated with
			 * the virtual host name.
			 * Field 0, the virtual host name, is special:
			 * it's already parsed off and will be strdup'ed
			 * later, after we know its canonical form.
			 */
			for (i = 1; i < 5 && (cp = strtok(NULL, " \t")); i++)
				if (*cp != '-' && (cp = strdup(cp)))
					switch (i) {
					case 1:	{ /* anon user permissions */
						char *cp_anondir;
						cp_anondir = strchr(cp, ':');
						if (cp_anondir) {
						    *cp_anondir++ = '\0';
						    anonuser = cp;
						    anondir = cp_anondir;
						} else {
						    anonuser = cp;
						}
						break;
					}
					case 2: /* statistics file */
						statfile = cp;
						break;
					case 3: /* welcome message */
						welcome  = cp;
						break;
					case 4: /* login message */
						loginmsg = cp;
						break;
					default: /* programming error */
						abort();
						/* NOTREACHED */
					}

			hints.ai_flags = 0;
			hints.ai_family = AF_UNSPEC;
			hints.ai_flags = AI_PASSIVE|AI_CANONNAME;
			if (getaddrinfo(vhost, NULL, &hints, &res) != 0)
				goto nextline;
			for (ai = res; ai != NULL && ai->ai_addr != NULL;
			     ai = ai->ai_next) {

			gothost = 0;
			for (hrp = firsthost; hrp != NULL; hrp = hrp->next) {
				struct addrinfo *hi;

				for (hi = hrp->hostinfo; hi != NULL;
				     hi = hi->ai_next)
					if (hi->ai_addrlen == ai->ai_addrlen &&
					    memcmp(hi->ai_addr,
						   ai->ai_addr,
#ifdef LINUX /* Linux port */
						   AI_ADDRLEN(ai)
#else /* BSD source */
						   ai->ai_addr->sa_len
#endif /* BSD source */
						   ) == 0) {
						gothost++;
						break;
					}
				if (gothost)
					break;
			}
			if (hrp == NULL) {
				if ((hrp = malloc(sizeof(struct ftphost))) == NULL)
					goto nextline;
				hrp->hostname = NULL;
				insert = 1;
			} else {
				if (hrp->hostinfo && hrp->hostinfo != res)
					freeaddrinfo(hrp->hostinfo);
				insert = 0; /* host already in the chain */
			}
			hrp->hostinfo = res;

			/*
			 * determine hostname to use: force canonical name then
			 * it is possible.
			 */
			if(hrp->hostinfo->ai_canonname != NULL)
				vhost = hrp->hostinfo->ai_canonname;

			if (hrp->hostname &&
			    strcmp(hrp->hostname, vhost) != 0) {
				free(hrp->hostname);
				hrp->hostname = NULL;
			}
			if (hrp->hostname == NULL &&
			    (hrp->hostname = strdup(vhost)) == NULL) {
				freeaddrinfo(hrp->hostinfo);
				hrp->hostinfo = NULL; /* mark as blank */
				goto nextline;
			}
			hrp->anonuser = anonuser;
			hrp->anondir  = anondir;
			hrp->statfile = statfile;
			hrp->welcome  = welcome;
			hrp->loginmsg = loginmsg;
			if (insert) {
				hrp->next  = NULL;
				lhrp->next = hrp;
				lhrp = hrp;
			}
		      }
nextline:
			if (mp)
				free(mp);
		}
		(void) fclose(fp);
	}
}

static void
selecthost(union sockunion *su)
{
	struct ftphost	*hrp;
	u_int16_t port;
#ifdef INET6
	struct in6_addr *mapped_in6 = NULL;
#endif
	struct addrinfo *hi;

#ifdef INET6
	/*
	 * XXX IPv4 mapped IPv6 addr consideraton,
	 * specified in rfc2373.
	 */
	if (su->su_family == AF_INET6 &&
	    IN6_IS_ADDR_V4MAPPED(&su->su_sin6.sin6_addr))
		mapped_in6 = &su->su_sin6.sin6_addr;
#endif

	hrp = thishost = firsthost;	/* default */
	port = su->su_port;
	su->su_port = 0;
	while (hrp != NULL) {
	    for (hi = hrp->hostinfo; hi != NULL; hi = hi->ai_next) {
		if (memcmp(su, hi->ai_addr,
#ifdef LINUX /* Linux port */
		    AI_ADDRLEN(hi)
#else /* BSD source */
		    hi->ai_addrlen
#endif /* BSD source */
		    ) == 0) {
			thishost = hrp;
			goto found;
		}
#ifdef INET6
		/* XXX IPv4 mapped IPv6 addr consideraton */
		if (hi->ai_addr->sa_family == AF_INET && mapped_in6 != NULL &&
		    (memcmp(&mapped_in6->s6_addr[12],
			    &((struct sockaddr_in *)hi->ai_addr)->sin_addr,
			    sizeof(struct in_addr)) == 0)) {
			thishost = hrp;
			goto found;
		}
#endif /* INET6 */
	    }
	    hrp = hrp->next;
	}
found:
	su->su_port = port;
	/* setup static variables as appropriate */
	hostname = thishost->hostname;
	ftpuser = thishost->anonuser;
}
#endif

/*
 * Helper function for sgetpwnam().
 */
static char *
sgetsave(char *s)
{
	char *new = malloc(strlen(s) + 1);

	if (new == NULL) {
		reply(421, "Ran out of memory.");
		dologout(1);
		/* NOTREACHED */
	}
	(void) strcpy(new, s);
	return (new);
}

/*
 * Save the result of a getpwnam.  Used for USER command, since
 * the data returned must not be clobbered by any other command
 * (e.g., globbing).
 * NB: The data returned by sgetpwnam() will remain valid until
 * the next call to this function.  Its difference from getpwnam()
 * is that sgetpwnam() is known to be called from ftpd code only.
 */
static struct passwd *
sgetpwnam(char *name)
{
	static struct passwd save;
	struct passwd *p;
#ifdef HAVE_SHADOW_H
	struct spwd *sp;
#endif /* HAVE_SHADOW_H */

	if ((p = getpwnam(name)) == NULL)
		return (p);
	if (save.pw_name) {
		free(save.pw_name);
		free(save.pw_passwd);
		free(save.pw_gecos);
		free(save.pw_dir);
		free(save.pw_shell);
	}
	save = *p;
	save.pw_name = sgetsave(p->pw_name);
#ifdef HAVE_SHADOW_H
	if ((sp = getspnam(p->pw_name)) != NULL) {
		save.pw_passwd = sgetsave(sp->sp_pwdp);
	} else
#endif /* HAVE_SHADOW_H */
	save.pw_passwd = sgetsave(p->pw_passwd);
	save.pw_gecos = sgetsave(p->pw_gecos);
	save.pw_dir = sgetsave(p->pw_dir);
	save.pw_shell = sgetsave(p->pw_shell);
	return (&save);
}

static int login_attempts;	/* number of failed login attempts */
static int askpasswd;		/* had user command, ask for passwd */
static char curname[MAXLOGNAME];	/* current USER name */

/*
 * USER command.
 * Sets global passwd pointer pw if named account exists and is acceptable;
 * sets askpasswd if a PASS command is expected.  If logged in previously,
 * need to reset state.  If name is "ftp" or "anonymous", the name is not in
 * _PATH_FTPUSERS, and ftp account exists, set guest and pw, then just return.
 * If account doesn't exist, ask for passwd anyway.  Otherwise, check user
 * requesting login privileges. Disallow anyone mentioned in the file
 * _PATH_FTPUSERS to allow people such as root and uucp to be avoided.
 */
void
user(char *name)
{
	if (logged_in) {
		if (guest) {
			reply(530, "Can't change user from guest login.");
			return;
		} else if (dochroot) {
			reply(530, "Can't change user from chroot user.");
			return;
		}
		end_login();
	}

	guest = 0;
#ifdef VIRTUAL_HOSTING
	pw = sgetpwnam(thishost->anonuser);
#else
	pw = sgetpwnam("ftp");
#endif
	if (strcmp(name, "ftp") == 0 || strcmp(name, "anonymous") == 0) {
		if (checkuser(_PATH_FTPUSERS, "ftp", 0, NULL) ||
		    checkuser(_PATH_FTPUSERS, "anonymous", 0, NULL))
			reply(530, "User %s access denied.", name);
#ifdef USE_SSL /* policy checking */
		else {
			/* Deny anonymous access over secure session. */
			if (ssl_active_flag && ssl_dpau_flag) {
				reply(534,
			    "User %s secure access denied for policy reasons.",
				name);
			}
#endif /* USE_SSL */
		else if (pw != NULL) {
			guest = 1;
			askpasswd = 1;
			reply(331,
			"Guest login ok, send your email address as password.");
		} else
			reply(530, "User %s unknown.", name);
#ifdef USE_SSL /* policy checking */
		}
#endif /* USE_SSL */
		if (!askpasswd && logging)
			syslog(LOG_NOTICE,
#ifdef USE_SSL
			    "%sANONYMOUS FTP LOGIN REFUSED FROM %s",
			    ssl_active_flag ? "SECURE " : "", remotehost);
#else /* !USE_SSL */
			    "ANONYMOUS FTP LOGIN REFUSED FROM %s", remotehost);
#endif /* USE_SSL */
		return;
	}
	if (anon_only != 0) {
		reply(530, "Sorry, only anonymous ftp allowed.");
		return;
	}

#ifdef USE_SSL /* policy checking */
	/* Deny non-anonymous access over non-secure session. */
	if (!ssl_active_flag && ssl_rpnu_flag) {
		reply(534,
		    "User %s non-secure access denied for policy reasons.",
		    name);
		if (logging)
			syslog(LOG_NOTICE,
			    "NON-SECURE FTP LOGIN REFUSED FROM %s, %s",
			    remotehost, name);
		return;
	}
#endif /* USE_SSL */

	pw = sgetpwnam(name);
	if (checkuser(_PATH_FTPUSERS, name, pw != NULL ? 1 : 0, NULL)) {
		reply(530, "User %s access denied.", name);
		if (logging)
			syslog(LOG_NOTICE,
			    "FTP LOGIN REFUSED FROM %s, %s (name is in %s)",
			    remotehost, name, _PATH_FTPUSERS);
		pw = NULL;
		return;
	}
	snprintf(curname, sizeof(curname), "%s", name);

#ifdef USE_SSL
	/* Do X.509 auth of TLS/SSL client, if possible. */
	if (ssl_active_flag && pw && good_ssl_user(name)) {
		x509_auth_ok = 1;
	}
	/*
	 * Fallback to the standard auth if X.509 auth isn't sufficient or
	 * is impossible.
	 */
	if (x509_auth_fallback_status()) {
#endif /* USE_SSL */
	/*
	 * Delay before reading passwd after first failed
	 * attempt to slow down passwd-guessing programs.
	 */
	if (login_attempts) {
		sigset_t oset, nset;

		sigfillset(&nset);
		sigprocmask(SIG_BLOCK, &nset, &oset);
		sleep(login_attempts);
		sigprocmask(SIG_SETMASK, &oset, NULL);
	}

#ifdef HAVE_OPIE
	pwok = 0;
#ifdef USE_PAM
	/* XXX Kluge! The conversation mechanism needs to be fixed. */
#endif
	if (opiechallenge(&opiedata, name, opieprompt) == 0) {
		pwok = (pw != NULL) &&
		       opieaccessfile(remotehost) &&
		       opiealways(pw->pw_dir);
		reply(331, "Response to %s %s for %s.",
		      opieprompt, pwok ? "requested" : "required", name);
	} else {
		pwok = 1;
		reply(331, "Password required for %s.", name);
	}
#else /* !HAVE_OPIE */
	reply(331, "Password required for %s.", name);
#endif /* HAVE_OPIE */
#ifdef USE_SSL
	}
	/*
	 * Now do the common part for both X.509-silent and standard auth.
	 */
#endif /* USE_SSL */
	askpasswd = 1;
#ifdef USE_SSL
	/* Do X.509 auth of TLS/SSL client without asking user password. */
	if (!x509_auth_fallback_status()) {
		pass("X.509"); /* send dummy password to login */
	}
#endif /* USE_SSL */
}

/*
 * Check if a user is in the file "fname",
 * return a pointer to a malloc'd string with the rest
 * of the matching line in "residue" if not NULL.
 */
static int
checkuser(char *fname, char *name, int pwset, char **residue)
{
	FILE *fd;
	int found = 0;
	size_t len;
	char *line, *mp, *p;

	if ((fd = fopen(fname, "r")) != NULL) {
		while (!found && (line = fgetln(fd, &len)) != NULL) {
			/* skip comments */
			if (line[0] == '#')
				continue;
			if (line[len - 1] == '\n') {
				line[len - 1] = '\0';
				mp = NULL;
			} else {
				if ((mp = malloc(len + 1)) == NULL)
					fatalerror("Ran out of memory.");
				memcpy(mp, line, len);
				mp[len] = '\0';
				line = mp;
			}
			/* avoid possible leading and trailing whitespace */
			p = strtok(line, " \t");
			/* skip empty lines */
			if (p == NULL)
				goto nextline;
			/*
			 * if first chr is '@', check group membership
			 */
			if (p[0] == '@') {
				int i = 0;
				struct group *grp;

				if (p[1] == '\0') /* single @ matches anyone */
					found = 1;
				else {
					if ((grp = getgrnam(p+1)) == NULL)
						goto nextline;
					/*
					 * Check user's default group
					 */
					if (pwset && grp->gr_gid == pw->pw_gid)
						found = 1;
					/*
					 * Check supplementary groups
					 */
					while (!found && grp->gr_mem[i])
						found = strcmp(name,
							grp->gr_mem[i++])
							== 0;
				}
			}
			/*
			 * Otherwise, just check for username match
			 */
			else
				found = strcmp(p, name) == 0;
			/*
			 * Save the rest of line to "residue" if matched
			 */
			if (found && residue) {
				if ((p = strtok(NULL, "")) != NULL)
					p += strspn(p, " \t");
				if (p && *p) {
				 	if ((*residue = strdup(p)) == NULL)
						fatalerror("Ran out of memory.");
				} else
					*residue = NULL;
			}
nextline:
			if (mp)
				free(mp);
		}
		(void) fclose(fd);
	}
	return (found);
}

/*
 * Terminate login as previous user, if any, resetting state;
 * used when USER command is given or login fails.
 */
static void
end_login(void)
{
#ifdef USE_PAM
	int e;
#endif

	(void) seteuid(0);
	if (logged_in && dowtmp)
		ftpd_logwtmp(ttyline, "", NULL);
	pw = NULL;
#ifdef	LOGIN_CAP
	setusercontext(NULL, getpwuid(0), 0,
		       LOGIN_SETPRIORITY|LOGIN_SETRESOURCES|LOGIN_SETUMASK
#ifdef HAVE_MAC
		       |LOGIN_SETMAC
#endif /* HAVE_MAC */
		       );
#endif /* LOGIN_CAP */
#ifdef USE_PAM
	if (pamh) {
		if ((e = pam_setcred(pamh, PAM_DELETE_CRED)) != PAM_SUCCESS)
			syslog(LOG_ERR, "pam_setcred: %s", pam_strerror(pamh, e));
		if ((e = pam_close_session(pamh,0)) != PAM_SUCCESS)
			syslog(LOG_ERR, "pam_close_session: %s", pam_strerror(pamh, e));
		if ((e = pam_end(pamh, e)) != PAM_SUCCESS)
			syslog(LOG_ERR, "pam_end: %s", pam_strerror(pamh, e));
		pamh = NULL;
		/* Reset the logging facility because it may be changed by PAM
		 * modules. */
		ftpd_openlog();
	}
#endif /* USE_PAM */
	logged_in = 0;
	guest = 0;
	dochroot = 0;
#ifdef USE_SSL
	x509_auth_ok = 0;
#endif
}

#ifdef USE_PAM

/*
 * the following code is stolen from imap-uw PAM authentication module and
 * login.c
 */
#define COPY_STRING(s) (s ? strdup(s) : NULL)

struct cred_t {
	const char *uname;		/* user name */
	const char *pass;		/* password */
};
typedef struct cred_t cred_t;

static int
auth_conv(int num_msg, const struct pam_message **msg,
	  struct pam_response **resp, void *appdata)
{
	int i;
	cred_t *cred = (cred_t *) appdata;
	struct pam_response *reply;

	reply = calloc(num_msg, sizeof *reply);
	if (reply == NULL)
		return PAM_BUF_ERR;

	for (i = 0; i < num_msg; i++) {
		switch (msg[i]->msg_style) {
		case PAM_PROMPT_ECHO_ON:	/* assume want user name */
			reply[i].resp_retcode = PAM_SUCCESS;
			reply[i].resp = COPY_STRING(cred->uname);
			/* PAM frees resp. */
			break;
		case PAM_PROMPT_ECHO_OFF:	/* assume want password */
			reply[i].resp_retcode = PAM_SUCCESS;
			reply[i].resp = COPY_STRING(cred->pass);
			/* PAM frees resp. */
			break;
		case PAM_TEXT_INFO:
		case PAM_ERROR_MSG:
			reply[i].resp_retcode = PAM_SUCCESS;
			reply[i].resp = NULL;
			break;
		default:			/* unknown message style */
			free(reply);
			return PAM_CONV_ERR;
		}
	}

	*resp = reply;
	return PAM_SUCCESS;
}

/*
 * Attempt to authenticate the user using PAM.  Returns 0 if the user is
 * authenticated, or 1 if not authenticated.  If some sort of PAM system
 * error occurs (e.g., the "/etc/pam.conf" file is missing) then this
 * function returns -1.  This can be used as an indication that we should
 * fall back to a different authentication mechanism.
 */
static int
auth_pam(struct passwd **ppw, const char *pass)
{
	char *tmpl_user;
	const void *item;
	int rval;
	int e;
	cred_t auth_cred = { (*ppw)->pw_name, pass };
	struct pam_conv conv = { &auth_conv, &auth_cred };

	e = pam_start(_SERVICE_NAME, (*ppw)->pw_name, &conv, &pamh);
	if (e != PAM_SUCCESS) {
		syslog(LOG_ERR, "pam_start: %s", pam_strerror(pamh, e));
		/* pamh is NULL, so don't call pam_end() */
		return -1;
	}

	e = pam_set_item(pamh, PAM_RHOST, remotehost);
	if (e != PAM_SUCCESS) {
		syslog(LOG_ERR, "pam_set_item(PAM_RHOST): %s",
			pam_strerror(pamh, e));
		if ((e = pam_end(pamh, e)) != PAM_SUCCESS) {
			syslog(LOG_ERR, "pam_end: %s", pam_strerror(pamh, e));
		}
		pamh = NULL;
		return -1;
	}

#ifdef USE_SSL
	/*
	 * If X.509 auth was successful and fallback to standard auth
	 * isn't required, skip all auth management modules.
	 */
	if (x509_auth_fallback_status()) {
#endif
	e = pam_authenticate(pamh, 0);
	switch (e) {
	case PAM_SUCCESS:
		/*
		 * With PAM we support the concept of a "template"
		 * user.  The user enters a login name which is
		 * authenticated by PAM, usually via a remote service
		 * such as RADIUS or TACACS+.  If authentication
		 * succeeds, a different but related "template" name
		 * is used for setting the credentials, shell, and
		 * home directory.  The name the user enters need only
		 * exist on the remote authentication server, but the
		 * template name must be present in the local password
		 * database.
		 *
		 * This is supported by two various mechanisms in the
		 * individual modules.  However, from the application's
		 * point of view, the template user is always passed
		 * back as a changed value of the PAM_USER item.
		 */
		if ((e = pam_get_item(pamh, PAM_USER, &item)) ==
		    PAM_SUCCESS) {
			tmpl_user = (char *) item;
			if (strcmp((*ppw)->pw_name, tmpl_user) != 0)
				*ppw = sgetpwnam(tmpl_user);
		} else
			syslog(LOG_ERR, "Couldn't get PAM_USER: %s",
			    pam_strerror(pamh, e));
		rval = 0;
		break;

	case PAM_AUTH_ERR:
	case PAM_USER_UNKNOWN:
	case PAM_MAXTRIES:
		rval = 1;
		break;

	default:
		syslog(LOG_ERR, "pam_authenticate: %s", pam_strerror(pamh, e));
		rval = -1;
		break;
	}
#ifdef USE_SSL
	} else {
		rval = 0;
	}
#endif

	if (rval == 0) {
		e = pam_acct_mgmt(pamh, 0);
		if (e != PAM_SUCCESS) {
			syslog(LOG_ERR, "pam_acct_mgmt: %s",
						pam_strerror(pamh, e));
			rval = 1;
		}
	}

	if (rval != 0) {
		if ((e = pam_end(pamh, e)) != PAM_SUCCESS) {
			syslog(LOG_ERR, "pam_end: %s", pam_strerror(pamh, e));
		}
		pamh = NULL;
	}
	return rval;
}

#endif /* USE_PAM */

void
pass(char *passwd)
{
	int rval;
	FILE *fd;
#ifdef	LOGIN_CAP
	login_cap_t *lc = NULL;
#endif
#ifdef USE_PAM
	int e;
#endif
	char *residue = NULL;
	char *xpasswd;
#ifdef HAVE_SHADOW_H
	struct spwd *sp;
#endif /* HAVE_SHADOW_H */


	if (logged_in || askpasswd == 0) {
		reply(503, "Login with USER first.");
		return;
	}
	askpasswd = 0;
	if (!guest) {		/* "ftp" is only account allowed no password */
#ifdef USE_SSL
		/*
		 * If X.509 auth of TLS/SSL client is required in any case,
		 * but failed, don't fallback to standard auth; the overall
		 * auth is failed.
		 */
		if (ssl_active_flag && !x509_auth_ok &&
		    ((x509_auth_flag == X509_AUTH_REQUIRED) ||
		    (x509_auth_flag == X509_AND_STANDARD_AUTH))) {
			rval = 1;
 			goto skip;
		}
#endif /* USE_SSL */
#ifdef USE_PAM
		/*
		 * PAM authentication.
		 */
		if (pw == NULL) {
			/* Try to use a "template" user account. */
			struct passwd tpw, *ppw = &tpw;

			tpw.pw_name = strdup(curname);
			if (tpw.pw_name == NULL)
				fatalerror("Ran out of memory.");

			rval = auth_pam(&ppw, passwd);

			/* If the initial value of the user name was changed
			 * by PAM, try to use the new name as the "template"
			 * user account. */
			if (rval == 0) {
				if ((ppw == NULL) ||
				    (strcmp(ppw->pw_name, curname) == 0)) {
					/* The "template" account doesn't exist
					 * or it wasn't passed back by PAM. */
					rval = 1;
				} else {
					/* Use the "template" account. */
					pw = ppw;
				}
			}
			free(tpw.pw_name);
		} else {
			/* Normal PAM auth. */
			rval = auth_pam(&pw, passwd);
		}

		/* Reset the logging facility because it may be changed by PAM
		 * modules. */
		ftpd_openlog();

		if ((pw != NULL) && logging) {
			if (strcmp(pw->pw_name, curname) != 0)
				syslog(LOG_NOTICE,
				    "PAM template user account is: %s",
				    pw->pw_name);
		}

		if (rval >= 0) {
#ifdef HAVE_OPIE
			opieunlock();
#endif /* HAVE_OPIE */
			goto skip;
		}
#endif /* USE_PAM */
		/*
		 * Traditional UNIX authentication.
		 */
		if (pw == NULL) {
			rval = 1;	/* failure below */
			goto skip;
		}
#ifdef USE_SSL /* X.509 auth support without PAM */
		if (x509_auth_fallback_status()) {
#endif /* USE_SSL */
#ifdef HAVE_OPIE
		if (opieverify(&opiedata, passwd) == 0)
			xpasswd = pw->pw_passwd;
		else if (pwok) {
#endif /* HAVE_OPIE */
		xpasswd = crypt(passwd, pw->pw_passwd);
		if (passwd[0] == '\0' && pw->pw_passwd[0] != '\0')
			xpasswd = ":";
#ifdef HAVE_OPIE
		} else {
			rval = 1;
			goto skip;
		}
#endif /* HAVE_OPIE */
		rval = strcmp(pw->pw_passwd, xpasswd);
#ifdef USE_SSL /* X.509 auth support without PAM */
		} else {
			rval = 0;
		}
#endif /* USE_SSL */
#ifdef HAVE_SHADOW_H /* SysV-style passwd/shadow implementation used in Linux */
		if ((sp = getspnam(pw->pw_name)) != NULL) {
			if (sp->sp_expire && time(NULL) >= sp->sp_expire)
				rval = 1;	/* failure */
		}
#else /* BSD-style passwd/master.passwd implementation */
		if (pw->pw_expire && time(NULL) >= pw->pw_expire)
			rval = 1;	/* failure */
#endif /* HAVE_SHADOW_H */
skip:
		/*
		 * If rval == 1, the user failed the authentication check
		 * above.  If rval == 0, either PAM or local authentication
		 * succeeded.
		 */
		if (rval) {
			reply(530, "Login incorrect.");
			if (logging) {
				syslog(LOG_NOTICE,
				    "FTP LOGIN FAILED FROM %s (attempt %d)",
				    remotehost, login_attempts + 1);
				syslog(LOG_AUTHPRIV | LOG_NOTICE,
				    "FTP LOGIN FAILED FROM %s, %s (attempt %d)",
				    remotehost, curname, login_attempts + 1);
			}
			pw = NULL;
			if (login_attempts++ >= 5) {
				syslog(LOG_NOTICE,
				    "repeated login failures from %s",
				    remotehost);
				dologout(0);
			}
			return;
		} else {
			/*
			 * Disallow anyone who does not have a standard shell
			 * as returned by getusershell(). This check is
			 * performed in the last step of the authentication to
			 * prevent the username enumeration.
			 */
			char *cp, *shell;
			if ((shell = pw->pw_shell) == NULL || *shell == 0)
				shell = _PATH_BSHELL;
			setusershell();
			while ((cp = getusershell()) != NULL)
				if (strcmp(cp, shell) == 0)
					break;
			endusershell();

			if (cp == NULL) {
				reply(530, "User %s access denied.", curname);
				if (logging)
					syslog(LOG_NOTICE,
					    "FTP LOGIN REFUSED FROM %s, %s (shell is not in /etc/shells)",
					    remotehost, curname);
				/* SKYNICK: "return" is also possible */
				dologout(0);
			}
		}
	}
	login_attempts = 0;		/* this time successful */
	if (setegid(pw->pw_gid) < 0) {
		reply(550, "Can't set gid.");
		return;
	}
	/* May be overridden by login.conf */
	(void) umask(defumask);
#ifdef	LOGIN_CAP
	if ((lc = login_getpwclass(pw)) != NULL) {
		char	remote_ip[NI_MAXHOST];

		if (getnameinfo((struct sockaddr *)&his_addr, his_addr.su_len,
			remote_ip, sizeof(remote_ip) - 1, NULL, 0,
			NI_NUMERICHOST))
				*remote_ip = 0;
		remote_ip[sizeof(remote_ip) - 1] = 0;
		if (!auth_hostok(lc, remotehost, remote_ip)) {
			syslog(LOG_INFO|LOG_AUTH,
			    "FTP LOGIN FAILED (HOST) as %s: permission denied.",
			    curname);
			reply(530, "Permission denied.");
			pw = NULL;
			return;
		}
		if (!auth_timeok(lc, time(NULL))) {
			reply(530, "Login not available right now.");
			pw = NULL;
			return;
		}
	}
	setusercontext(lc, pw, 0,
		LOGIN_SETLOGIN|LOGIN_SETGROUP|LOGIN_SETPRIORITY|
		LOGIN_SETRESOURCES|LOGIN_SETUMASK
#ifdef HAVE_MAC
		|LOGIN_SETMAC
#endif
		);
#else /* !LOGIN_CAP */
#ifndef LINUX /* BSD source */
	setlogin(pw->pw_name);
#endif /* BSD source */
	(void) initgroups(pw->pw_name, pw->pw_gid);
#endif /* LOGIN_CAP */

#ifdef USE_PAM
	if (pamh) {
		if ((e = pam_open_session(pamh, 0)) != PAM_SUCCESS) {
			syslog(LOG_ERR, "pam_open_session: %s", pam_strerror(pamh, e));
		} else if ((e = pam_setcred(pamh, PAM_ESTABLISH_CRED)) != PAM_SUCCESS) {
			syslog(LOG_ERR, "pam_setcred: %s", pam_strerror(pamh, e));
		}
		/* Reset the logging facility because it may be changed by PAM
		 * modules. */
		ftpd_openlog();
	}
#endif /* USE_PAM */

	/* open wtmp before chroot */
	if (dowtmp)
		ftpd_logwtmp(ttyline, curname,
		    (struct sockaddr *)&his_addr);
	logged_in = 1;

	if ((stats || xferlog_stat) && statfd < 0)
#ifdef VIRTUAL_HOSTING
		statfd = open(thishost->statfile, O_WRONLY|O_APPEND);
#else
		statfd = open(_PATH_FTPDSTATFILE, O_WRONLY|O_APPEND);
#endif
		if (statfd < 0) {
			stats = 0;
			xferlog_stat = 0;
		}

	dochroot =
		checkuser(_PATH_FTPCHROOT, pw->pw_name, 1, &residue)
#ifdef	LOGIN_CAP	/* Allow login.conf configuration as well */
		|| login_getcapbool(lc, "ftp-chroot", 0)
#endif
	;
	chrootdir = NULL;
	/*
	 * For a chrooted local user,
	 * a) see whether ftpchroot(5) specifies a chroot directory,
	 * b) extract the directory pathname from the line,
	 * c) expand it to the absolute pathname if necessary.
	 */
	if (dochroot && residue &&
	    (chrootdir = strtok(residue, " \t")) != NULL) {
		if (chrootdir[0] != '/')
			asprintf(&chrootdir, "%s/%s", pw->pw_dir, chrootdir);
		else
			chrootdir = strdup(chrootdir); /* make it permanent */
		if (chrootdir == NULL)
			fatalerror("Ran out of memory.");
	}
#ifdef VIRTUAL_HOSTING
	/*
	 * The anonymous ftp area of the virtual host from /etc/ftphosts, if
	 * it is specified, overrides all possible values of the chroot
	 * directory for anonymous ftp account (home directory, the directory
	 * from ftpchroot(5)).
	 * Copy thishost->anondir so it can be modified while the original
	 * value stays intact. Also expand it to the absolute pathname if
	 * necessary.
	 */
	if (guest && thishost->anondir != NULL) {
		char *ctemp;

		ctemp = NULL;
		if (chrootdir)
			free(chrootdir);

		if ((ctemp = strdup(thishost->anondir)) == NULL)
			fatalerror("Ran out of memory.");
		if (ctemp[0] != '/') {
			asprintf(&chrootdir, "%s/%s", pw->pw_dir, ctemp);
			if (chrootdir == NULL)
				fatalerror("Ran out of memory.");
		} else {
			if ((chrootdir = strdup(ctemp)) == NULL)
				fatalerror("Ran out of memory.");
		}

		if (ctemp)
			free(ctemp);
	}
#endif /* VIRTUAL_HOSTING */
	if (guest || dochroot) {
		/*
		 * If no chroot directory set yet, use the login directory.
		 * Copy it so it can be modified while pw->pw_dir stays intact.
		 */
		if (chrootdir == NULL &&
		    (chrootdir = strdup(pw->pw_dir)) == NULL)
			fatalerror("Ran out of memory.");
		/*
		 * Check for the "/chroot/./home" syntax,
		 * separate the chroot and home directory pathnames.
		 */
		if ((homedir = strstr(chrootdir, "/./")) != NULL) {
			*(homedir++) = '\0';	/* wipe '/' */
			homedir++;		/* skip '.' */
		} else {
			/*
			 * We MUST do a chdir() after the chroot. Otherwise
			 * the old current directory will be accessible as "."
			 * outside the new root!
			 */
			homedir = "/";
		}
		/* Sanity checking... */
		if (realpath(chrootdir, rchrootdir) == NULL) {
			syslog(LOG_WARNING, "realpath failed on '%s': %m", rchrootdir);
			reply(550, "Can't change root.");
			goto bad;
		}
		/*
		 * Finally, do chroot()
		 */
		if (chroot(chrootdir) < 0) {
			reply(550, "Can't change root.");
			goto bad;
		}
	} else	/* real user w/o chroot */
		homedir = pw->pw_dir;
	/*
	 * Set euid *before* doing chdir() so
	 * a) the user won't be carried to a directory that he couldn't reach
	 *    on his own due to no permission to upper path components,
	 * b) NFS mounted homedirs w/restrictive permissions will be accessible
	 *    (uid 0 has no root power over NFS if not mapped explicitly.)
	 */
	if (seteuid(pw->pw_uid) < 0) {
		reply(550, "Can't set uid.");
		goto bad;
	}
	if (chdir(homedir) < 0) {
		if (guest || dochroot) {
			reply(550, "Can't change to base directory.");
			goto bad;
		} else {
			if (chdir("/") < 0) {
				reply(550, "Root is inaccessible.");
				goto bad;
			}
			lreply(230, "No directory! Logging in with home=/.");
		}
	}

	/*
	 * Display a login message, if it exists.
	 * N.B. reply(230,) must follow the message.
	 */
#ifdef VIRTUAL_HOSTING
	fd = fopen(thishost->loginmsg, "r");
#else
	fd = fopen(_PATH_FTPLOGINMESG, "r");
#endif
	if (fd != NULL) {
		char *cp, line[LINE_MAX];

		while (fgets(line, sizeof(line), fd) != NULL) {
			if ((cp = strchr(line, '\n')) != NULL)
				*cp = '\0';
			lreply(230, "%s", line);
		}
		(void) FFLUSH(stdout);
		(void) fclose(fd);
	}
	if (guest) {
		if (ident != NULL)
			free(ident);
		ident = strdup(passwd);
		if (ident == NULL)
			fatalerror("Ran out of memory.");

		reply(230, "Guest login ok, access restrictions apply.");
#ifdef SETPROCTITLE
#ifdef VIRTUAL_HOSTING
		if (thishost != firsthost)
			snprintf(proctitle, sizeof(proctitle),
				 "%s: anonymous(%s)/%s", remotehost, hostname,
				 passwd);
		else
#endif
			snprintf(proctitle, sizeof(proctitle),
				 "%s: anonymous/%s", remotehost, passwd);
		setproctitle("%s", proctitle);
#endif /* SETPROCTITLE */
		if (logging)
			syslog(LOG_INFO, "ANONYMOUS FTP LOGIN FROM %s, %s",
			    remotehost, passwd);
	} else {
#ifndef USE_SSL /* !USE_SSL */
		if (dochroot)
			reply(230, "User %s logged in, "
				   "access restrictions apply.", curname);
		else
			reply(230, "User %s logged in.", curname);
#else /* USE_SSL */
		if (dochroot)
			reply(x509_auth_fallback_status() ? 230 : 232,
			    "User %s logged in, access restrictions apply.",
			    curname);
		else
			reply(x509_auth_fallback_status() ? 230 : 232,
			    "User %s logged in.", curname);
#endif /* !USE_SSL */

#ifdef SETPROCTITLE
		snprintf(proctitle, sizeof(proctitle),
			 "%s: user/%s", remotehost, curname);
		setproctitle("%s", proctitle);
#endif /* SETPROCTITLE */
		if (logging)
			syslog(LOG_INFO, "FTP LOGIN FROM %s as %s",
			    remotehost, curname);
	}
	if (logging && (guest || dochroot))
		syslog(LOG_INFO, "session root changed to '%s' (realpath: '%s')",
		      chrootdir, rchrootdir);
#ifdef	LOGIN_CAP
	login_close(lc);
#endif
	if (residue)
		free(residue);
	return;
bad:
	/* Forget all about it... */
#ifdef	LOGIN_CAP
	login_close(lc);
#endif
	if (residue)
		free(residue);
	end_login();
}

void
retrieve(char *cmd, char *name)
{
	FILE *fin, *dout;
	struct stat st;
	int (*closefunc)(FILE *);
	time_t time_start, time_end;
	int send_err = -1;

	/* Set the default values */
	time(&time_start);
	time_end = time_start;

	if (cmd == 0) {
		fin = fopen(name, "r"), closefunc = fclose;
		st.st_size = 0;
	} else {
		char line[BUFSIZ];

		(void) snprintf(line, sizeof(line), cmd, name), name = line;
		fin = ftpd_popen(line, "r"), closefunc = ftpd_pclose;
		st.st_size = -1;
		st.st_blksize = BUFSIZ;
	}
	if (fin == NULL) {
		if (errno != 0) {
			perror_reply(550, name);
			if (cmd == 0) {
				LOGCMD("get", name);
			}
		}
		return;
	}
	byte_count = -1;
	if (cmd == 0) {
		if (fstat(fileno(fin), &st) < 0) {
			perror_reply(550, name);
			goto done;
		}
		if (!S_ISREG(st.st_mode)) {
			/*
			 * Never sending a raw directory is a workaround
			 * for buggy clients that will attempt to RETR
			 * a directory before listing it, e.g., Mozilla.
			 * Preventing a guest from getting irregular files
			 * is a simple security measure.
			 */
			if (S_ISDIR(st.st_mode) || guest) {
				reply(550, "%s: not a plain file.", name);
				goto done;
			}
			st.st_size = -1;
			/* st.st_blksize is set for all descriptor types */
		}
	}
	if (restart_point) {
		if (type == TYPE_A) {
			off_t i, n;
			int c;

			n = restart_point;
			i = 0;
			while (i++ < n) {
				if ((c=getc(fin)) == EOF) {
					perror_reply(550, name);
					goto done;
				}
				if (c == '\n')
					i++;
			}
		} else if (lseek(fileno(fin), restart_point, L_SET) < 0) {
			perror_reply(550, name);
			goto done;
		}
	}
	dout = dataconn(name, st.st_size, "w");
	if (dout == NULL) {
		goto done;
	}
	time(&time_start);
	send_err = send_data(fin, dout, st.st_blksize, st.st_size,
		  restart_point == 0 && cmd == 0 && S_ISREG(st.st_mode));
	time(&time_end);

#ifdef USE_SSL
	if (ssl_data_active_flag && (ssl_data_con != NULL)) {
		if (SSL_shutdown(ssl_data_con) == 0) {
		    switch (SSL_get_shutdown(ssl_data_con)) {
		    case SSL_SENT_SHUTDOWN:
			SSL_get_shutdown(ssl_data_con);
			break;
		    default:
			break;
		    }
		}
		SSL_free(ssl_data_con);
		ssl_data_active_flag = 0;
		ssl_data_con = NULL;
	}
#endif /* USE_SSL */

	(void) fclose(dout);
	data = -1;
	pdata = -1;
done:
	if (cmd == 0) {
		logxfer("get", name, st.st_size, time_start, time_end, 1,
		    send_err);
	}
	(*closefunc)(fin);
}

void
store(char *name, char *mode, int unique)
{
	int fd;
	FILE *fout, *din;
	struct stat st;
	int (*closefunc)(FILE *);
	time_t time_start, time_end;
	int rec_err = -1;

	/* Set the default values */
	time(&time_start);
	time_end = time_start;

	if (*mode == 'a') {		/* APPE */
		if (unique) {
			/* Programming error */
			syslog(LOG_ERR, "Internal: unique flag to APPE");
			unique = 0;
		}
		if (guest && noguestmod) {
			reply(550, "Appending to existing file denied.");
			goto err;
		}
		restart_point = 0;	/* not affected by preceding REST */
	}
	if (unique)			/* STOU overrides REST */
		restart_point = 0;
	if (guest && noguestmod) {
		if (restart_point) {	/* guest STOR w/REST */
			reply(550, "Modifying existing file denied.");
			goto err;
		} else			/* treat guest STOR as STOU */
			unique = 1;
	}

	if (restart_point)
		mode = "r+";	/* so ASCII manual seek can work */
	if (unique) {
		if ((fd = guniquefd(name, &name)) < 0)
			goto err;
		fout = fdopen(fd, mode);
	} else
		fout = fopen(name, mode);
	closefunc = fclose;
	if (fout == NULL) {
		perror_reply(553, name);
		goto err;
	}
	byte_count = -1;
	if (restart_point) {
		if (type == TYPE_A) {
			off_t i, n;
			int c;

			n = restart_point;
			i = 0;
			while (i++ < n) {
				if ((c=getc(fout)) == EOF) {
					perror_reply(550, name);
					goto done;
				}
				if (c == '\n')
					i++;
			}
			/*
			 * We must do this seek to "current" position
			 * because we are changing from reading to
			 * writing.
			 */
			if (fseeko(fout, 0, SEEK_CUR) < 0) {
				perror_reply(550, name);
				goto done;
			}
		} else if (lseek(fileno(fout), restart_point, L_SET) < 0) {
			perror_reply(550, name);
			goto done;
		}
	}
	din = dataconn(name, -1, "r");
	if (din == NULL) {
		goto done;
	}

	time(&time_start);
	rec_err = receive_data(din, fout);
	time(&time_end);

	if (rec_err == 0) {
		if (unique)
			reply(226, "Transfer complete (unique file name:%s).",
			    name);
		else
			reply(226, "Transfer complete.");
	}

#ifdef USE_SSL
	if (ssl_data_active_flag && (ssl_data_con != NULL)) {
		if (SSL_shutdown(ssl_data_con) == 0) {
		    switch (SSL_get_shutdown(ssl_data_con)) {
		    case SSL_SENT_SHUTDOWN:
			SSL_get_shutdown(ssl_data_con);
			break;
		    default:
			break;
		    }
		}
		SSL_free(ssl_data_con);
		ssl_data_active_flag = 0;
		ssl_data_con = NULL;
	}
#endif /* USE_SSL */

	(void) fclose(din);
	data = -1;
	pdata = -1;
done:
	if (fstat(fileno(fout), &st) < 0) { /* Unexpected error... */
		char wd[MAXPATHLEN + 1];
		if (getcwd(wd, sizeof(wd) - 1) == NULL)
			snprintf(wd, sizeof(wd), "%s", strerror(errno));
		syslog(LOG_WARNING,
		    "can't fstat the received file '%s' (wd: '%s')", name, wd);
		st.st_size = 0;
	}
	logxfer(*mode == 'a' ? "append" : "put", name, st.st_size, time_start,
	    time_end, 0, rec_err);
	(*closefunc)(fout);
	return;
err:
	LOGCMD(*mode == 'a' ? "append" : "put" , name);
	return;
}

static FILE *
getdatasock(char *mode)
{
	int on = 1, s, t, tries;

	if (data >= 0)
		return (fdopen(data, mode));
#ifdef BSDORIG_BIND
	(void) seteuid(0);
#endif

	s = socket(data_dest.su_family, SOCK_STREAM, 0);
	if (s < 0)
		goto bad;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		syslog(LOG_WARNING, "data setsockopt (SO_REUSEADDR): %m");
	/* anchor socket to avoid multi-homing problems */
	data_source = ctrl_addr;
	data_source.su_port = htons(dataport);
#ifndef BSDORIG_BIND
	(void) seteuid(0);
#endif
	for (tries = 1; ; tries++) {
		/*
		 * We should loop here since it's possible that
		 * another ftpd instance has passed this point and is
		 * trying to open a data connection in active mode now.
		 * Until the other connection is opened, we'll be getting
		 * EADDRINUSE because no SOCK_STREAM sockets in the system
		 * can share both local and remote addresses, localIP:20
		 * and *:* in this case.
		 */
		if (bind(s, (struct sockaddr *)&data_source,
#ifdef LINUX /* Linux port */
		    SU_LEN(data_source)
#else /* BSD source */
		    data_source.su_len
#endif /* BSD source */
		    ) >= 0)
			break;
		if (errno != EADDRINUSE || tries > 10)
			goto bad;
		sleep(tries);
	}
	(void) seteuid(pw->pw_uid);
#ifdef IP_TOS
	if (data_source.su_family == AF_INET)
      {
	on = IPTOS_THROUGHPUT;
	if (setsockopt(s, IPPROTO_IP, IP_TOS, &on, sizeof(int)) < 0)
		syslog(LOG_WARNING, "data setsockopt (IP_TOS): %m");
      }
#endif
#ifdef TCP_NOPUSH
	/*
	 * Turn off push flag to keep sender TCP from sending short packets
	 * at the boundaries of each write().  Should probably do a SO_SNDBUF
	 * to set the send buffer size as well, but that may not be desirable
	 * in heavy-load situations.
	 */
	on = 1;
	if (setsockopt(s, IPPROTO_TCP, TCP_NOPUSH, &on, sizeof on) < 0)
		syslog(LOG_WARNING, "data setsockopt (TCP_NOPUSH): %m");
#endif
#ifdef SO_SNDBUF
	on = 65536;
	if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, &on, sizeof on) < 0)
		syslog(LOG_WARNING, "data setsockopt (SO_SNDBUF): %m");
#endif

	return (fdopen(s, mode));
bad:
	/* Return the real value of errno (close may change it) */
	t = errno;
	(void) seteuid(pw->pw_uid);
	(void) close(s);
	errno = t;
	return (NULL);
}

static FILE *
dataconn(char *name, off_t size, char *mode)
{
	char sizebuf[32];
	FILE *file;
	int retry = 0, tos, conerrno;
#ifdef USE_SSL
	char *ssl_version;
	int ssl_bits;
	SSL_CIPHER *ssl_cipher;
	X509 *x509_ssl_data_con, *x509_ssl_con;
#endif /* USE_SSL */

	file_size = size;
	byte_count = 0;
	if (size != -1)
		(void) snprintf(sizebuf, sizeof(sizebuf),
				" (%jd bytes)", (intmax_t)size);
	else
		*sizebuf = '\0';
	if (pdata >= 0) {
		union sockunion from;
		int flags;
		int s, fromlen =
#ifdef LINUX /* Linux port */
		SU_LEN(ctrl_addr);
#else /* BSD source */
		ctrl_addr.su_len;
#endif /* BSD source */
		struct timeval timeout;
		fd_set set;

		FD_ZERO(&set);
		FD_SET(pdata, &set);

		timeout.tv_usec = 0;
		timeout.tv_sec = 120;

		/*
		 * Granted a socket is in the blocking I/O mode,
		 * accept() will block after a successful select()
		 * if the selected connection dies in between.
		 * Therefore set the non-blocking I/O flag here.
		 */
		if ((flags = fcntl(pdata, F_GETFL, 0)) == -1 ||
		    fcntl(pdata, F_SETFL, flags | O_NONBLOCK) == -1)
			goto pdata_err;
		if (select(pdata+1, &set, NULL, NULL, &timeout) <= 0 ||
		    (s = accept(pdata, (struct sockaddr *) &from, &fromlen)) < 0)
			goto pdata_err;
		(void) close(pdata);
		pdata = s;
		/*
		 * Unset the inherited non-blocking I/O flag
		 * on the child socket so stdio can work on it.
		 */
		if ((flags = fcntl(pdata, F_GETFL, 0)) == -1 ||
		    fcntl(pdata, F_SETFL, flags & ~O_NONBLOCK) == -1)
			goto pdata_err;
#ifdef IP_TOS
		if (from.su_family == AF_INET)
	      {
		tos = IPTOS_THROUGHPUT;
		if (setsockopt(s, IPPROTO_IP, IP_TOS, &tos, sizeof(int)) < 0)
			syslog(LOG_WARNING, "pdata setsockopt (IP_TOS): %m");
	      }
#endif

#ifdef USE_SSL
		/*
		 * Time to negotiate SSL on the data connection...
		 * Do this via SSL_accept (as we are still the server
		 * even though things are started around the other way).
		 */
		ssl_data_active_flag = 0;
		if (ssl_active_flag && ssl_encrypt_data) {
		    /* Do SSL */

		    reply(150, "Opening %s mode SSL data connection for '%s'%s.",
			 type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);

		    if (ssl_data_con != NULL) {
			SSL_free(ssl_data_con);
			ssl_data_con = NULL;
		    }

		    ssl_data_con = (SSL *)SSL_new(ssl_ctx);
		    SSL_set_accept_state(ssl_data_con);
		    SSL_set_fd(ssl_data_con, pdata);

		    if (ssl_debug_flag) {
			ssl_log_msgn(bio_err, "Cached sessions: %d",
			    SSL_CTX_sess_number(ssl_ctx));
			ssl_log_msgn(bio_err,
			    "===>START SSL_accept on passive mode DATA connection");
		    }

		    if (SSL_accept(ssl_data_con) <= 0) {
			char errbuf[BUFSIZ];
			snprintf(errbuf, sizeof(errbuf),
				"SSL_accept DATA connection error %s.",
				ERR_reason_error_string(ERR_get_error()));
			perror_reply(425, errbuf);

			/* Drop an established connection. */
			close(pdata);
			pdata = -1;

			return NULL;
		    } else {
			if (ssl_debug_flag) {
			    ssl_version = SSL_get_cipher_version(ssl_data_con);
			    ssl_cipher = SSL_get_current_cipher(ssl_data_con);
			    SSL_CIPHER_get_bits(ssl_cipher, &ssl_bits);
			    ssl_log_msgn(bio_err,
				"[DATA conn: %s, cipher %s, %d bits]",
				ssl_version, SSL_CIPHER_get_name(ssl_cipher),
				ssl_bits);
			    ssl_log_msgn(bio_err,
				"[DATA conn: session reused: %s]",
				SSL_session_reused(ssl_data_con) ? "yes" : "no");
			}

			/* Get client certificates of control and data
			 * connections. */
			x509_ssl_con = SSL_get_peer_certificate(ssl_con);
			x509_ssl_data_con = SSL_get_peer_certificate(ssl_data_con);

			/*
			 * Check the certificates if the client certificate
			 * is presented for the control connection.
			 */
			switch (ssl_X509_cmp(x509_ssl_con, x509_ssl_data_con)) {
			    char errbuf[BUFSIZ];
			case -3:
			    snprintf(errbuf, sizeof(errbuf),
				"Client did not present a certificate for data connection.");
			    reply(425, "%s", errbuf);

			    /* Drop an established TLS/SSL connection. */
			    SSL_free(ssl_data_con);
			    ssl_data_con = NULL;
			    close(pdata);
			    pdata = -1;

			    return NULL;
			case 0:
			    snprintf(errbuf, sizeof(errbuf),
				"Client certificates for control and data connections are different.");
			    reply(425, "%s", errbuf);

			    /* Drop an established TLS/SSL connection. */
			    SSL_free(ssl_data_con);
			    ssl_data_con = NULL;
			    close(pdata);
			    pdata = -1;

			    return NULL;
			default:
			    break;
			}

			X509_free(x509_ssl_con);
			X509_free(x509_ssl_data_con);

			ssl_data_active_flag = 1;
		    }

		    if (ssl_debug_flag)
			ssl_log_msgn(bio_err,
			"===>DONE SSL_accept on passive mode DATA connection");
		} else {
		    reply(150, "Opening %s mode data connection for '%s'%s.",
			 type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
		}
#else /* !USE_SSL */
		reply(150, "Opening %s mode data connection for '%s'%s.",
		     type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
#endif /* USE_SSL */
		return (fdopen(pdata, mode));
pdata_err:
		reply(425, "Can't open data connection.");
		(void) close(pdata);
		pdata = -1;
		return (NULL);
	}
	if (data >= 0) {
		reply(125, "Using existing data connection for '%s'%s.",
		    name, sizebuf);
		usedefault = 1;
		return (fdopen(data, mode));
	}
	if (usedefault)
		data_dest = his_addr;
	usedefault = 1;
	do {
		file = getdatasock(mode);
		if (file == NULL) {
			char hostbuf[NI_MAXHOST], portbuf[NI_MAXSERV];

			if (getnameinfo((struct sockaddr *)&data_source,
#ifdef LINUX /* Linux port */
				SU_LEN(data_source),
#else /* BSD source */
				data_source.su_len,
#endif /* BSD source */
				hostbuf, sizeof(hostbuf) - 1,
				portbuf, sizeof(portbuf) - 1,
				NI_NUMERICHOST|NI_NUMERICSERV))
					*hostbuf = *portbuf = 0;
			hostbuf[sizeof(hostbuf) - 1] = 0;
			portbuf[sizeof(portbuf) - 1] = 0;
			reply(425, "Can't create data socket (%s,%s): %s.",
				hostbuf, portbuf, strerror(errno));
			return (NULL);
		}
		data = fileno(file);
		conerrno = 0;
		if (connect(data, (struct sockaddr *)&data_dest,
#ifdef LINUX /* Linux port */
		    SU_LEN(data_dest)
#else /* BSD source */
		    data_dest.su_len
#endif /* BSD source */
		    ) == 0)
			break;
		conerrno = errno;
		(void) fclose(file);
		data = -1;
		if (conerrno == EADDRINUSE) {
			sleep(swaitint);
			retry += swaitint;
		} else {
			break;
		}
	} while (retry <= swaitmax);
	if (conerrno != 0) {
		reply(425, "Can't build data connection: %s.",
			   strerror(conerrno));
		return (NULL);
	}
#ifdef USE_SSL
	/*
	 * Time to negotiate SSL on the data connection...
	 * Do this via SSL_accept (as we are still the server
	 * even though things are started around the other way).
	 */
	ssl_data_active_flag = 0;
	if (ssl_active_flag && ssl_encrypt_data) {
	    /* Do SSL */

	    reply(150, "Opening %s mode SSL data connection for '%s'%s.",
		 type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);

	    if (ssl_data_con != NULL) {
		SSL_free(ssl_data_con);
		ssl_data_con = NULL;
	    }

	    ssl_data_con = (SSL *)SSL_new(ssl_ctx);
	    SSL_set_accept_state(ssl_data_con);
	    SSL_set_fd(ssl_data_con, data);

	    if (ssl_debug_flag) {
		ssl_log_msgn(bio_err, "Cached sessions: %d",
			    SSL_CTX_sess_number(ssl_ctx));
		ssl_log_msgn(bio_err, "===>START SSL_accept on DATA connection");
	    }

	    if (SSL_accept(ssl_data_con) <= 0) {
		char errbuf[BUFSIZ];

		snprintf(errbuf, sizeof(errbuf),
			"SSL_accept DATA connection error %s.",
			ERR_reason_error_string(ERR_get_error()));
		perror_reply(425, errbuf);

		/* Drop an established connection. */
		fclose(file);
		data = -1;

		return NULL;
	    } else {
		if (ssl_debug_flag) {
		    ssl_version = SSL_get_cipher_version(ssl_data_con);
		    ssl_cipher = SSL_get_current_cipher(ssl_data_con);
		    SSL_CIPHER_get_bits(ssl_cipher, &ssl_bits);
		    ssl_log_msgn(bio_err, "[DATA conn: %s, cipher %s, %d bits]",
				ssl_version, SSL_CIPHER_get_name(ssl_cipher),
				ssl_bits);
		    ssl_log_msgn(bio_err, "[DATA conn: session reused: %s]",
				SSL_session_reused(ssl_data_con) ? "yes" : "no");
		}

		/* Get client certificates of control and data connections. */
		x509_ssl_con=SSL_get_peer_certificate(ssl_con);
		x509_ssl_data_con=SSL_get_peer_certificate(ssl_data_con);

		/*
		 * Check the certificates if the client certificate
		 * is presented for the control connection.
		 */
		switch (ssl_X509_cmp(x509_ssl_con, x509_ssl_data_con)) {
		    char errbuf[BUFSIZ];
		case -3:
		    snprintf(errbuf, sizeof(errbuf),
			"Client did not present a certificate for data connection.");
		    reply(425, "%s", errbuf);

		    /* Drop an established TLS/SSL connection. */
		    SSL_free(ssl_data_con);
		    ssl_data_con = NULL;
    		    fclose(file);
		    data = -1;

		    return NULL;
		case  0:
		    snprintf(errbuf, sizeof(errbuf),
			"Client certificates for control and data connections are different.");
		    reply(425, "%s", errbuf);

		    /* Drop an established TLS/SSL connection. */
		    SSL_free(ssl_data_con);
		    ssl_data_con = NULL;
		    fclose(file);
		    data = -1;

		    return NULL;
		default:
		    break;
		}

		X509_free(x509_ssl_con);
		X509_free(x509_ssl_data_con);

		ssl_data_active_flag = 1;
	    }

	    if (ssl_debug_flag) 
		ssl_log_msgn(bio_err, "===>DONE SSL_accept on DATA connection");

	} else {
	    reply(150, "Opening %s mode data connection for '%s'%s.",
		 type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
	}
#else /* !USE_SSL */
	reply(150, "Opening %s mode data connection for '%s'%s.",
	     type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
#endif /* USE_SSL */
	return (file);
}

/*
 * Tranfer the contents of "instr" to "outstr" peer using the appropriate
 * encapsulation of the data subject to Mode, Structure, and Type.
 *
 * NB: Form isn't handled.
 */
static int
send_data(FILE *instr, FILE *outstr, size_t blksize, off_t filesize, int isreg)
{
	int c, cp, filefd, netfd;
	char *buf, *bp;
	off_t cnt = 0, len;

	transflag++;
	switch (type) {

	case TYPE_A:
		cp = '\0';
		while ((c = getc(instr)) != EOF) {
			if (recvurg)
				if (myoob())
					goto got_oob;
			byte_count++;
 			if (c == '\n' && cp != '\r') {
				if (ferror(outstr))
					goto data_err;
				(void) DATAPUTC('\r', outstr);
			}
			(void) DATAPUTC(c, outstr);
 			cp = c;
		}
		if (recvurg)
			if (myoob())
				goto got_oob;
		DATAFLUSH(outstr);
		transflag = 0;
		if (ferror(instr))
			goto file_err;
		if (ferror(outstr))
			goto data_err;
		reply(226, "Transfer complete.");
		return (0);

	case TYPE_I:
	case TYPE_L:
		/*
		 * isreg is only set if we are not doing restart and we
		 * are sending a regular file
		 */
		netfd = fileno(outstr);
		filefd = fileno(instr);

		if (isreg) {
			int trans_complete = 1;	/* "0" if we hit the EOF
						 * prematurely */
			off_t offset;
#ifdef USE_SENDFILE
			int err;
#endif /* USE_SENDFILE */

			cnt = offset = 0;
#ifdef USE_SSL
			if (ssl_data_active_flag) {
			    if (filesize < (off_t)16 * 1024 * 1024) {
				buf = mmap(0, filesize, PROT_READ, MAP_SHARED,
				    filefd, (off_t)0);
				if (buf == MAP_FAILED) {
					syslog(LOG_WARNING, "mmap(%lu): %m",
					    (unsigned long)filesize);
					goto oldway;
				}

				bp = buf;
				len = filesize;

				do {
				    cnt = ssl_write(ssl_data_con, bp, len);
				    len -= cnt;
				    bp += cnt;
				    if (cnt > 0) byte_count += cnt;
				    if (recvurg)
					    if (myoob()) {
						munmap(buf, (size_t)filesize);
						goto got_oob;
					    }
				} while(cnt > 0 && len > 0);

				munmap(buf, (size_t)filesize);
				if (cnt < 0)
					goto data_err;
			    } else
				goto oldway;
			} else
#endif /* USE_SSL */
#ifdef USE_SENDFILE
			while (filesize > 0) {
#ifdef LINUX /* Linux port */
				err = sendfile(filefd, netfd, &offset, filesize);
				if (err >= 0)
					cnt = err;
				else
					cnt = 0;
#else /* BSD source */
				err = sendfile(filefd, netfd, offset, 0,
					       NULL, &cnt, 0);
#endif /* BSD source */
				/*
				 * Calculate byte_count before OOB processing.
				 * It can be used in myoob() later.
				 */
				byte_count += cnt;
				if (recvurg)
					if (myoob())
						goto got_oob;
				offset += cnt;
				filesize -= cnt;

				if (err == -1) {
#ifndef LINUX /* BSD source */
					if (errno == EAGAIN || errno == EINTR)
						continue;
#endif /* BSD source */
					if (cnt == 0 && offset == 0)
						goto oldway;

					goto data_err;
				}

				/*
				 * We hit the EOF prematurely.
				 * Perhaps the file was externally truncated.
				 */
				if (cnt == 0) {
					trans_complete = 0;
					break;
				}
			}
#else /* !USE_SENDFILE */
			if (filesize < (off_t)16 * 1024 * 1024) {
				buf = mmap(0, filesize, PROT_READ, MAP_SHARED,
				    filefd, (off_t)0);
				if (buf == MAP_FAILED) {
					syslog(LOG_WARNING, "mmap(%lu): %m",
					    (unsigned long)filesize);
					goto oldway;
				}
				bp = buf;
				len = filesize;

				do {
					cnt = write(netfd, bp, len);
					len -= cnt;
					bp += cnt;
					if (cnt > 0) byte_count += cnt;
					if (recvurg)
						if (myoob()) {
						    munmap(buf, (size_t)filesize);
						    goto got_oob;
						}
				} while(cnt > 0 && len > 0);

				munmap(buf, (size_t)filesize);
				if (cnt < 0)
					goto data_err;
			} else
				goto oldway;
#endif /* USE_SENDFILE */
			transflag = 0;
			reply(226, "%s", trans_complete ?
			    "Transfer complete." :
			    "Transfer finished due to premature end of file.");
			return (0);
		}

oldway:
		if ((buf = malloc(blksize)) == NULL) {
			transflag = 0;
			reply(451, "Ran out of memory.");
			return (-1);
		}

#ifdef USE_SSL
		if (ssl_data_active_flag) {
			while ((len = read(filefd, buf, blksize)) > 0 &&
			    (cnt = ssl_write(ssl_data_con, buf, len)) == len) {
				byte_count += cnt;
				if (recvurg)
					if (myoob()) {
						free(buf);
						goto got_oob;
					}
			}
		} else
#endif /* USE_SSL */
		while ((len = read(filefd, buf, blksize)) > 0) {
			bp = buf;
			do {
				cnt = write(netfd, bp, len);
				len -= cnt;
				bp += cnt;
				if (cnt > 0) byte_count += cnt;
				if (recvurg)
					if (myoob()) {
						free(buf);
						goto got_oob;
					}
			} while (cnt > 0 && len > 0);
		}
		transflag = 0;
		free(buf);
		if (len < 0)
		    goto file_err;
		if (cnt < 0)
		    goto data_err;
		reply(226, "Transfer complete.");
		return (0);
	default:
		transflag = 0;
		reply(550, "Unimplemented TYPE %d in send_data.", type);
		return (-1);
	}

data_err:
	transflag = 0;
	perror_reply(426, "Data connection");
	return (-1);

file_err:
	transflag = 0;
	perror_reply(551, "Error on input file");
	return (-1);

got_oob:
	recvurg = 0;
	transflag = 0;
	DATAFLUSH(outstr);
	return (-1);
}

/*
 * Transfer data from peer to "outstr" using the appropriate encapulation of
 * the data subject to Mode, Structure, and Type.
 *
 * N.B.: Form isn't handled.
 */
static int
receive_data(FILE *instr, FILE *outstr)
{
	int c;
	int cnt, bare_lfs;
	char buf[BUFSIZ];

	transflag++;
	bare_lfs = 0;

	switch (type) {

	case TYPE_I:
	case TYPE_L:
#ifdef USE_SSL
		if (ssl_data_active_flag) {
			while ((cnt = ssl_read(ssl_data_con, buf, sizeof buf)) > 0) {
		        	if (write(fileno(outstr), buf, cnt) != cnt)
			        	goto file_err;
				byte_count += cnt;
			}
		} else
#endif /* !USE_SSL */
		while ((cnt = read(fileno(instr), buf, sizeof(buf))) > 0) {
			if (recvurg)
				if (myoob())
					goto got_oob;
			if (write(fileno(outstr), buf, cnt) != cnt)
				goto file_err;
			byte_count += cnt;
		}
		if (recvurg)
			if (myoob())
				goto got_oob;
		if (cnt < 0)
			goto data_err;
		transflag = 0;
		return (0);

	case TYPE_E:
		reply(553, "TYPE E not implemented.");
		transflag = 0;
		return (-1);

	case TYPE_A:
		while ((c = DATAGETC(instr)) != EOF) {
			if (recvurg)
				if (myoob())
					goto got_oob;
			byte_count++;
			if (c == '\n')
				bare_lfs++;
			while (c == '\r') {
				if (ferror(outstr))
					goto data_err;
				if ((c = DATAGETC(instr)) != '\n') {
					(void) putc ('\r', outstr);
					if (c == '\0' || c == EOF)
						goto contin2;
				}
			}
			(void) putc(c, outstr);
	contin2:	;
		}
		if (recvurg)
			if (myoob())
				goto got_oob;
		fflush(outstr);
		if (ferror(instr))
			goto data_err;
		if (ferror(outstr))
			goto file_err;
		transflag = 0;
		if (bare_lfs) {
			lreply(226,
		"WARNING! %d bare linefeeds received in ASCII mode.",
			    bare_lfs);
		(void)PRINTF("   File may not have transferred correctly.\r\n");
		}
		return (0);
	default:
		reply(550, "Unimplemented TYPE %d in receive_data.", type);
		transflag = 0;
		return (-1);
	}

data_err:
	transflag = 0;
	perror_reply(426, "Data connection");
	return (-1);

file_err:
	transflag = 0;
	perror_reply(452, "Error writing to file");
	return (-1);

got_oob:
	recvurg = 0;
	transflag = 0;
	return (-1);
}

void
statfilecmd(char *filename)
{
	FILE *fin;
	int atstart;
	int c, code;
	char line[LINE_MAX];
	struct stat st;

	code = lstat(filename, &st) == 0 && S_ISDIR(st.st_mode) ? 212 : 213;
	(void)snprintf(line, sizeof(line), _PATH_LS " -lgA %s", filename);
	fin = ftpd_popen(line, "r");
	lreply(code, "status of '%s':", filename);
	atstart = 1;
	while ((c = getc(fin)) != EOF) {
		if (c == '\n') {
			if (ferror(stdout)){
				perror_reply(421, "Control connection");
				(void) ftpd_pclose(fin);
				dologout(1);
				/* NOTREACHED */
			}
			if (ferror(fin)) {
				perror_reply(551, filename);
				(void) ftpd_pclose(fin);
				return;
			}
			(void) PUTC('\r', stdout);
		}
		/*
		 * RFC 959 says neutral text should be prepended before
		 * a leading 3-digit number followed by whitespace, but
		 * many ftp clients can be confused by any leading digits,
		 * as a matter of fact.
		 */
		if (atstart && isdigit(c))
			(void) PUTC(' ', stdout);
		(void) PUTC(c, stdout);
		atstart = (c == '\n');
	}
#ifdef USE_SSL
	if (ssl_active_flag)
		ssl_putc_flush(ssl_con);
#endif /* USE_SSL */
	(void) ftpd_pclose(fin);
	reply(code, "End of status.");
}

void
statcmd(void)
{
	union sockunion *su;
	u_char *a = NULL, *p = NULL;
	char hname[NI_MAXHOST];
	int ispassive;

	if (hostinfo) {
		lreply(211, "%s FTP server status:", hostname);
		PRINTF("     %s\r\n", version);
	} else
		lreply(211, "FTP server status:");
	PRINTF("     Connected to %s", remotehost);
	if (!getnameinfo((struct sockaddr *)&his_addr,
#ifdef LINUX /* Linux port */
			 SU_LEN(his_addr),
#else /* BSD source */
			 his_addr.su_len,
#endif /* BSD source */
			 hname, sizeof(hname) - 1, NULL, 0, NI_NUMERICHOST)) {
		hname[sizeof(hname) - 1] = 0;
		if (strcmp(hname, remotehost) != 0)
			PRINTF(" (%s)", hname);
	}
	PRINTF("\r\n");
	if (logged_in) {
		if (guest)
			PRINTF("     Logged in anonymously\r\n");
		else
			PRINTF("     Logged in as %s\r\n", curname);
	} else if (askpasswd)
		PRINTF("     Waiting for password\r\n");
	else
		PRINTF("     Waiting for user name\r\n");
#ifdef USE_SSL
	PRINTF("     TLS/SSL protection of control connection: %s\r\n",
		ssl_active_flag ? "ON" : "OFF");
	PRINTF("     TLS/SSL protection of data connections: %s\r\n",
		ssl_encrypt_data ? "ON" : "OFF");
	PRINTF("     FTP-SSL compatibility mode: %s\r\n",
		ssl_compat_flag ? "ON" : "OFF");
#endif /*USE_SSL*/
	PRINTF("     TYPE: %s", typenames[type]);
	if (type == TYPE_A || type == TYPE_E)
		PRINTF(", FORM: %s", formnames[form]);
	if (type == TYPE_L)
#if CHAR_BIT == 8
		PRINTF(" %d", CHAR_BIT);
#else
		PRINTF(" %d", bytesize);	/* need definition! */
#endif
	PRINTF("; STRUcture: %s; transfer MODE: %s\r\n",
	    strunames[stru], modenames[mode]);
	if (data != -1)
		PRINTF("     Data connection open\r\n");
	else if (pdata != -1) {
		ispassive = 1;
		su = &pasv_addr;
		goto printaddr;
	} else if (usedefault == 0) {
		ispassive = 0;
		su = &data_dest;
printaddr:
#define UC(b) (((int) b) & 0xff)
		if (epsvall) {
			PRINTF("     EPSV only mode (EPSV ALL)\r\n");
			goto epsvonly;
		}

		/* PORT/PASV */
		if (su->su_family == AF_INET) {
			a = (u_char *) &su->su_sin.sin_addr;
			p = (u_char *) &su->su_sin.sin_port;
			PRINTF("     %s (%d,%d,%d,%d,%d,%d)\r\n",
				ispassive ? "PASV" : "PORT",
				UC(a[0]), UC(a[1]), UC(a[2]), UC(a[3]),
				UC(p[0]), UC(p[1]));
		}

		/* LPRT/LPSV */
	    {
		int alen = 0, af, i;

		switch (su->su_family) {
		case AF_INET:
			a = (u_char *) &su->su_sin.sin_addr;
			p = (u_char *) &su->su_sin.sin_port;
			alen = sizeof(su->su_sin.sin_addr);
			af = 4;
			break;
#ifdef INET6
		case AF_INET6:
			a = (u_char *) &su->su_sin6.sin6_addr;
			p = (u_char *) &su->su_sin6.sin6_port;
			alen = sizeof(su->su_sin6.sin6_addr);
			af = 6;
			break;
#endif
		default:
			af = 0;
			break;
		}
		if (af) {
			PRINTF("     %s (%d,%d,", ispassive ? "LPSV" : "LPRT",
				af, alen);
			for (i = 0; i < alen; i++)
				PRINTF("%d,", UC(a[i]));
			PRINTF("%d,%d,%d)\r\n", 2, UC(p[0]), UC(p[1]));
		}
	    }

epsvonly:;
		/* EPRT/EPSV */
	    {
		int af;

		switch (su->su_family) {
		case AF_INET:
			af = 1;
			break;
#ifdef INET6
		case AF_INET6:
			af = 2;
			break;
#endif
		default:
			af = 0;
			break;
		}
		if (af) {
			union sockunion tmp;

			tmp = *su;
#ifdef INET6
			if (tmp.su_family == AF_INET6)
				tmp.su_sin6.sin6_scope_id = 0;
#endif
			if (!getnameinfo((struct sockaddr *)&tmp,
#ifdef LINUX /* Linux port */
					SU_LEN(tmp),
#else /* BSD source */
					tmp.su_len,
#endif /* BSD source */
					hname, sizeof(hname) - 1, NULL, 0,
					NI_NUMERICHOST)) {
				hname[sizeof(hname) - 1] = 0;
				PRINTF("     %s |%d|%s|%d|\r\n",
					ispassive ? "EPSV" : "EPRT",
					af, hname, htons(tmp.su_port));
			}
		}
	    }
#undef UC
	} else
		PRINTF("     No data connection\r\n");
	reply(211, "End of status.");
}

void
fatalerror(char *s)
{

	reply(451, "Error in server: %s", s);
	reply(221, "Closing connection due to server error.");
	dologout(0);
	/* NOTREACHED */
}

void
reply(int n, const char *fmt, ...)
{
	va_list ap;
#ifdef USE_SSL
	/* the size seems to be enough for normal use */
	char outputbuf[BUFSIZ + MAXPATHLEN + MAXHOSTNAMELEN];
	size_t outputbuflen;
#endif /* USE_SSL */

#ifdef USE_SSL
	snprintf(outputbuf, sizeof(outputbuf) - 2, "%d ", n);
	va_start(ap, fmt);
	outputbuflen = strlen(outputbuf);
	vsnprintf(outputbuf + outputbuflen,
		 sizeof(outputbuf) - outputbuflen - 2, fmt, ap);
	va_end(ap);
	strcat(outputbuf, "\r\n");

	if (ssl_debug_flag)
		ssl_log_msg(bio_err, "\n<--- %s", outputbuf);

	if (ssl_active_flag) {
		ssl_write(ssl_con, outputbuf, strlen(outputbuf));
	} else {
		printf("%s", outputbuf);
		fflush(stdout);
	}

	if (ftpdebug)
		syslog(LOG_DEBUG, "<--- %s ", outputbuf);
#else /* !USE_SSL */
	(void)printf("%d ", n);
	va_start(ap, fmt);
	(void)vprintf(fmt, ap);
	va_end(ap);
	(void)printf("\r\n");
	(void)fflush(stdout);
	if (ftpdebug) {
		syslog(LOG_DEBUG, "<--- %d ", n);
		va_start(ap, fmt);
		vsyslog(LOG_DEBUG, fmt, ap);
		va_end(ap);
	}
#endif /* USE_SSL */
}

void
lreply(int n, const char *fmt, ...)
{
	va_list ap;
#ifdef USE_SSL
	/* the size seems to be enough for normal use */
	char outputbuf[BUFSIZ + MAXPATHLEN + MAXHOSTNAMELEN];
	size_t outputbuflen;
#endif /* USE_SSL */

#ifdef USE_SSL
	snprintf(outputbuf, sizeof(outputbuf) - 2, "%d- ", n);
	va_start(ap, fmt);
	outputbuflen = strlen(outputbuf);
	vsnprintf(outputbuf + outputbuflen, 
		 sizeof(outputbuf) - outputbuflen - 2, fmt, ap);
	va_end(ap);
	strcat(outputbuf, "\r\n");

	if (ssl_debug_flag)
		ssl_log_msg(bio_err, "\n<--- %s", outputbuf);

	if (ssl_active_flag) {
		ssl_write(ssl_con, outputbuf, strlen(outputbuf));
	} else {
		printf("%s", outputbuf);
		fflush(stdout);
	}

	if (ftpdebug)
		syslog(LOG_DEBUG, "<--- %s ", outputbuf);
#else /* !USE_SSL */
	(void)printf("%d- ", n);
	va_start(ap, fmt);
	(void)vprintf(fmt, ap);
	va_end(ap);
	(void)printf("\r\n");
	(void)fflush(stdout);
	if (ftpdebug) {
		syslog(LOG_DEBUG, "<--- %d- ", n);
		va_start(ap, fmt);
		vsyslog(LOG_DEBUG, fmt, ap);
		va_end(ap);
	}
#endif /* USE_SSL */
}

static void
ack(char *s)
{

	reply(250, "%s command successful.", s);
}

void
nack(char *s)
{

	reply(502, "%s command not implemented.", s);
}

/*
 * Return the "Syntax error" reply to the client.
 * arguments:
 *	s - the buffer which contains the FTP command.
 */
void
synterr(char *s)
{
	char *cp;

	if ((cp = strchr(s,'\n')))
		*cp = '\0';
	reply(501, "%s: syntax error in arguments.", s);
}

/* ARGSUSED */
void
yyerror(char *s)
{
	char *cp;

	if ((cp = strchr(cbuf,'\n')))
		*cp = '\0';
	reply(500, "%s: command not understood.", cbuf);
}

void
delete(char *name)
{
	struct stat st;

	LOGCMD("delete", name);
	if (lstat(name, &st) < 0) {
		perror_reply(550, name);
		return;
	}
	if (S_ISDIR(st.st_mode)) {
		if (rmdir(name) < 0) {
			perror_reply(550, name);
			return;
		}
		goto done;
	}
	if (guest && noguestmod) {
		reply(550, "Operation not permitted.");
		return;
	}
	if (unlink(name) < 0) {
		perror_reply(550, name);
		return;
	}
done:
	ack("DELE");
}

void
cwd(char *path)
{

	if (chdir(path) < 0)
		perror_reply(550, path);
	else
		ack("CWD");
}

void
makedir(char *name)
{
	char *s;

	LOGCMD("mkdir", name);
	if (guest && noguestmkd)
		reply(550, "Operation not permitted.");
	else if (mkdir(name, 0777) < 0)
		perror_reply(550, name);
	else {
		if ((s = doublequote(name)) == NULL)
			fatalerror("Ran out of memory.");
		reply(257, "\"%s\" directory created.", s);
		free(s);
	}
}

void
removedir(char *name)
{

	LOGCMD("rmdir", name);
	if (rmdir(name) < 0)
		perror_reply(550, name);
	else
		ack("RMD");
}

void
pwd(void)
{
	char *s, path[MAXPATHLEN + 1];

	if (getcwd(path, sizeof(path)) == NULL)
		perror_reply(550, "Get current directory");
	else {
		if ((s = doublequote(path)) == NULL)
			fatalerror("Ran out of memory.");
		reply(257, "\"%s\" is current directory.", s);
		free(s);
	}
}

char *
renamefrom(char *name)
{
	struct stat st;

	if (guest && noguestmod) {
		reply(550, "Operation not permitted.");
		return (NULL);
	}
	if (lstat(name, &st) < 0) {
		perror_reply(550, name);
		return (NULL);
	}
	reply(350, "File exists, ready for destination name.");
	return (name);
}

void
renamecmd(char *from, char *to)
{
	struct stat st;

	LOGCMD2("rename", from, to);

	if (guest && (stat(to, &st) == 0)) {
		reply(550, "%s: permission denied.", to);
		return;
	}

	if (rename(from, to) < 0)
		perror_reply(550, "rename");
	else
		ack("RNTO");
}

static void
dolog(struct sockaddr *who)
{
	char who_name[NI_MAXHOST];

	realhostname_sa(remotehost, sizeof(remotehost) - 1, who,
#ifdef LINUX /* Linux port */
	    SA_LEN(who));
#else /* BSD source */
	    who->sa_len);
#endif /* BSD source */
	remotehost[sizeof(remotehost) - 1] = 0;
	if (getnameinfo(who,
#ifdef LINUX /* Linux port */
		    SA_LEN(who),
#else /* BSD source */
		    who->sa_len,
#endif /* BSD source */
		who_name, sizeof(who_name) - 1, NULL, 0, NI_NUMERICHOST))
			*who_name = 0;
	who_name[sizeof(who_name) - 1] = 0;

#ifdef SETPROCTITLE
#ifdef VIRTUAL_HOSTING
	if (thishost != firsthost)
		snprintf(proctitle, sizeof(proctitle), "%s: connected (to %s)",
			 remotehost, hostname);
	else
#endif
		snprintf(proctitle, sizeof(proctitle), "%s: connected",
			 remotehost);
	setproctitle("%s", proctitle);
#endif /* SETPROCTITLE */

	if (logging) {
#ifdef VIRTUAL_HOSTING
		if (thishost != firsthost)
			syslog(LOG_INFO, "connection from %s (%s) to %s",
			       remotehost, who_name, hostname);
		else
#endif
			syslog(LOG_INFO, "connection from %s (%s)",
			       remotehost, who_name);
	}
}

/*
 * Record logout in wtmp file
 * and exit with supplied status.
 */
void
dologout(int status)
{
	if (logged_in && dowtmp) {
		(void) seteuid(0);
		ftpd_logwtmp(ttyline, "", NULL);
	}

	if (logging)
		syslog(LOG_INFO, "connection %s", recvlostconn ?
		      "lost" : "closed");

	/* beware of flushing buffers after a SIGPIPE */
	_exit(status);
}

static void
sigurg(int signo)
{
	/* only process if transfer occurring */
	if (!transflag)
		return;

	recvurg = 1;
}

static int
myoob(void)
{
	char *cp;
#ifdef USE_SSL /* "pseudo-OOB" with SSL */
	fd_set mask;
	struct timeval tv;
#endif /*USE_SSL*/

	/* only process if transfer occurring */
	if (!transflag) {
		recvurg = 0;
		return 0;
	}
	cp = tmpline;
#ifdef USE_SSL /* "pseudo-OOB" with SSL */
	FD_ZERO(&mask);
	FD_SET(fileno(stdin),&mask);
	tv.tv_sec=0;
	tv.tv_usec=0;
	if (select(fileno(stdin)+1, &mask, NULL, NULL, &tv)) {
#endif /*USE_SSL*/
	if (get_line(cp, 7, stdin) == NULL) {
		reply(221, "You could at least say goodbye.");
		dologout(0);
	}
	upper(cp);
	if (strcmp(cp, "ABOR\r\n") == 0) {
		tmpline[0] = '\0';
		reply(426, "Transfer aborted. Data connection closed.");
		reply(226, "Abort successful.");

		return 1;
	}
	if (strcmp(cp, "STAT\r\n") == 0) {
		tmpline[0] = '\0';
		if (file_size != -1)
			reply(213, "Status: %jd of %jd bytes transferred.",
				   (intmax_t)byte_count, (intmax_t)file_size);
		else
			reply(213, "Status: %jd bytes transferred.",
				   (intmax_t)byte_count);
	}
#ifdef USE_SSL /* "pseudo-OOB" with SSL */
	}
#endif /*USE_SSL*/
	recvurg = 0;
	return 0;
}

/*
 * Note: a response of 425 is not mentioned as a possible response to
 *	the PASV command in RFC959. However, it has been blessed as
 *	a legitimate response by Jon Postel in a telephone conversation
 *	with Rick Adams on 25 Jan 89.
 */
void
passive(void)
{
	int len, on;
	char *p, *a;

	if (pdata >= 0)		/* close old port if one set */
		close(pdata);

	pdata = socket(ctrl_addr.su_family, SOCK_STREAM, 0);
	if (pdata < 0) {
		perror_reply(425, "Can't open passive connection");
		return;
	}
	on = 1;
	if (setsockopt(pdata, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		syslog(LOG_WARNING, "pdata setsockopt (SO_REUSEADDR): %m");

	(void) seteuid(0);

#ifdef IP_PORTRANGE
	if (ctrl_addr.su_family == AF_INET) {
	    on = restricted_data_ports ? IP_PORTRANGE_HIGH
				       : IP_PORTRANGE_DEFAULT;

	    if (setsockopt(pdata, IPPROTO_IP, IP_PORTRANGE,
			    &on, sizeof(on)) < 0)
		    goto pasv_error;
	}
#endif
#ifdef IPV6_PORTRANGE
	if (ctrl_addr.su_family == AF_INET6) {
	    on = restricted_data_ports ? IPV6_PORTRANGE_HIGH
				       : IPV6_PORTRANGE_DEFAULT;

	    if (setsockopt(pdata, IPPROTO_IPV6, IPV6_PORTRANGE,
			    &on, sizeof(on)) < 0)
		    goto pasv_error;
	}
#endif

	pasv_addr = ctrl_addr;
	pasv_addr.su_port = 0;
	if (bind(pdata, (struct sockaddr *)&pasv_addr, 
#ifdef LINUX /* Linux port */
	SU_LEN(pasv_addr)
#else /* BSD source */
	pasv_addr.su_len
#endif /* BSD source */
	) < 0)
		goto pasv_error;

	(void) seteuid(pw->pw_uid);

	len = sizeof(pasv_addr);
	if (getsockname(pdata, (struct sockaddr *) &pasv_addr, &len) < 0)
		goto pasv_error;
	if (listen(pdata, 1) < 0)
		goto pasv_error;
	if (pasv_addr.su_family == AF_INET)
		if (tun_pasvip_flag)
			a = (char *) &tun_pasvip_addr.su_sin.sin_addr;
		else
			a = (char *) &pasv_addr.su_sin.sin_addr;
#ifdef INET6
	else if (pasv_addr.su_family == AF_INET6 &&
		 IN6_IS_ADDR_V4MAPPED(&pasv_addr.su_sin6.sin6_addr))
		if (tun_pasvip_flag)
			a = (char *) &tun_pasvip_addr.su_sin.sin_addr;
		else
			a = (char *) &pasv_addr.su_sin6.sin6_addr.s6_addr[12];
#endif
	else
		goto pasv_error;
		
	p = (char *) &pasv_addr.su_port;

#define UC(b) (((int) b) & 0xff)

	reply(227, "Entering Passive Mode (%d,%d,%d,%d,%d,%d)", UC(a[0]),
		UC(a[1]), UC(a[2]), UC(a[3]), UC(p[0]), UC(p[1]));
	return;

pasv_error:
	(void) seteuid(pw->pw_uid);
	(void) close(pdata);
	pdata = -1;
	perror_reply(425, "Can't open passive connection");
	return;
}

/*
 * Long Passive defined in RFC 1639.
 *     228 Entering Long Passive Mode
 *         (af, hal, h1, h2, h3,..., pal, p1, p2...)
 */

void
long_passive(char *cmd, int pf)
{
	int len, on;
	char *p, *a;

	if (pdata >= 0)		/* close old port if one set */
		close(pdata);

	if (pf != PF_UNSPEC) {
		if (ctrl_addr.su_family != pf) {
			switch (ctrl_addr.su_family) {
			case AF_INET:
				pf = 1;
				break;
#ifdef INET6
			case AF_INET6:
				pf = 2;
				break;
#endif
			default:
				pf = 0;
				break;
			}
			/*
			 * XXX
			 * only EPRT/EPSV ready clients will understand this
			 */
			if (strcmp(cmd, "EPSV") == 0 && pf) {
				reply(522, "Network protocol mismatch, "
					"use (%d)", pf);
			} else
				reply(501, "Network protocol mismatch."); /*XXX*/

			return;
		}
	}
		
	pdata = socket(ctrl_addr.su_family, SOCK_STREAM, 0);
	if (pdata < 0) {
		perror_reply(425, "Can't open passive connection");
		return;
	}
	on = 1;
	if (setsockopt(pdata, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		syslog(LOG_WARNING, "pdata setsockopt (SO_REUSEADDR): %m");

	(void) seteuid(0);

	pasv_addr = ctrl_addr;
	pasv_addr.su_port = 0;
	len = 
#ifdef LINUX /* Linux port */
	SU_LEN(pasv_addr);
#else /* BSD source */
	pasv_addr.su_len;
#endif /* BSD source */

#ifdef IP_PORTRANGE
	if (ctrl_addr.su_family == AF_INET) {
	    on = restricted_data_ports ? IP_PORTRANGE_HIGH
				       : IP_PORTRANGE_DEFAULT;

	    if (setsockopt(pdata, IPPROTO_IP, IP_PORTRANGE,
			    &on, sizeof(on)) < 0)
		    goto pasv_error;
	}
#endif
#ifdef IPV6_PORTRANGE
	if (ctrl_addr.su_family == AF_INET6) {
	    on = restricted_data_ports ? IPV6_PORTRANGE_HIGH
				       : IPV6_PORTRANGE_DEFAULT;

	    if (setsockopt(pdata, IPPROTO_IPV6, IPV6_PORTRANGE,
			    &on, sizeof(on)) < 0)
		    goto pasv_error;
	}
#endif

	if (bind(pdata, (struct sockaddr *)&pasv_addr, len) < 0)
		goto pasv_error;

	(void) seteuid(pw->pw_uid);

	if (getsockname(pdata, (struct sockaddr *) &pasv_addr, &len) < 0)
		goto pasv_error;
	if (listen(pdata, 1) < 0)
		goto pasv_error;

#define UC(b) (((int) b) & 0xff)

	if (strcmp(cmd, "LPSV") == 0) {
		p = (char *)&pasv_addr.su_port;
		switch (pasv_addr.su_family) {
		case AF_INET:
			if (tun_pasvip_flag)
				a = (char *) &tun_pasvip_addr.su_sin.sin_addr;
			else
				a = (char *) &pasv_addr.su_sin.sin_addr;
#ifdef INET6
		v4_reply:
#endif
			reply(228,
"Entering Long Passive Mode (%d,%d,%d,%d,%d,%d,%d,%d,%d)",
			      4, 4, UC(a[0]), UC(a[1]), UC(a[2]), UC(a[3]),
			      2, UC(p[0]), UC(p[1]));
			return;
#ifdef INET6
		case AF_INET6:
			if (IN6_IS_ADDR_V4MAPPED(&pasv_addr.su_sin6.sin6_addr)) {
				if (tun_pasvip_flag)
					a = (char *) &tun_pasvip_addr.su_sin.sin_addr;
				else
					a = (char *) &pasv_addr.su_sin6.sin6_addr.s6_addr[12];
				goto v4_reply;
			}
			a = (char *) &pasv_addr.su_sin6.sin6_addr;
			reply(228,
"Entering Long Passive Mode "
"(%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d)",
			      6, 16, UC(a[0]), UC(a[1]), UC(a[2]), UC(a[3]),
			      UC(a[4]), UC(a[5]), UC(a[6]), UC(a[7]),
			      UC(a[8]), UC(a[9]), UC(a[10]), UC(a[11]),
			      UC(a[12]), UC(a[13]), UC(a[14]), UC(a[15]),
			      2, UC(p[0]), UC(p[1]));
			return;
#endif
		}
	} else if (strcmp(cmd, "EPSV") == 0) {
		switch (pasv_addr.su_family) {
		case AF_INET:
#ifdef INET6
		case AF_INET6:
#endif
			reply(229, "Entering Extended Passive Mode (|||%d|)",
				ntohs(pasv_addr.su_port));
			return;
		}
	} else {
		/* more proper error code? */
	}

pasv_error:
	(void) seteuid(pw->pw_uid);
	(void) close(pdata);
	pdata = -1;
	perror_reply(425, "Can't open passive connection");
	return;
}

/*
 * Generate unique name for file with basename "local"
 * and open the file in order to avoid possible races.
 * Try "local" first, then "local.1", "local.2" etc, up to "local.99".
 * Return descriptor to the file, set "name" to its name.
 *
 * Generates failure reply on error.
 */
static int
guniquefd(char *local, char **name)
{
	static char new[MAXPATHLEN];
	struct stat st;
	char *cp;
	int count;
	int fd;

	cp = strrchr(local, '/');
	if (cp)
		*cp = '\0';
	if (stat(cp ? local : ".", &st) < 0) {
		perror_reply(553, cp ? local : ".");
		return (-1);
	}
	if (cp) {
		/*
		 * Let not overwrite dirname with counter suffix.
		 * -4 is for /nn\0
		 * In this extreme case dot won't be put in front of suffix.
		 */
		if (strlen(local) > sizeof(new) - 4) {
			reply(553, "Pathname too long.");
			return (-1);
		}
		*cp = '/';
	}
	/* -4 is for the .nn<null> we put on the end below */
	(void) snprintf(new, sizeof(new) - 4, "%s", local);
	cp = new + strlen(new);
	/* 
	 * Don't generate dotfile unless requested explicitly.
	 * This covers the case when basename gets truncated off
	 * by buffer size.
	 */
	if (cp > new && cp[-1] != '/')
		*cp++ = '.';
	for (count = 0; count < 100; count++) {
		/* At count 0 try unmodified name */
		if (count)
			(void)sprintf(cp, "%d", count);
		if ((fd = open(count ? new : local,
		    O_RDWR | O_CREAT | O_EXCL, 0666)) >= 0) {
			*name = count ? new : local;
			return (fd);
		}
		if (errno != EEXIST) {
			perror_reply(553, count ? new : local);
			return (-1);
		}
	}
	reply(452, "Unique file name cannot be created.");
	return (-1);
}

/*
 * Format and send reply containing system error number.
 */
void
perror_reply(int code, char *string)
{

	reply(code, "%s: %s.", string, strerror(errno));
}

static char *onefile[] = {
	"",
	0
};

void
send_file_list(char *whichf)
{
	struct stat st;
	DIR *dirp = NULL;
	struct dirent *dir;
	FILE *dout = NULL;
	char **dirlist, *dirname;
	int simple = 0;
	int freeglob = 0;
	glob_t gl;
	char buf[BUFSIZ + MAXPATHLEN];

	if (strpbrk(whichf, "~{[*?") != NULL) {
		int flags = GLOB_BRACE|GLOB_NOCHECK|GLOB_TILDE;

		memset(&gl, 0, sizeof(gl));
		gl.gl_matchc = MAXGLOBARGS;
		flags |= GLOB_LIMIT;
		freeglob = 1;
#ifdef LINUX /* Linux port */
		if (bsdglob(whichf, flags, 0, &gl)) {
#else /* BSD source */
		if (glob(whichf, flags, 0, &gl)) {
#endif /* BSD source */
			reply(550, "No matching files found.");
			goto out;
		} else if (gl.gl_pathc == 0) {
			errno = ENOENT;
			perror_reply(550, whichf);
			goto out;
		}
		dirlist = gl.gl_pathv;
	} else {
		onefile[0] = whichf;
		dirlist = onefile;
		simple = 1;
	}

	while ((dirname = *dirlist++)) {
		if (stat(dirname, &st) < 0) {
			/*
			 * If user typed "ls -l", etc, and the client
			 * used NLST, do what the user meant.
			 */
			if (dirname[0] == '-' && *dirlist == NULL &&
			    transflag == 0) {
				retrieve(_PATH_LS " %s", dirname);
				goto out;
			}
			perror_reply(550, whichf);
			if (dout != NULL) {
#ifdef USE_SSL
				if (ssl_data_active_flag &&
				    (ssl_data_con != NULL)) {
					if (SSL_shutdown(ssl_data_con) == 0) {
					    switch (SSL_get_shutdown(ssl_data_con)) {
					    case SSL_SENT_SHUTDOWN:
						SSL_get_shutdown(ssl_data_con);
						break;
					    default:
						break;
					    }
					}
					SSL_free(ssl_data_con);
					ssl_data_active_flag = 0;
					ssl_data_con = NULL;
				}
#endif /* USE_SSL */
				(void) fclose(dout);
				transflag = 0;
				data = -1;
				pdata = -1;
			}
			goto out;
		}

		if (S_ISREG(st.st_mode)) {
			if (dout == NULL) {
				dout = dataconn("file list", -1, "w");
				if (dout == NULL)
					goto out;
				transflag++;
			}
			snprintf(buf, sizeof(buf), "%s%s\n", dirname,
				type == TYPE_A ? "\r" : "");
#ifdef USE_SSL
			if (ssl_data_active_flag)
				ssl_write(ssl_data_con, buf, strlen(buf));
			else
#endif /* USE_SSL */
				fwrite(buf, strlen(buf), 1, dout);
			byte_count += strlen(dirname) + 1;
			continue;
		} else if (!S_ISDIR(st.st_mode))
			continue;

		if ((dirp = opendir(dirname)) == NULL)
			continue;

		while ((dir = readdir(dirp)) != NULL) {
			char nbuf[MAXPATHLEN];

			if (recvurg)
				if (myoob()) {
					recvurg = 0;
					transflag = 0;
					goto out;
				}

#ifdef LINUX
			if (dir->d_name[0] == '.' && dir->d_name[1] == '\0')
				continue;
			if (dir->d_name[0] == '.' && dir->d_name[1] == '.' &&
			    dir->d_name[2] == '\0')
				continue;
#else /* BSD source */
			if (dir->d_name[0] == '.' && dir->d_namlen == 1)
				continue;
			if (dir->d_name[0] == '.' && dir->d_name[1] == '.' &&
			    dir->d_namlen == 2)
				continue;
#endif /* BSD source */

			snprintf(nbuf, sizeof(nbuf),
				"%s/%s", dirname, dir->d_name);

			/*
			 * We have to do a stat to insure it's
			 * not a directory or special file.
			 */
			if (simple || (stat(nbuf, &st) == 0 &&
			    S_ISREG(st.st_mode))) {
				if (dout == NULL) {
					dout = dataconn("file list", -1, "w");
					if (dout == NULL)
						goto out;
					transflag++;
				}
				if (nbuf[0] == '.' && nbuf[1] == '/')
					snprintf(buf, sizeof(buf),
					         "%s%s\n", &nbuf[2],
					         type == TYPE_A ? "\r" : "");
				else
					snprintf(buf, sizeof(buf),
					         "%s%s\n", nbuf,
						 type == TYPE_A ? "\r" : "");
#ifdef USE_SSL
				if (ssl_data_active_flag)
					ssl_write(ssl_data_con, buf, strlen(buf));
				else
#endif /* USE_SSL */
					fwrite(buf, strlen(buf), 1, dout);
				byte_count += strlen(nbuf) + 1;
			}
		}
		(void) closedir(dirp);
	}

	if (dout == NULL)
		reply(550, "No files found.");
	else if (ferror(dout) != 0)
		perror_reply(550, "Data connection");
	else
		reply(226, "Transfer complete.");

	transflag = 0;
	if (dout != NULL) {
#ifdef USE_SSL
		if (ssl_data_active_flag && (ssl_data_con != NULL)) {
			if (SSL_shutdown(ssl_data_con) == 0) {
			    switch (SSL_get_shutdown(ssl_data_con)) {
			    case SSL_SENT_SHUTDOWN:
				SSL_get_shutdown(ssl_data_con);
				break;
			    default:
				break;
			    }
			}
			SSL_free(ssl_data_con);
			ssl_data_active_flag = 0;
			ssl_data_con = NULL;
		}
#endif /* USE_SSL */
		(void) fclose(dout);
	}
	data = -1;
	pdata = -1;
out:
	if (freeglob) {
		freeglob = 0;
		globfree(&gl);
	}
}

void
reapchild(int signo)
{
	while (waitpid(-1, NULL, WNOHANG) > 0);
}

#ifdef OLD_SETPROCTITLE
/*
 * Clobber argv so ps will show what we're doing.  (Stolen from sendmail.)
 * Warning, since this is usually started from inetd.conf, it often doesn't
 * have much of an environment or arglist to overwrite.
 */
void
setproctitle(const char *fmt, ...)
{
	int i;
	va_list ap;
	char *p, *bp, ch;
	char buf[LINE_MAX];

	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);

	/* make ps print our process name */
	p = Argv[0];
	*p++ = '-';

	i = strlen(buf);
	if (i > LastArgv - p - 2) {
		i = LastArgv - p - 2;
		buf[i] = '\0';
	}
	bp = buf;
	while (ch = *bp++)
		if (ch != '\n' && ch != '\r')
			*p++ = ch;
	while (p < LastArgv)
		*p++ = ' ';
}
#endif /* OLD_SETPROCTITLE */

static void
appendf(char **strp, char *fmt, ...)
{
	va_list ap;
	char *ostr, *p;

	va_start(ap, fmt);
	vasprintf(&p, fmt, ap);
	va_end(ap);
	if (p == NULL)
		fatalerror("Ran out of memory.");
	if (*strp == NULL)
		*strp = p;
	else {
		ostr = *strp;
		asprintf(strp, "%s%s", ostr, p);
		if (*strp == NULL)
			fatalerror("Ran out of memory.");
		free(ostr);
	}
}

static void
logcmd(char *cmd, char *file1, char *file2, off_t cnt)
{
	char *msg = NULL;
	char wd[MAXPATHLEN + 1];

	if (logging <= 1)
		return;

	if (getcwd(wd, sizeof(wd) - 1) == NULL)
		snprintf(wd, sizeof(wd), "%s", strerror(errno));

	appendf(&msg, "%s", cmd);
	if (file1)
		appendf(&msg, " %s", file1);
	if (file2)
		appendf(&msg, " %s", file2);
	if (cnt >= 0)
		appendf(&msg, " = %jd bytes", (intmax_t)cnt);
	appendf(&msg, " (wd: %s", wd);
	if (guest || dochroot)
		appendf(&msg, "; chrooted");
	appendf(&msg, ")");
	syslog(LOG_INFO, "%s", msg);
	free(msg);
}

/*
 * Log the transfer information.
 * arguments:
 *	cmd  - the name of the ftp(1) operation.
 *	name - the name of the file.
 *	size - the size of the file (not the size of the transfer!).
 *	time_start, time_end - the values of times (according to time(3)) of
 *		the begin and the end of transfer, respectively.
 *	outgoing_flag - is not 0 in case of outgoing transfer.
 *	err_flag - is not 0 if errors occurred during the transfer.
 * return:
 *	none.
 */
static void
logxfer(char *cmd, char *name, off_t size, time_t time_start, time_t time_end,
	int outgoing_flag, int err_flag)
{
	/* don't use the own format for syslog if the wu-ftpd style format is
	 * enabled */
	if (!xferlog_syslog)
		LOGBYTES(cmd, name, byte_count);

	/* don't log to file in the own format if the wu-ftpd style one is
	 * enabled */
	if (!xferlog_stat && guest && stats && outgoing_flag && byte_count > 0)
		logxfer_anon(name, byte_count, time_start, time_end);

	/* use the wu-ftpd style xferlog if it is enabled */
	if (xferlog_stat || xferlog_syslog)
		logxfer_wuftpd(cmd, name, size, time_start, time_end,
		    outgoing_flag, err_flag);
}

/*
 * Log anonymous file downloads.
 * arguments:
 *	name - the name of the file.
 *	size - the size of the file (can be the size of the transfer).
 *	time_start, time_end - the values of times (according to time(3)) of
 *		the begin and the end of transfer, respectively.
 * return:
 *	none.
 */
static void
logxfer_anon(char *name, off_t size, time_t time_start, time_t time_end)
{
	char *buf = NULL;
	char path[MAXPATHLEN + 1];

	if (statfd >= 0) {
		if (realpath(name, path) == NULL) {
			syslog(LOG_WARNING, "realpath failed on '%s': %m", path);
			return;
		}
		appendf(&buf, "%.20s!%s!%s!%s%s!%jd!%ld\n", ctime(&time_end) + 4,
			ident, remotehost,
			(guest || dochroot) && xferlog_anon_realroot &&
			/* check the special case: a chroot directory and the
			 * real root directory are the same */
			rchrootdir[1] != 0 ?
			rchrootdir : "", path, (intmax_t)size,
			(long)(time_end-time_start + (time_end==time_start)));
		write(statfd, buf, strlen(buf));
		free(buf);
	}
}

/*
 * Log the transfer information in the wu-ftpd style format.
 * arguments:
 *	cmd  - the name of the ftp(1) operation.
 *	name - the name of the file.
 *	size - the size of the file (not the size of the transfer!).
 *	time_start, time_end - the values of times (according to time(3)) of
 *		the begin and the end of transfer, respectively.
 *	outgoing_flag - is not 0 in case of outgoing transfer.
 *	err_flag - is not 0 if errors occurred during the transfer.
 * return:
 *	none.
 * notes:
 *	In case of an incoming transfer (outgoing_flag == 0) the "size" must
 *	be the size of the file AFTER the completion of the transfer.
 */
static void
logxfer_wuftpd(char *cmd, char *name, off_t size, time_t time_start,
	    time_t time_end, int outgoing_flag, int err_flag)
{
	char *msg = NULL; /* original fields of the wu-ftpd xferlog */
	char *msg_ext = NULL; /* extended fields of the xferlog */
	char wd[MAXPATHLEN + 1], path[MAXPATHLEN + 1];
	time_t xfertime = time_end - time_start + (time_end == time_start);
	time_t curtime = time(NULL);

	if (getcwd(wd, sizeof(wd)) == NULL) {
		int errno_ori = errno;
		syslog(LOG_WARNING, "getcwd failed: %m");
		snprintf(wd, sizeof(wd), "'%s'", strerror(errno_ori));
	}
	if (realpath(name, path) == NULL) {
		int errno_ori = errno;
		syslog(LOG_WARNING, "realpath failed on '%s': %m", path);
		snprintf(path, sizeof(path), "'%s'", strerror(errno_ori));
	}

	appendf(&msg, "%.24s %ld %s %jd %s%s %c %s %c %c %s ftp %d %s %c",
		/* current-time in the form "DDD MMM dd hh:mm:ss YYYY" */
		ctime(&curtime),
		/* transfer-time (seconds) */
		(long)xfertime,
		/* remote-host */
		remotehost,
		/* byte-count (bytes) */
		byte_count > 0 ? (intmax_t)byte_count : 0,
		/* filename */
		(guest || dochroot) && xferlog_wu_realroot &&
		    /* check the special case: a chroot directory and the real
		     * root directory are the same */
		    rchrootdir[1] != 0 ?
		    rchrootdir : "", path,
		/* transfer-type (ascii/binary: [a|b]) */
		type == TYPE_A ? 'a' : 'b',
		/* special-action-flag (gzip/gunzip/tar/none: [C|U|T|_]) */
		"_",
		/* direction (incoming/outgoing: [i|o]) */
		outgoing_flag ? 'o' : 'i',
		/* access-mode (anonymous/guest/real user: [a|g|r]) */
		guest ? 'a' : dochroot ? 'g' : 'r',
		/* username (ident string/login name) */
		guest ? ident : curname,
		/* authentication-method (none/RFC931: [0|1]) */
		0,
		/* authentication-user-id; '*' if it isn't available */
		"*",
		/* completion-status (complete/incomplete: [c|i]) */
		err_flag ? 'i' : 'c'
	);

	if (xferlog_stat == XFERLOG_EXT || xferlog_syslog == XFERLOG_EXT) {
		appendf(&msg_ext, " %jd %jd %s %s %s",
		/* restart-point (bytes), file-size (bytes) */
		(intmax_t)restart_point, (intmax_t)size,
		/* cwd, filename-arg */
		wd, name,
		/* protection-level
		 * (Clear/Safe/Confidential/Private: [C|S|E|P]) */
#ifdef USE_SSL
		ssl_encrypt_data ? "P" : "C");
#else /* !USE_SSL */
		"C");
#endif /* USE_SSL */
	}

	if (xferlog_syslog)
		syslog(LOG_INFO, "xferlog (%s): %s%s",
		    /* The extended format or... */
		    xferlog_syslog == XFERLOG_EXT ? cmd :
		    /* ...the original wu-ftpd one */
		    (outgoing_flag ? "send" : "recv"),
		    /* Skip date/time */
		    msg + 25,
		    /* Append extended fields */
		    xferlog_syslog == XFERLOG_EXT ? msg_ext : "");

	if (statfd >= 0 && xferlog_stat) {
		/* Append '\n' and extended fields */
		appendf(&msg, "%s\n",
		    xferlog_stat == XFERLOG_EXT ? msg_ext: "");

		write(statfd, msg, strlen(msg));
	}

	free(msg);
	free(msg_ext);
}

static char *
doublequote(char *s)
{
	int n;
	char *p, *s2;

	for (p = s, n = 0; *p; p++)
		if (*p == '"')
			n++;

	if ((s2 = malloc(p - s + n + 1)) == NULL)
		return (NULL);

	for (p = s2; *s; s++, p++) {
		if ((*p = *s) == '"')
			*(++p) = '"';
	}
	*p = '\0';

	return (s2);
}

/* setup server socket for specified address family */
/* if af is PF_UNSPEC more than one socket may be returned */
/* the returned list is dynamically allocated, so caller needs to free it */
static int *
socksetup(int af, char *bindname, const char *bindport)
{
	struct addrinfo hints, *res, *r;
	int error, maxs, *s, *socks;
	const int on = 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = af;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(bindname, bindport, &hints, &res);
	if (error) {
		syslog(LOG_ERR, "%s", gai_strerror(error));
		if (error == EAI_SYSTEM)
			syslog(LOG_ERR, "%s", strerror(errno));
		return NULL;
	}

	/* Count max number of sockets we may open */
	for (maxs = 0, r = res; r; r = r->ai_next, maxs++)
		;
	socks = malloc((maxs + 1) * sizeof(int));
	if (!socks) {
		freeaddrinfo(res);
		syslog(LOG_ERR, "couldn't allocate memory for sockets");
		return NULL;
	}

	*socks = 0;   /* num of sockets counter at start of array */
	s = socks + 1;
	for (r = res; r; r = r->ai_next) {
		*s = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
		if (*s < 0) {
			syslog(LOG_DEBUG, "control socket: %m");
			continue;
		}
		if (setsockopt(*s, SOL_SOCKET, SO_REUSEADDR,
		    &on, sizeof(on)) < 0)
			syslog(LOG_WARNING,
			    "control setsockopt (SO_REUSEADDR): %m");
#if defined(INET6) && defined(IPV6_V6ONLY)
		/* In Linux, IPV6_V6ONLY socket option does not exist in
		 * kernel 2.4.20 or earlier... */
		if (r->ai_family == AF_INET6) {
			if (setsockopt(*s, IPPROTO_IPV6, IPV6_V6ONLY,
			    &on, sizeof(on)) < 0)
				syslog(LOG_WARNING,
				    "control setsockopt (IPV6_V6ONLY): %m");
		}
#endif /* INET6 */
		if (bind(*s, r->ai_addr, r->ai_addrlen) < 0) {
			syslog(LOG_DEBUG, "control bind: %m");
			close(*s);
			continue;
		}
		(*socks)++;
		s++;
	}

	if (res)
		freeaddrinfo(res);

	if (*socks == 0) {
		syslog(LOG_ERR, "control socket: Couldn't bind to any socket");
		free(socks);
		return NULL;
	}
	return(socks);
}

#ifdef USE_SSL

/*
 * Initialize the TLS/SSL session on a control connection.
 */
void
do_ssl_start()
{
    char errstr[BUFSIZ];
    char *ssl_version;
    int ssl_bits;
    SSL_CIPHER *ssl_cipher;

    if (ssl_debug_flag)
	    ssl_log_msgn(bio_err, "do_ssl_start triggered");

    /* Do the SSL stuff now... Before we play with pty's. */
    ssl_con = (SSL *)SSL_new(ssl_ctx);
    SSL_set_accept_state(ssl_con);

    /* We are working with stdin (inetd based) by default. */
    SSL_set_fd(ssl_con, 0);

    if (SSL_accept(ssl_con) <= 0) {
	    switch (SSL_get_verify_result(ssl_con)) {
	    case X509_V_ERR_CRL_SIGNATURE_FAILURE:
		    snprintf(errstr, sizeof(errstr),
			    "invalid signature on CRL!");
		    break;
	    case X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD:
		    snprintf(errstr, sizeof(errstr),
			    "found CRL has invalid nextUpdate field");
		    break;
	    case X509_V_ERR_CRL_HAS_EXPIRED:
		    snprintf(errstr, sizeof(errstr),
			    "found CRL expired - revoking all certificates until you get updated CRL");
		    break;
	    case X509_V_ERR_CERT_REVOKED:
		    snprintf(errstr, sizeof(errstr),
			    "client certificate is revoked");
		    break;
	    default:
		    snprintf(errstr, sizeof(errstr), "%s",
			    ERR_reason_error_string(ERR_peek_error()));
	    }

	    if (logging)
		    syslog(LOG_NOTICE, "TLS/SSL FAILED WITH %s (reason: %s)",
			  remotehost, errstr);

	    snprintf(errstr, sizeof(errstr), "SSL_accept: %s.",
		    ERR_reason_error_string(ERR_get_error()));
	    reply(421, "%s", errstr);

	    SSL_free(ssl_con);
	    ssl_con = NULL;

	    dologout(1);
    } else {
	    ssl_active_flag = 1;

	    ssl_version = SSL_get_cipher_version(ssl_con);
	    ssl_cipher = SSL_get_current_cipher(ssl_con);
	    SSL_CIPHER_get_bits(ssl_cipher, &ssl_bits);

	    if (logging)
        	    syslog(LOG_INFO,
			"TLS/SSL SUCCEEDED WITH %s (%s, %s, cipher %s, %d bits)",
			remotehost, ssl_compat_flag ? "FTP-SSL" : "FTP-TLS",
			ssl_version, SSL_CIPHER_get_name(ssl_cipher), ssl_bits);
    }

    /*
     * ssl_fprintf calls require that this be null to test
     * for being an ssl stream.
     */
    if (!ssl_active_flag) {
	    if (ssl_con != NULL)
		    SSL_free(ssl_con);
	    ssl_con = NULL;
    }
}

/*
 * Do the X.509 user authentication and log results
 * arguments:
 *	name - login name provided by the client.
 * return:
 *	1 - peer's X.509 certificate is authorized to use name.
 *	0 - otherwise, or if the peer doesn't provide the cert (for any reason,
 *	    include the unencrypted connection), or if errors occurred.
 */
int
good_ssl_user(name)
char *name;
{
    X509 *cert;
    int ret;

    if (!x509_auth_flag)
	    return 0; /* can't happen */

    cert = SSL_get_peer_certificate(ssl_con);
    if (cert != NULL) {
	    /* Do X.509 auth. */
	    if (ssl_x509_auth(_SERVICE_NAME, cert, name) == 1) {
    		    ret = 1;
	    } else {
		    ret = 0;
	    }
	    if (logging)
    		    syslog(LOG_INFO, ret ?
			"X.509 AUTH ACCEPTED FOR SUBJECT: %s" :
			"X.509 AUTH REJECTED FOR SUBJECT: %s",
			(char *)ONELINE_NAME(X509_get_subject_name(cert)));
    } else {
	    /* Can't authenticate because no peer certificate provided. */
	    ret = 0;
    }

    X509_free(cert);
    return ret;
}

#endif /* USE_SSL */

#ifdef USE_PAM
static void
ftpd_openlog()
{
    closelog();

    /*
     * LOG_NDELAY sets up the logging connection immediately,
     * necessary for anonymous ftp's that chroot and can't do it later.
     */
    openlog(_SERVICE_NAME, LOG_PID | LOG_NDELAY, LOG_FTP);
}
#endif /* USE_PAM */

#ifdef TCPWRAPPERS
static int
check_host(struct sockaddr *sa)
{
    struct request_info req;

    int error_name, error_addr;
    char sa_name[NI_MAXHOST], sa_addr[NI_MAXHOST];

    error_name = getnameinfo(sa,
#ifdef LINUX /* Linux port */
		SA_LEN(sa),
#else /* BSD source */
		sa->sa_len,
#endif /* BSD source */
		sa_name, sizeof(sa_name) - 1, NULL, 0, NI_NAMEREQD);
    error_addr = getnameinfo(sa,
#ifdef LINUX /* Linux port */
		SA_LEN(sa),
#else /* BSD source */
		sa->sa_len,
#endif /* BSD source */
		sa_addr, sizeof(sa_addr) - 1, NULL, 0, NI_NUMERICHOST);

    /* Fill req struct with port name and fd number. */
    if (!error_name) {
	    request_init(&req, RQ_DAEMON, _SERVICE_NAME, RQ_CLIENT_NAME,
		    sa_name, RQ_CLIENT_ADDR, error_addr == 0 ? sa_addr : "",
		    RQ_USER, STRING_UNKNOWN, NULL);
    } else {
	    request_init(&req, RQ_DAEMON, _SERVICE_NAME, RQ_CLIENT_NAME,
		    STRING_UNKNOWN, RQ_CLIENT_ADDR, error_addr == 0 ? sa_addr :
		    "", RQ_USER, STRING_UNKNOWN, NULL);
    }

    if (!hosts_access(&req)) {
	    syslog(deny_severity, "connection from %.256s rejected by tcp_wrappers",
		  eval_client(&req));
	    return (0);
    }

    syslog(allow_severity, "connection from %.256s granted by tcp_wrappers",
	  eval_client(&req));
    return (1);
}
#endif /* TCPWRAPPERS */

/*
 * Set the global variable "tun_pasvip_addr", which is used to override the IP
 * address that will be advertised to IPv4 clients in response to the PASV/LPSV
 * commands.
 * arguments:
 *	tun_pasvip_str - the string that contains an IPv4 address or a symbolic
 *	                 host name, which is expected to be resolvable as
 *			 an IPv4 family address.
 * return:
 *	1 - an IPv4 family address has been successfully obtained from
 *	    "tun_pasvip_str".
 *	0 - otherwise, or if overriding of the IP address is turned off.
 * notes:
 *	The "tun_pasvip_str" string can contain an IP address or a symbolic
 *	host name, which will be resolved in DNS, but in daemon mode only
 *	the IP addresses are allowed.
 */
static int
set_pasvip_addr(char *tun_pasvip_str)
{
    struct addrinfo hints, *res;
    int error;

    if (tun_pasvip_str == NULL || tun_pasvip_flag == 0) {
	    tun_pasvip_flag = 0;
	    return 0; /* unknown error */
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_addr = NULL;
    if (daemon_mode) {
	    hints.ai_flags = AI_NUMERICHOST;
    }

    error = getaddrinfo(tun_pasvip_str, NULL, &hints, &res);
    if (error) {
	    tun_pasvip_flag = 0;
	    syslog(LOG_ERR, "can't set an IP address for passive mode: %s",
		  gai_strerror(error));
	    if (error == EAI_SYSTEM)
		    syslog(LOG_ERR, "%s", strerror(errno));
	    return 0;
    }
    if (res->ai_family != AF_INET || res->ai_addrlen > sizeof(tun_pasvip_addr)) {
	    /* Can't obtain an IPv4 address. */
	    tun_pasvip_flag = 0;
	    syslog(LOG_ERR,
		  "only an IPv4 family address may be used for PASV command");
	    return 0;
    }
    memcpy(&tun_pasvip_addr, res->ai_addr, res->ai_addrlen);

    return 1; /* OK */
}

/*
 * An implementation of the FEAT command defined in RFC 2389. Note: the feature
 * names returned are not command names, as such, but simply indications that
 * the server possesses some attribute or other.
 */
void
feat(void)
{
    struct feat_tab {
	    char *name;
    };

    struct feat_tab feat_list[] = {
#ifdef USE_SSL
	    /* RFC 2228 */
	    { "AUTH TLS" }, { "PBSZ" }, { "PROT" },
#endif /* USE_SSL */
	    /* RFC 3659 */
	    { "SIZE" }, { "MDTM" }, { "REST STREAM" },
	    { NULL }
    };

    struct feat_tab *c;

    lreply(211, "Extensions supported:");
    for (c = feat_list; c->name != NULL; c++)
	    PRINTF(" %s\r\n", c->name);
    FFLUSH(stdout);
    reply(211, "End.");
}
