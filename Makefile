default:	toggle iofollow ioprobe macbookd

LOCALVERSION	:= $(shell ./getlocalversion .)

PREFIX	= /usr/local
CFLAGS	= -Wall -g3 -O0
CPPFLAGS= -D_GNU_SOURCE
LDLIBS	= -levt -lllist -lm -lrt

-include config.mk
CPPFLAGS += -DLOCALVERSION=\"$(LOCALVERSION)\"

%: %.c
	$(CC) -o $@ -DNAME=\"$@\" $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS)

libio.a: libio.o led.o inputev.o netio.o \
	applelight.o preset.o
	ar crs $@ $^

toggle: libio.a

iofollow: libio.a

ioprobe: libio.a

macbookd: libio.a

clean:
	rm -f libio.a toggle iofollow ioprobe $(wildcard *.o)
