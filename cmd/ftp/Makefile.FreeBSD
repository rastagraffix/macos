# Makefile for the root of the source tree

.include "./Makefile.inc"

# Install destination
DESTDIR=/usr/local
# Relative to ${DESTDIR}
MANDIR=/man/man
DOCDIR=/share/doc
# end of dest variables

INSTALL= install -g wheel -o root
INS_BIN= -c -p -s -m 555
INS_DOC= -p -m 444
INS_SCRIPT= -p -m 555
.if defined(BSDTYPE) && ( ${BSDTYPE} == "NetBSD" )
INS_DOC+= -c
INS_SCRIPT+= -c
.else
INS_DOC+= -C
INS_SCRIPT+= -C
.endif
LN= ln -s -f
MKDIR= mkdir -m 755

SUBDIR= port \
	ftpd \
	ftp

install-prep: /dev/null
.if defined(BSDTYPE) && ( ${BSDTYPE} == "NetBSD" )
	$(MKDIR) -p ${DESTDIR}
	$(MKDIR) -p ${DESTDIR}/bin
	$(MKDIR) -p ${DESTDIR}/libexec
	$(MKDIR) -p ${DESTDIR}/share
	$(MKDIR) -p ${DESTDIR}/share/doc
	$(MKDIR) -p ${DESTDIR}/man
	$(MKDIR) -p ${DESTDIR}/man/man1
	$(MKDIR) -p ${DESTDIR}/man/man5
	$(MKDIR) -p ${DESTDIR}/man/man8
.endif

install-server: ftpd
	$(INSTALL) ${INS_BIN} ftpd/ftpd ${DESTDIR}/libexec/ftpd
	$(INSTALL) ${INS_DOC} ftpd/ftpchroot.5.gz ${DESTDIR}${MANDIR}5/ftpchroot.5.gz
	$(LN) ${DESTDIR}${MANDIR}5/ftpchroot.5.gz ${DESTDIR}${MANDIR}5/ftpchroot-ssl.5.gz
	$(INSTALL) ${INS_DOC} ftpd/xferlog.5.gz ${DESTDIR}${MANDIR}5/xferlog.5.gz
	$(INSTALL) ${INS_DOC} ftpd/ftpd.8.gz ${DESTDIR}${MANDIR}8/ftpd.8.gz
	$(LN) ftpd.8.gz ${DESTDIR}${MANDIR}8/ftpd-ssl.8.gz

install-client: ftp
	$(INSTALL) ${INS_BIN} ftp/ftps ${DESTDIR}/bin/ftps
	$(INSTALL) ${INS_DOC} ftp/ftps.1.gz ${DESTDIR}${MANDIR}1/ftps.1.gz

install-doc: /dev/null
	$(MKDIR) ${DESTDIR}${DOCDIR}/bsdftpd-ssl
	$(INSTALL) ${INS_DOC} COPYRIGHT ${DESTDIR}${DOCDIR}/bsdftpd-ssl/COPYRIGHT
	$(INSTALL) ${INS_DOC} README ${DESTDIR}${DOCDIR}/bsdftpd-ssl/README
	$(INSTALL) ${INS_DOC} INSTALL ${DESTDIR}${DOCDIR}/bsdftpd-ssl/INSTALL
	$(INSTALL) ${INS_DOC} ChangeLog ${DESTDIR}${DOCDIR}/bsdftpd-ssl/ChangeLog
	$(MKDIR) ${DESTDIR}${DOCDIR}/bsdftpd-ssl/docs
	$(INSTALL) ${INS_DOC} docs/README ${DESTDIR}${DOCDIR}/bsdftpd-ssl/docs/README
	$(INSTALL) ${INS_DOC} docs/cert-basics.txt ${DESTDIR}${DOCDIR}/bsdftpd-ssl/docs/cert-basics.txt
	$(INSTALL) ${INS_DOC} docs/cert-howto.txt ${DESTDIR}${DOCDIR}/bsdftpd-ssl/docs/cert-howto.txt
	$(INSTALL) ${INS_DOC} docs/ciphers.txt ${DESTDIR}${DOCDIR}/bsdftpd-ssl/docs/ciphers.txt
	$(INSTALL) ${INS_DOC} docs/licenses ${DESTDIR}${DOCDIR}/bsdftpd-ssl/docs/licenses
	$(INSTALL) ${INS_DOC} docs/standards.txt ${DESTDIR}${DOCDIR}/bsdftpd-ssl/docs/standards.txt
	$(INSTALL) ${INS_DOC} docs/verify.txt ${DESTDIR}${DOCDIR}/bsdftpd-ssl/docs/verify.txt
	$(INSTALL) ${INS_DOC} docs/x509_auth.txt ${DESTDIR}${DOCDIR}/bsdftpd-ssl/docs/x509_auth.txt
	$(MKDIR) ${DESTDIR}${DOCDIR}/bsdftpd-ssl/cert
	$(INSTALL) ${INS_SCRIPT} cert/cert-dummy.sh ${DESTDIR}${DOCDIR}/bsdftpd-ssl/cert/cert-dummy.sh
	$(INSTALL) ${INS_SCRIPT} cert/cert-nopass.sh ${DESTDIR}${DOCDIR}/bsdftpd-ssl/cert/cert-nopass.sh
	$(INSTALL) ${INS_SCRIPT} cert/cert-pass.sh ${DESTDIR}${DOCDIR}/bsdftpd-ssl/cert/cert-pass.sh
	$(INSTALL) ${INS_SCRIPT} cert/xCA.sh ${DESTDIR}${DOCDIR}/bsdftpd-ssl/cert/xCA.sh

install: install-prep install-server install-client install-doc
	@echo "**************************************************"
	@echo "* Do not forget to make the certificate for ftpd *"
	@echo "**************************************************"

.if defined(BSDTYPE) && ( ${BSDTYPE} != "NetBSD" || !make(install) )
.include <bsd.subdir.mk>
.endif
