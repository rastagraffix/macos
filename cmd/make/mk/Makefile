#	$OpenBSD: Makefile,v 1.14 2015/10/26 10:43:42 bluhm Exp $
#	$NetBSD: Makefile,v 1.12 1995/10/22 00:45:53 christos Exp $

BINDIR ?= /usr/local/share

FILES=	bsd.README bsd.dep.mk bsd.lib.mk bsd.man.mk \
	bsd.obj.mk bsd.own.mk bsd.prog.mk \
	bsd.subdir.mk sys.mk
NOOBJ=	noobj

install:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 ${FILES} \
	    ${DESTDIR}${BINDIR}/mk

.include <bsd.prog.mk>
