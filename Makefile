CFLAGS	+=	-Wall -g -O3 -pg
LDLIBS	+=	-lform -lcurses -lm -pg

.ifdef WITH_OUTRIGGER
  CFLAGS += -DWITH_OUTRIGGER -I../openham/outrigger/ -L../openham/outrigger/build/
  LDLIBS += -loutrigger -lpthread
.endif

OBJS = bsdtty.o fsk_demod.o ui.o

bsdtty: ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} ${.ALLSRC} ${LDLIBS} -o ${.TARGET}

clean:
	rm -f bsdtty ${OBJS}
