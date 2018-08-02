CFLAGS	+=	-Wall -g -O0
LDLIBS	+=	-lcurses -lm

OBJS = bsdtty.o fsk_demod.o ui.o

bsdtty: ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} ${.ALLSRC} ${LDLIBS} -o ${.TARGET}

clean:
	rm -f bsdtty ${OBJS}
