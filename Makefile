CFLAGS	+=	-Wall -g -O3
LDLIBS	+=	-lform -lcurses -lm

.ifdef DEBUG
  CFLAGS += -g -O0
.else
  CFLAGS += -O3
.endif

.ifndef WITHOUT_OUTRIGGER
  CFLAGS += -DWITH_OUTRIGGER -Ioutrigger/ -pthread
  LDFLAGS += -Lor-lib
  LDLIBS += -loutrigger -pthread
.endif

OBJS = bsdtty.o fsk_demod.o ui.o fldigi_xmlrpc.o

.ifndef WITHOUT_OUTRIGGER
bsdtty: or-lib/liboutrigger.a
.endif

bsdtty: ${OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} ${.ALLSRC} ${LDLIBS} -o ${.TARGET}

or-lib/:
	mkdir ${.TARGET}

or-lib/liboutrigger.a: or-lib/ .EXEC
	cd or-lib && cmake ../outrigger
	${MAKE} -C or-lib

clean:
	rm -f bsdtty ${OBJS}
	rm -rf or-lib
