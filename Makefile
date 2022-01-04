SUBDIRS=	cmd

default: all

all install clean:
	@for i in $(SUBDIRS); do (echo "===>" $$i; cd $$i && $(MAKE) $@); done

dirs:
	mkdir -p /usr/local/bin
	mkdir -p /usr/local/share/man/man1
