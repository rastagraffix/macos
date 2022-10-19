#	$OpenBSD: Makefile,v 1.64 2020/01/13 15:41:53 espie Exp $

PROG=	bmake
CDIAGFLAGS=-Wall -W -Wno-char-subscripts -Wstrict-prototypes -pedantic \
	-Wmissing-prototypes -Wdeclaration-after-statement -std=c99

#CDEFS+=-DHAS_STATS

CFLAGS+=${CDEFS} -I. -Ilst.lib

SRCS=	arch.c buf.c cmd_exec.c compat.c cond.c dir.c direxpand.c dump.c \
	engine.c enginechoice.c error.c expandchildren.c \
	for.c init.c job.c lowparse.c main.c make.c memory.c ohash.c parse.c \
	parsevar.c reallocarray.c str.c stats.c suff.c targ.c targequiv.c \
	timestamp.c var.c varmodifiers.c varname.c

LSTSRCS = lst.lib/lstAddNew.c lst.lib/lstAppend.c lst.lib/lstConcat.c \
	lst.lib/lstConcatDestroy.c lst.lib/lstDeQueue.c lst.lib/lstDestroy.c \
	lst.lib/lstDupl.c lst.lib/lstFindFrom.c lst.lib/lstForEachFrom.c \
	lst.lib/lstInsert.c lst.lib/lstMember.c \
	lst.lib/lstRemove.c lst.lib/lstReplace.c lst.lib/lstRequeue.c \
	lst.lib/lstSucc.c

CLEANFILES+=generate generate.o regress.o check

CLEANFILES+= varhashconsts.h condhashconsts.h nodehashconsts.h

# may need tweaking if you add variable synonyms or change the hash function
MAGICVARSLOTS=82
MAGICCONDSLOTS=65

HEADERS= varhashconsts.h condhashconsts.h nodehashconsts.h
OBJS= ${SRCS:.c=.o} ${LSTSRCS:.c=.o}

${PROG}: ${OBJS} ${HEADERS}
	${CC} ${CFLAGS} -o ${PROG} ${OBJS}

all: ${PROG}

varhashconsts.h: generate
	./generate 1 ${MAGICVARSLOTS} >$@.tmp && mv $@.tmp $@

condhashconsts.h: generate
	./generate 2 ${MAGICCONDSLOTS} >$@.tmp && mv $@.tmp $@

nodehashconsts.h: generate
	./generate 3 0 >$@.tmp && mv $@.tmp $@

generate: generate.o stats.o memory.o ohash.o reallocarray.o ${DPADD}
	${CC} ${CFLAGS} -o generate generate.o stats.o \
		memory.o ohash.o reallocarray.o

CHECKOBJS = regress.o str.o memory.o buf.o

check: ${CHECKOBJS} ${DPADD}
	${CC} -o ${.TARGET} ${CFLAGS} ${CHECKOBJS} ${LDADD}

regress: check
	./check

var.o: varhashconsts.h
cond.o: condhashconsts.h
targ.o parse.o: nodehashconsts.h

SUDO = sudo
# Perform a minimal install of the bootstrap build. This is just
# enough to allow a full rebuild using the bootstrapped bmake.
install:
	$(SUDO) install -d -m 775 -o root -g wheel /usr/local/bin
	$(SUDO) install -d -m 775 -o root -g wheel /usr/local/share/mk
	$(SUDO) install -m 555 -o root -g wheel bmake /usr/local/bin/bmake
	$(SUDO) cp mk/*.mk /usr/local/share/mk

clean:
	rm -f ${PROG} ${OBJS} ${CLEANFILES}

.PHONY:		regress clean install
