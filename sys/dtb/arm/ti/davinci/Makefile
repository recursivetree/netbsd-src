#	$NetBSD: Makefile,v 1.2 2026/01/19 05:25:22 skrll Exp $

DTSSUBDIR=	ti/davinci

.if !empty(MACHINE_ARCH:Mearmv5*)
DTSMAKEVARS=	CONFIG_ARCH_DAVINCI=y
.endif

DTSFILESCMD=	${MAKE} -C ${ARCHDTSDIR}/${DTSSUBDIR} ${DTSMAKEVARS} -v dtb-y
DTS=		${DTSFILESCMD:sh}

.include <bsd.dtb.mk>
