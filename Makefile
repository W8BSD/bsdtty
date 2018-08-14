LOCALBASE?=	/usr/local
DESTDIR?=	${LOCALBASE}
BINDIR?=	/bin
MANDIR?=	/man/man

PROG=	bsdtty
LDADD=	-lform -lcurses -lm
SRCS=	bsdtty.c fldigi_xmlrpc.c fsk_demod.c ui.c
DPADD=	${LIBCURSES} ${LIBFORM} $(LIBM}

.include <bsd.prog.mk>
