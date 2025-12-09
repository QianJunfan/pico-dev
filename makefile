PREFIX?=/usr/X11R6
CFLAGS?=-Os -pedantic -Wall
PROGRAM = pico

all: $(PROGRAM)

$(PROGRAM):
	$(CC) $(CFLAGS) -I$(PREFIX)/include $(PROGRAM).c -L$(PREFIX)/lib -lX11 -o $(PROGRAM)

test: $(PROGRAM)
	@echo "--- Starting Xephyr server (800x600) ---"
	# Start Xephyr in the background (&) and capture its PID
	@Xephyr :1 -screen 800x600 & \
	XEPHYR_PID=$$! ; \
	sleep 1 ; \
	echo "--- Running $(PROGRAM) on Display :1. Press Ctrl+C to exit. ---" ; \
	# Run the Window Manager in the foreground
	DISPLAY=:1 ./$(PROGRAM) ; \
	# Once ./pico exits, clean up the Xephyr process
	echo "--- $(PROGRAM) exited. Shutting down Xephyr (PID: $$XEPHYR_PID) ---" ; \
	kill $$XEPHYR_PID

.PHONY: all clean test

clean:
	rm -f $(PROGRAM) tinywm