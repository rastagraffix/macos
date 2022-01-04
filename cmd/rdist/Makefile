# $Header$
#	$OpenBSD: Makefile,v 1.21 2017/10/17 19:31:56 naddy Exp $

BINDIR=		/usr/local/bin
MAN1DIR=	/usr/local/share/man/man1
CFLAGS+=	-I.
OBJS=	gram.o child.o client.o common.o distopt.o docmd.o expand.o \
	isexec.o lookup.o message.o rcmdsh.o rdist.o
WARNINGS=yes

all: rdist

rdist: $(OBJS)
	$(CC) $(LDFLAGS) -o rdist $(OBJS)

.c.o:
	$(CC) $(CFLAGS) -c $<

gram.c gram.h: gram.y
	yacc -d -o gram.c gram.y

install:
	install -m 555 rdist $(BINDIR)/rdist
	install -m 444 rdist.1 $(MAN1DIR)/rdist.1

clean:
	rm -f $(OBJS)
