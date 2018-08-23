LOCALBASE?=	/usr/local
DESTDIR?=	${LOCALBASE}
BINDIR?=	/bin
MANDIR?=	/man/man

PROG=	bsdtty
LDADD=	-lform -lcurses -lm -lpthread
SRCS=	bsdtty.c fldigi_xmlrpc.c fsk_demod.c ui.c afsk_send.c baudot.c \
	rigctl.c fsk_send.c
DPADD=	${LIBCURSES} ${LIBFORM} $(LIBM}

.include <bsd.prog.mk>
