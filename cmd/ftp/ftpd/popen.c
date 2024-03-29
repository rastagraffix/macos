/*
 * Copyright (c) 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software written by Ken Arnold and
 * published in UNIX Review, Vol. 6, No. 8.
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
static char sccsid[] = "@(#)popen.c	8.3 (Berkeley) 4/6/94";
#endif
#endif /* not lint */

#if 0
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/libexec/ftpd/popen.c,v 1.26 2004/11/18 13:46:29 yar Exp $");
#endif

#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include <errno.h>

#ifdef LINUX /* Linux port */
#include "bsdglob.h"
#else /* BSD source */
#include <glob.h>
#endif /* BSD source */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"
#ifdef	INTERNAL_LS
#include "pathnames.h"
#include <syslog.h>
#include <time.h>
#endif

#define	MAXUSRARGS	100
#define	MAXGLOBARGS	1000

/*
 * Special version of popen which avoids call to shell.  This ensures noone
 * may create a pipe to a hidden program as a side effect of a list or dir
 * command.
 */
static int *pids;
static int fds;

FILE *
ftpd_popen(char *program, char *type)
{
	char *cp;
	static FILE *iop;
	int argc, pdes[2], pid;
	static int gargc;
	char **pop, *argv[MAXUSRARGS], *gargv[MAXGLOBARGS];

	if (((*type != 'r') && (*type != 'w')) || type[1])
		return (NULL);

	if (!pids) {
		if ((fds = getdtablesize()) <= 0)
			return (NULL);
		if ((pids = malloc(fds * sizeof(int))) == NULL)
			return (NULL);
		memset(pids, 0, fds * sizeof(int));
	}
	if (pipe(pdes) < 0)
		return (NULL);

	/* break up string into pieces */
	for (argc = 0, cp = program; argc < MAXUSRARGS; cp = NULL) {
		if (!(argv[argc++] = strtok(cp, " \t\n")))
			break;
	}
	argv[argc - 1] = NULL;

	/* glob each piece */
	gargv[0] = argv[0];
	for (gargc = argc = 1; argv[argc] && gargc < (MAXGLOBARGS-1); argc++) {
		glob_t gl;
		int flags = GLOB_BRACE|GLOB_NOCHECK|GLOB_TILDE;

		memset(&gl, 0, sizeof(gl));
		gl.gl_matchc = MAXGLOBARGS;
		flags |= GLOB_LIMIT;
#ifdef LINUX /* Linux port */
		if (bsdglob(argv[argc], flags, NULL, &gl))
#else /* BSD source */
		if (glob(argv[argc], flags, NULL, &gl))
#endif /* BSD source */
			gargv[gargc++] = strdup(argv[argc]);
		else
			for (pop = gl.gl_pathv; *pop && gargc < (MAXGLOBARGS-1);
			     pop++)
				gargv[gargc++] = strdup(*pop);
		globfree(&gl);
	}
	gargv[gargc] = NULL;

	iop = NULL;
#ifdef	INTERNAL_LS
	fflush(NULL);
	pid = (strcmp(gargv[0], _PATH_LS) == 0) ? fork() : vfork();
#else
	pid = vfork();
#endif
	switch(pid) {
	case -1:			/* error */
		(void)close(pdes[0]);
		(void)close(pdes[1]);
		goto pfree;
		/* NOTREACHED */
	case 0:				/* child */
		if (*type == 'r') {
			if (pdes[1] != STDOUT_FILENO) {
				dup2(pdes[1], STDOUT_FILENO);
				(void)close(pdes[1]);
			}
			dup2(STDOUT_FILENO, STDERR_FILENO); /* stderr too! */
			(void)close(pdes[0]);
		} else {
			if (pdes[0] != STDIN_FILENO) {
				dup2(pdes[0], STDIN_FILENO);
				(void)close(pdes[0]);
			}
			(void)close(pdes[1]);
		}
#ifdef INTERNAL_LS
		if (strcmp(gargv[0], _PATH_LS) == 0) {
#ifdef LINUX
			optind = optopt = 1;
#else /* BSD source */
			/* Reset getopt for ls_main() */
			optreset = optind = optopt = 1;
#endif /* BSD source */
			/* Close syslogging to remove pwd.db missing msgs */
			closelog();
			/* Trigger to sense new /etc/localtime after chroot */
			if (getenv("TZ") == NULL) {
				setenv("TZ", "", 0);
				tzset();
				unsetenv("TZ");
				tzset();
			}
			exit(ls_main(gargc, gargv));
		}
#endif
		execv(gargv[0], gargv);
		_exit(1);
	}
	/* parent; assume fdopen can't fail...  */
	if (*type == 'r') {
		iop = fdopen(pdes[0], type);
		(void)close(pdes[1]);
	} else {
		iop = fdopen(pdes[1], type);
		(void)close(pdes[0]);
	}
	pids[fileno(iop)] = pid;

pfree:	for (argc = 1; gargv[argc] != NULL; argc++)
		free(gargv[argc]);

	return (iop);
}

int
ftpd_pclose(FILE *iop)
{
	int fdes, omask, status;
	pid_t pid;

	/*
	 * pclose returns -1 if stream is not associated with a
	 * `popened' command, or, if already `pclosed'.
	 */
	if (pids == 0 || pids[fdes = fileno(iop)] == 0)
		return (-1);
	(void)fclose(iop);
	omask = sigblock(sigmask(SIGINT)|sigmask(SIGQUIT)|sigmask(SIGHUP));
	while ((pid = waitpid(pids[fdes], &status, 0)) < 0 && errno == EINTR)
		continue;
	(void)sigsetmask(omask);
	pids[fdes] = 0;
	if (pid < 0)
		return (pid);
	if (WIFEXITED(status))
		return (WEXITSTATUS(status));
	return (1);
}
