/* @(#)pathnames.h.in	8.4 (Berkeley) 6/26/96 */
/* $FreeBSD: head/usr.bin/vi/pathnames.h 114763 2003-05-05 22:49:23Z obrien $ */

/* Read standard system paths first. */
#include <paths.h>

#ifndef	_PATH_BSHELL
#define	_PATH_BSHELL	"/bin/sh"
#endif

#ifndef	_PATH_EXRC
#define	_PATH_EXRC	".exrc"
#endif

#ifndef	_PATH_MSGCAT
#define _PATH_MSGCAT    "/usr/local/share/vi/catalog/"
#endif

#ifndef	_PATH_NEXRC
#define	_PATH_NEXRC	".nexrc"
#endif

#ifndef	_PATH_PRESERVE
#define	_PATH_PRESERVE	"/var/tmp/vi.recover"
#endif

#ifndef _PATH_SYSV_PTY
#define	_PATH_SYSV_PTY	"/dev/ptmx"
#endif

#ifndef	_PATH_SENDMAIL
#define	_PATH_SENDMAIL	"/usr/sbin/sendmail"
#endif

#ifndef	_PATH_SYSEXRC
#define	_PATH_SYSEXRC	"/etc/vi.exrc"
#endif

#ifndef	_PATH_TAGS
#define	_PATH_TAGS	"tags"
#endif

#ifndef	_PATH_TMP
#define	_PATH_TMP	"/tmp"
#endif

#ifndef	_PATH_TTY
#define	_PATH_TTY	"/dev/tty"
#endif
