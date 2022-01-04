SUBDIRS=	rdist telnet
BROKEN=		nvi

default: all

all install clean:
	@for i in $(SUBDIRS); do (echo "===>" $$i; cd $$i && $(MAKE) $@); done
