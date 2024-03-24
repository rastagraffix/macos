# $FreeBSD: /ctm/FreeBSD/anoncvs/cvs/src/usr.bin/ftp/Makefile,v 1.11.2.1 2000/05/16 04:25:19 cpiazza Exp $
#	$NetBSD: Makefile,v 1.15 1997/10/18 15:31:20 lukem Exp $
#	from: @(#)Makefile	8.2 (Berkeley) 4/3/94

.include "../../Makefile.inc"

#CFLAGS= -g
#DEBUG= -g

# Clang is noisy ...
CFLAGS+= -Wno-unused-const-variable
CFLAGS+= -Wno-deprecated-non-prototype
CFLAGS+= -Wno-implicit-function-declaration

## IPv6 support
CFLAGS+=-DINET6

## Uncomment the following to provide defaults for gate-ftp operation
#CFLAGS+=-DGATE_SERVER=\"ftp-gw.host\" # -DGATE_PORT=21

### TLS/SSL support

# This requires a 3rd party installation of OpenSSL; MacOS
# does not ship with one. Point the next two macros at your
# local installation to enable it in the build.
OPENSSLINC= /opt/local/include
OPENSSLLIB= /opt/local/lib

.if exists(${OPENSSLINC}/openssl/crypto.h) && \
    exists(${OPENSSLLIB}/libcrypto.a) && \
    exists(${OPENSSLLIB}/libssl.a)
CFLAGS+=-DUSE_SSL
LDADD+=	-L${OPENSSLLIB} -lssl -lcrypto
.PATH:	${.CURDIR}/../ssl
SRCS+= sslapp.c ssl_port.c ssl_port_ftps.c
CFLAGS+= -I${OPENSSLINC} -I../ssl
.endif

PROG=	ftp
SRCS+=	cmds.c cmdtab.c complete.c domacro.c fetch.c ftp.c main.c ruserpass.c \
	util.c

CFLAGS+= -I../port -Wall -fcommon

LDADD+=	-ledit
LDADD+=	-lncurses
DPADD+=	${LIBEDIT} ${LIBNCURSES}

.include <bsd.prog.mk>
