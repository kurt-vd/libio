PROGS	= io
default: $(PROGS)

LOCALVERSION	:= $(shell ./getlocalversion .)

PREFIX	= /usr/local
CFLAGS	= -Wall -g3 -O0
CPPFLAGS= -D_GNU_SOURCE
LDFLAGS =
LDLIBS	= -levt -lllist -lm -lrt
STRIP	= strip

GPSLON	= NAN
GPSLAT	= NAN

-include config.mk
CPPFLAGS += -DLOCALVERSION=\"$(LOCALVERSION)\"

%.o: %.c
	@echo " CC $<"
	@$(CC) -c -o $@ -DNAME=\"$*\" $(CPPFLAGS) $(CFLAGS) $<

libio.a: libio.o led.o inputev.o netio.o virtual.o shared.o defaults.o \
	applelight.o preset.o \
	motor.o \
	teleruptor.o
	@echo " AR $@"
	@ar crs $@ $^

io: io.o iofollow.o ioserver.o ioprobe.o iotoggle.o \
	hadirect.o hamotor.o hasingletouch.o haspawn.o \
	ha2addons.o \
	suntellposition.o sunposition.o \
	macbookd.o \
	libio.a
	@echo " CC $@"
	@$(CC) -o $@ -DNAME=\"$@\" $(LDFLAGS) $^ $(LDLIBS)

# specific programs without libio
defaults.o: CPPFLAGS += -DGPSLON=$(GPSLON) -DGPSLAT=$(GPSLAT)

clean:
	rm -f libio.a $(PROGS) $(wildcard *.o libllist/*.o libevt/*.o)

install: $(PROGS)
	install --strip-program=$(STRIP) -v -s $^ $(DESTDIR)$(PREFIX)/bin
	install -v ha2.conf $(DESTDIR)$(PREFIX)/share/libio
	install -v ha2direct.sh $(DESTDIR)$(PREFIX)/libexec
	install -v ha2veluxg.sh $(DESTDIR)$(PREFIX)/libexec
