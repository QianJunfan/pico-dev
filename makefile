PREFIX?=/usr/X11R6
CFLAGS?=-Os -pedantic -Wall

all:
	$(CC) $(CFLAGS) -I$(PREFIX)/include pico.c -L$(PREFIX)/lib -lX11 -o pico

clean:
	rm -f tinywm
