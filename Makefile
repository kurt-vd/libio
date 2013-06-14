PROGS	= iotoggle iofollow ioprobe ioserver macbookd
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

clean:
	rm -f libio.a $(PROGS) $(wildcard *.o)

install: $(PROGS)
	install --strip-program=$(STRIP) -v -s $^ $(DESTDIR)$(PREFIX)/bin
