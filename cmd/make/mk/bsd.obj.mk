#	$OpenBSD: bsd.obj.mk,v 1.19 2017/01/24 03:22:13 tb Exp $
#	$NetBSD: bsd.obj.mk,v 1.9 1996/04/10 21:08:05 thorpej Exp $

.if !target(obj)
.  if defined(NOOBJ)
obj:
.  else

.  if defined(MAKEOBJDIR)
__objdir=	${MAKEOBJDIR}
.  else
__objdir=	obj
.  endif

_SUBDIRUSE:

obj! _SUBDIRUSE
	@cd ${.CURDIR}; \
	[[ -z $$MKDIRS ]] && MKDIRS="mkdir -p"; \
		dest=${__objdir} ; \
		if [[ ! -d ${__objdir} ]]; then \
			echo "making $$dest" ; \
			$$MKDIRS $$dest; \
		fi ;
.  endif
.endif

.include <bsd.own.mk>
