#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <X11/XF86keysym.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define SUPER_MASK Mod4Mask

static const unsigned int ignore_mods[] = {
    0,
    LockMask,
    Mod2Mask,
    Mod3Mask,
    Mod5Mask,
    LockMask | Mod2Mask,
    LockMask | Mod3Mask,
    LockMask | Mod5Mask,
    Mod2Mask | Mod3Mask,
    Mod2Mask | Mod5Mask,
    Mod3Mask | Mod5Mask,
    LockMask | Mod2Mask | Mod3Mask,
    LockMask | Mod2Mask | Mod5Mask,
    Mod2Mask | Mod3Mask | Mod5Mask,
    LockMask | Mod2Mask | Mod3Mask | Mod5Mask
};

void launch_xterm() {
    if (fork() == 0) {
        setsid();
        execlp("xterm", "xterm", NULL);
        perror("Failed to launch xterm");
        _exit(EXIT_FAILURE);
    }
}

void grab_input(Display *dpy)
{
    unsigned int i;
    KeyCode keycode_f1 = XKeysymToKeycode(dpy, XStringToKeysym("F1"));
    
    for (i = 0; i < sizeof(ignore_mods) / sizeof(ignore_mods[0]); i++) {
        
        unsigned int mod = SUPER_MASK | ignore_mods[i];
        
        if (keycode_f1) {
             XGrabKey(dpy, keycode_f1, mod,
                 DefaultRootWindow(dpy), True, GrabModeAsync, GrabModeAsync);
        }

        XGrabButton(dpy, 1, mod, DefaultRootWindow(dpy), True,
                ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
        
        XGrabButton(dpy, 3, mod, DefaultRootWindow(dpy), True,
                ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
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
    
    grab_input(dpy);


    start.subwindow = None;
    for(;;)
    {
        XNextEvent(dpy, &ev);
        
        
        if(ev.type == KeyPress)
        {
            
            KeySym keysym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);

            if (keysym == XK_F1 && (ev.xkey.state & SUPER_MASK)) {
                 if(ev.xkey.subwindow != None)
                    XRaiseWindow(dpy, ev.xkey.subwindow);
            }
        }
        
        
        else if(ev.type == ButtonPress && ev.xbutton.subwindow != None)
        {
            
            if (ev.xbutton.state & SUPER_MASK)
            {
                XGetWindowAttributes(dpy, ev.xbutton.subwindow, &attr);
                start = ev.xbutton;
            }
        }
        
        
        else if(ev.type == MotionNotify && start.subwindow != None)
        {
            int xdiff = ev.xbutton.x_root - start.x_root;
            int ydiff = ev.xbutton.y_root - start.x_root;
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