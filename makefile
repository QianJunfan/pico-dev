PREFIX?=/usr/X11R6
# 添加 -std=c99 或更高版本，并链接 Xlib (-lX11)
# XKB 扩展依赖 Xrandr，所以经常需要 -lXrandr
CFLAGS?=-Os -pedantic -Wall -std=c99
PROGRAM = minimalwm
SRC = test.c

all: $(PROGRAM)

# 编译规则：使用 $(SRC) 作为源文件
$(PROGRAM): $(SRC)
	$(CC) $(CFLAGS) -I$(PREFIX)/include $(SRC) -L$(PREFIX)/lib -lX11 -lXrandr -o $(PROGRAM)

# 测试规则：确保目标是您编译的程序名
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
