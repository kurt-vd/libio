default:	iotoggle iofollow ioprobe ioserver macbookd

LOCALVERSION	:= $(shell ./getlocalversion .)

PREFIX	= /usr/local
CFLAGS	= -Wall -g3 -O0
CPPFLAGS= -D_GNU_SOURCE
LDLIBS	= -levt -lllist -lm -lrt

-include config.mk
CPPFLAGS += -DLOCALVERSION=\"$(LOCALVERSION)\"

%: %.c
	$(CC) -o $@ -DNAME=\"$@\" $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS)

libio.a: libio.o led.o inputev.o netio.o virtual.o shared.o \
	applelight.o preset.o \
	motor.o \
	teleruptor.o
	ar crs $@ $^

iotoggle: libio.a

iofollow: libio.a

ioprobe: libio.a

ioserver: libio.a

macbookd: libio.a

clean:
	rm -f libio.a toggle iofollow ioprobe $(wildcard *.o)
