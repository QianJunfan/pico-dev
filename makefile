PREFIX?=/usr/X11R6
CFLAGS?=-Os -pedantic -Wall -std=c99
PROGRAM = pico
SRC = pico.c

all: $(PROGRAM)

$(PROGRAM): $(SRC)
	$(CC) $(CFLAGS) -I$(PREFIX)/include $(SRC) -L$(PREFIX)/lib -lX11 -lXrandr -o $(PROGRAM)

test: $(PROGRAM)
	@echo "--- Starting Xephyr server (800x600) ---"
	@Xephyr :1 -screen 800x600 & \
	XEPHYR_PID=$$! ; \
	sleep 1 ; \
	echo "--- Running $(PROGRAM) on Display :1. Press Ctrl+C to exit. ---" ; \
	DISPLAY=:1 ./$(PROGRAM) ; \
	echo "--- $(PROGRAM) exited. Shutting down Xephyr (PID: $$XEPHYR_PID) ---" ; \
	kill $$XEPHYR_PID

.PHONY: all clean test

clean:
	rm -f $(PROGRAM)
