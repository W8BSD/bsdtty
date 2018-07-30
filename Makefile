CFLAGS	+=	-Wall -g
LDLIBS	+=	-lcurses -lm

OBJS = bsdtty.o

bsdtty: ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} ${.ALLSRC} ${LDLIBS} -o ${.TARGET}

clean:
	rm -f bsdtty ${OBJS}
