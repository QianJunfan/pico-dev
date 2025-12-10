#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <X11/Xproto.h>

struct xcli {
        char name[256];
        Window win;
        Monitor *mon;
        unsigned int minw, minh;
        float mina, maxa;
        bool is_neverfocus;
};