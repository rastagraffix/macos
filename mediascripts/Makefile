BINDIR	= /usr/local/bin
MANDIR	= /usr/local/man/man1

CMDS	= channel channelsurf dvd eyetv vol

.PHONY: all clean depend install

all clean depend:

install:
	@for cmd in $(CMDS); do \
		echo install -c -m 555 -o root -g wheel $$cmd $(DESTDIR)$(BINDIR)/$$cmd; \
		install -c -m 555 -o root -g wheel $$cmd $(DESTDIR)$(BINDIR)/$$cmd; \
		if test -f $$cmd.1 ; then \
			echo install -c -m 444 -o root -g wheel $$cmd.1 $(DESTDIR)$(MANDIR)/$$cmd.1; \
			install -c -m 444 -o root -g wheel $$cmd.1 $(DESTDIR)$(MANDIR)/$$cmd.1; \
		fi \
	done
