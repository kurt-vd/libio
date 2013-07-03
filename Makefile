PROGS	= io
default: $(PROGS)

LOCALVERSION	:= $(shell ./getlocalversion .)

PREFIX	= /usr/local
CFLAGS	= -Wall -g3 -O0
CPPFLAGS= -D_GNU_SOURCE
LDFLAGS =
LDLIBS	= -levt -lllist -lm -lrt
STRIP	= strip

-include config.mk
CPPFLAGS += -DLOCALVERSION=\"$(LOCALVERSION)\"

%.o: %.c
	@echo " CC $<"
	@$(CC) -c -o $@ -DNAME=\"$*\" $(CPPFLAGS) $(CFLAGS) $<

libio.a: libio.o led.o inputev.o netio.o virtual.o shared.o \
	applelight.o preset.o \
	motor.o \
	teleruptor.o
	@echo " AR $@"
	@ar crs $@ $^

io: io.o iofollow.o ioserver.o ioprobe.o iotoggle.o \
	hadirect.o hamotor.o haspawn.o \
	suntellposition.o sunposition.o \
	macbookd.o \
	libio.a
	@echo " CC $@"
	@$(CC) -o $@ -DNAME=\"$@\" $(LDFLAGS) $^ $(LDLIBS)

# specific programs without libio
ifdef GPSLON
suntellposition.o: CPPFLAGS += -DDEFAULT_LON=$(GPSLON)
endif
ifdef GPSLAT
suntellposition.o: CPPFLAGS += -DDEFAULT_LAT=$(GPSLAT)
endif

clean:
	rm -f libio.a $(PROGS) $(wildcard *.o libllist/*.o libevt/*.o)

install: $(PROGS)
	install --strip-program=$(STRIP) -v -s $^ $(DESTDIR)$(PREFIX)/bin
