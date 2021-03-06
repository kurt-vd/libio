PROGS	= io
default: $(PROGS)

LOCALVERSION	:= $(shell git describe --tags --always --dirty)

PREFIX	= /usr/local
CFLAGS	= -Wall -g3 -O0
CPPFLAGS= -D_GNU_SOURCE
LDFLAGS =
LDLIBS	= -lm -lrt
STRIP	= strip

-include config.mk
CPPFLAGS += -DLOCALVERSION=\"$(LOCALVERSION)\"

%.o: %.c
	@echo " CC $<"
	@$(CC) -c -o $@ -DNAME=\"$*\" $(CPPFLAGS) $(CFLAGS) $<

libio.a: libio.o led.o inputev.o netio.o sysfspar.o \
	virtual.o shared.o \
	consts.o longdetection.o \
	resc.o \
	cpuload.o \
	applelight.o \
	motor.o \
	teleruptor.o \
	battery.o \
	lib/libt.o lib/libe.o
	@echo " AR $@"
	@ar crs $@ $^

io: io.o iofollow.o ioserver.o ioprobe.o iotoggle.o \
	iotrace.o \
	resd.o \
	hadirect.o hamotor.o hasingletouch.o haspawn.o \
	ha2addons.o \
	suntellposition.o sunposition.o \
	macbookd.o \
	info.o \
	libio.a
	@echo " CC $@"
	@$(CC) -o $@ -DNAME=\"$@\" $(LDFLAGS) $^ $(LDLIBS)

clean:
	rm -f libio.a $(PROGS) $(wildcard *.o lib/*.o)

install: $(PROGS)
	install --strip-program=$(STRIP) -v -s $^ $(DESTDIR)$(PREFIX)/bin
	install -m 0644 -v ha2.conf $(DESTDIR)$(PREFIX)/share/libio
