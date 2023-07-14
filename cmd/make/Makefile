BINDIR ?= /usr/local/bin

PROG=	bmake
MAN=	bmake.1 bsd.lib.mk.5 bsd.prog.mk.5

CFLAGS+= -I${.OBJDIR} -I${.CURDIR}
HOSTCFLAGS+= -I${.OBJDIR} -I${.CURDIR}
CDIAGFLAGS=-Wall -W -Wno-char-subscripts -Wstrict-prototypes -pedantic \
	-Wmissing-prototypes -Wdeclaration-after-statement -std=c99

SRCS=	arch.c buf.c cmd_exec.c compat.c cond.c dir.c direxpand.c dump.c \
	engine.c enginechoice.c error.c expandchildren.c \
	for.c init.c job.c lowparse.c main.c make.c memory.c ohash.c \
	parse.c parsevar.c reallocarray.c str.c stats.c suff.c targ.c \
	targequiv.c timestamp.c var.c varmodifiers.c varname.c

.include "${.CURDIR}/lst.lib/Makefile.inc"

CLEANFILES+=generate generate.o regress.o check

CLEANFILES+= varhashconsts.h condhashconsts.h nodehashconsts.h

# may need tweaking if you add variable synonyms or change the hash function
MAGICVARSLOTS=82
MAGICCONDSLOTS=65

varhashconsts.h: generate
	${.OBJDIR}/generate 1 ${MAGICVARSLOTS} >$@.tmp && mv $@.tmp $@

condhashconsts.h: generate
	${.OBJDIR}/generate 2 ${MAGICCONDSLOTS} >$@.tmp && mv $@.tmp $@

nodehashconsts.h: generate
	${.OBJDIR}/generate 3 0 >$@.tmp && mv $@.tmp $@

generate: generate.c stats.c memory.c ohash.c reallocarray.c ${DPADD}
	${HOSTCC} ${HOSTCFLAGS} ${LDSTATIC} -o ${.TARGET} ${.ALLSRC} ${LDFLAGS} ${LDADD}

CHECKOBJS = regress.o str.o memory.o buf.o

check: ${CHECKOBJS} ${DPADD}
	${CC} -o ${.TARGET} ${CFLAGS} ${CHECKOBJS} ${LDADD}

regress: check
	${.OBJDIR}/check

var.o: varhashconsts.h
cond.o: condhashconsts.h
targ.o parse.o: nodehashconsts.h

.PHONY:		regress

SUBDIR = mk

.include <bsd.prog.mk>
