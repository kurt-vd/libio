PROGS	= iotoggle iofollow ioprobe ioserver macbookd
PROGS	+= hamotor
PROGS	+= suntellposition
default: $(PROGS)

LOCALVERSION	:= $(shell ./getlocalversion .)

PREFIX	= /usr/local
CFLAGS	= -Wall -g3 -O0
CPPFLAGS= -D_GNU_SOURCE
LDLIBS	= -levt -lllist -lm -lrt
STRIP	= strip

-include config.mk
CPPFLAGS += -DLOCALVERSION=\"$(LOCALVERSION)\"

%.o: %.c
	@echo " CC $<"
	@$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $<
%: %.c libio.a
	@echo " CC $@"
	@$(CC) -o $@ -DNAME=\"$@\" $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS)

libio.a: libio.o led.o inputev.o netio.o virtual.o shared.o \
	applelight.o preset.o \
	motor.o \
	teleruptor.o
	@echo " AR $@"
	@ar crs $@ $^

# specific programs without libio
suntellposition: LDLIBS	= -lm
suntellposition: suntellposition.c sunposition.o
	@echo " CC $@"
	@$(CC) -o $@ -DNAME=\"$@\" $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS)


clean:
	rm -f libio.a $(PROGS) $(wildcard *.o libllist/*.o libevt/*.o)

install: $(PROGS)
	install --strip-program=$(STRIP) -v -s $^ $(DESTDIR)$(PREFIX)/bin
