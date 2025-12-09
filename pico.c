#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define ALT_MASK Mod1Mask
#define IGNORE_MODS (LockMask | Mod2Mask) 

void launch_xterm() {
    if (fork() == 0) {
        setsid();
        execlp("xterm", "xterm", NULL);
        perror("Failed to launch xterm");
        _exit(EXIT_FAILURE);
    }
}

int main(void)
{
    Display * dpy;
    XWindowAttributes attr;
    XButtonEvent start;
    XEvent ev;

    if(!(dpy = XOpenDisplay(NULL))) return 1;

    
    launch_xterm();
    
    XGrabKey(dpy, XKeysymToKeycode(dpy, XStringToKeysym("F1")), ALT_MASK,
            DefaultRootWindow(dpy), True, GrabModeAsync, GrabModeAsync);
    
    XGrabButton(dpy, 1, ALT_MASK, DefaultRootWindow(dpy), True,
            ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
    
    XGrabButton(dpy, 3, ALT_MASK, DefaultRootWindow(dpy), True,
            ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);

    start.subwindow = None;
    for(;;)
    {
        XNextEvent(dpy, &ev);
        
        
        if(ev.type == KeyPress)
        {
            
            unsigned int clean_state = ev.xkey.state & ~IGNORE_MODS;
            KeySym keysym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);

            
            if (keysym == XK_F1 && (clean_state == ALT_MASK)) {
                 if(ev.xkey.subwindow != None)
                    XRaiseWindow(dpy, ev.xkey.subwindow);
            }
        }
        
        
        else if(ev.type == ButtonPress && ev.xbutton.subwindow != None)
        {
            
            unsigned int clean_state = ev.xbutton.state & ~IGNORE_MODS;

            if (clean_state == ALT_MASK)
            {
                XGetWindowAttributes(dpy, ev.xbutton.subwindow, &attr);
                start = ev.xbutton;
            }
        }
        
        
        else if(ev.type == MotionNotify && start.subwindow != None)
        {
            int xdiff = ev.xbutton.x_root - start.x_root;
            int ydiff = ev.xbutton.y_root - start.y_root;
            XMoveResizeWindow(dpy, start.subwindow,
                attr.x + (start.button==1 ? xdiff : 0),
                attr.y + (start.button==1 ? ydiff : 0),
                MAX(1, attr.width + (start.button==3 ? xdiff : 0)),
                MAX(1, attr.height + (start.button==3 ? ydiff : 0)));
        }
        else if(ev.type == ButtonRelease)
            start.subwindow = None;
    }
}