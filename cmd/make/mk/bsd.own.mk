#	$OpenBSD: bsd.own.mk,v 1.211 2021/08/21 03:00:02 gkoehler Exp $
#	$NetBSD: bsd.own.mk,v 1.24 1996/04/13 02:08:09 thorpej Exp $

# Host-specific overrides
.if defined(MAKECONF) && exists(${MAKECONF})
.include "${MAKECONF}"
.elif exists(/etc/mk.conf)
.include "/etc/mk.conf"
.endif

# Set `WARNINGS' to `yes' to add appropriate warnings to each compilation
WARNINGS?=	no

# where the system object and source trees are kept; can be configurable
# by the user in case they want them in ~/foosrc and ~/fooobj, for example
BSDSRCDIR?=	/usr/src
BSDOBJDIR?=	/usr/obj

BINGRP?=	wheel
BINOWN?=	root
BINMODE?=	555
NONBINMODE?=	444
DIRMODE?=	755

SHAREDIR?=	/usr/local/share
SHAREGRP?=	bin
SHAREOWN?=	root
SHAREMODE?=	${NONBINMODE}

MANDIR?=	/usr/local/share/man/man
MANGRP?=	bin
MANOWN?=	root
MANMODE?=	${NONBINMODE}

LIBDIR?=	/usr/local/lib
LIBGRP?=	${BINGRP}
LIBOWN?=	${BINOWN}
LIBMODE?=	${NONBINMODE}

DOCDIR?=	/usr/local/share/doc
DOCGRP?=	bin
DOCOWN?=	root
DOCMODE?=	${NONBINMODE}

LOCALEDIR?=	/usr/local/share/locale
LOCALEGRP?=	wheel
LOCALEOWN?=	root
LOCALEMODE?=	${NONBINMODE}

.if !defined(CDIAGFLAGS)
CDIAGFLAGS=	-Wall -Wpointer-arith -Wuninitialized -Wstrict-prototypes
CDIAGFLAGS+=	-Wmissing-prototypes -Wunused -Wsign-compare
CDIAGFLAGS+=	-Wshadow
.endif

# Shared files for system gnu configure, not used yet
GNUSYSTEM_AUX_DIR?=${BSDSRCDIR}/share/gnu

INSTALL_COPY?=	-c
.ifndef DEBUG
INSTALL_STRIP?=	-s
.endif

STATIC?=	-static

# Define SYS_INCLUDE to indicate whether you want symbolic links to the system
# source (``symlinks''), or a separate copy (``copies''); (latter useful
# in environments where it's not possible to keep /sys publicly readable)
#SYS_INCLUDE= 	symlinks

# pic relocation flags.
.if ${MACHINE_ARCH} == "alpha" || ${MACHINE_ARCH} == "powerpc" || \
    ${MACHINE_ARCH} == "sparc64"
PICFLAG?=-fPIC
.else
PICFLAG?=-fpic
.endif

# don't try to generate PROFILED versions of libraries on machines
# which don't support profiling.
.if 0
NOPROFILE=
.endif

BUILDUSER?= build
WOBJGROUP?= wobj
WOBJUMASK?= 007

BSD_OWN_MK=Done

.PHONY: spell clean cleandir obj manpages print all \
	depend beforedepend afterdepend cleandepend subdirdepend \
	all cleanman includes \
	beforeinstall realinstall maninstall afterinstall install
