# Porting things
SUBDIRS= libbsdport libedit

all:
	@for i in $(SUBDIRS); \
	    do (cd $$i && echo $$i && $(MAKE) CC="$(CC)" SSLTOP="$(SSLTOP)" LDADD="$(LDADD)") || exit; \
	done

cleandir: clean

clean: /dev/null
	@for i in $(SUBDIRS); \
	do (cd $$i && echo $$i && $(MAKE) clean) || exit; \
	done
