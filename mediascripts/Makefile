# Release:  (HEAD, Release-7, master) b3db3a1

BINDIR = /usr/local/bin
MAN1DIR = /usr/local/share/man/man1

.PHONY: all clean depend install

all clean depend:

install:
	install -c -m 555 -o root -g wheel dvd $(DESTDIR)$(BINDIR)/dvd
	install -c -m 555 -o root -g wheel vol $(DESTDIR)$(BINDIR)/vol
	install -c -m 444 -o root -g wheel dvd.1 $(DESTDIR)$(MAN1DIR)/dvd.1
	install -c -m 444 -o root -g wheel vol.1 $(DESTDIR)$(MAN1DIR)/vol.1
