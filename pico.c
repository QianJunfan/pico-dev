#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MOD_MASK Mod4Mask
#define MOUSE_MASK (ButtonPressMask|ButtonReleaseMask|PointerMotionMask)
#define CLEANMASK(mask) (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))

static Display *dpy;
static Window root;
static int screen;
static unsigned int numlockmask = 0;

void spawn(const char *cmd) {
    if (fork() == 0) {
        if (dpy) close(ConnectionNumber(dpy));
        setsid();
        execlp(cmd, cmd, NULL);
        _exit(0);
    }
}

int xerror(Display *dpy, XErrorEvent *er) {
    return 0;
}

void updatenumlockmask(void) {
    unsigned int i, j;
    XModifierKeymap *modmap;
    numlockmask = 0;
    modmap = XGetModifierMapping(dpy);
    for (i = 0; i < 8; i++)
        for (j = 0; j < modmap->max_keypermod; j++)
            if (modmap->modifiermap[i * modmap->max_keypermod + j] == XKeysymToKeycode(dpy, XK_Num_Lock))
                numlockmask = (1 << i);
    XFreeModifiermap(modmap);
}

void grabkeys(void) {
    updatenumlockmask();
    unsigned int j;
    unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    KeyCode k1 = XKeysymToKeycode(dpy, XK_F1);
    KeyCode k2 = XKeysymToKeycode(dpy, XK_F2);
    KeyCode k3 = XKeysymToKeycode(dpy, XK_q);

    XUngrabKey(dpy, AnyKey, AnyModifier, root);

    for (j = 0; j < 4; j++) {
        if (k1) XGrabKey(dpy, k1, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
        if (k2) XGrabKey(dpy, k2, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
        if (k3) XGrabKey(dpy, k3, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
        XGrabButton(dpy, 1, MOD_MASK | modifiers[j], root, True, MOUSE_MASK, GrabModeAsync, GrabModeAsync, None, None);
        XGrabButton(dpy, 3, MOD_MASK | modifiers[j], root, True, MOUSE_MASK, GrabModeAsync, GrabModeAsync, None, None);
    }
}

void maprequest(XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;
    XMapWindow(dpy, ev->window);
    XSetInputFocus(dpy, ev->window, RevertToParent, CurrentTime);
}

void configurerequest(XEvent *e) {
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges wc;

    wc.x = ev->x;
    wc.y = ev->y;
    wc.width = ev->width;
    wc.height = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
    
    XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
}

void (*handler[LASTEvent]) (XEvent *) = {
    [KeyPress] = NULL,
    [MapRequest] = maprequest,
    [ConfigureRequest] = configurerequest
};

int main(void) {
    XEvent ev;
    XWindowAttributes attr;
    XButtonEvent start = {0};

    if (!(dpy = XOpenDisplay(NULL))) return 1;

    XSetErrorHandler(xerror);
    
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
    sa.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &sa, NULL);

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);
    
    grabkeys();
    spawn("xterm");

    for (;;) {
        XNextEvent(dpy, &ev);
        if (handler[ev.type]) {
            handler[ev.type](&ev);
            continue;
        }

        if (ev.type == KeyPress) {
            KeySym keysym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
            if (CLEANMASK(ev.xkey.state) == MOD_MASK) {
                if (keysym == XK_F1) {
                    spawn("xterm");
                } else if (keysym == XK_F2) {
                    if (ev.xkey.subwindow != None) {
                         XDestroyWindow(dpy, ev.xkey.subwindow);
                    } else {
                        Window w;
                        int revert;
                        XGetInputFocus(dpy, &w, &revert);
                        if(w != root && w != 0) XDestroyWindow(dpy, w);
                    }
                } else if (keysym == XK_q) { 
                    XCloseDisplay(dpy);
                    return 0;
                }
            }
        }
        else if (ev.type == ButtonPress && ev.xbutton.subwindow != None) {
            XGetWindowAttributes(dpy, ev.xbutton.subwindow, &attr);
            start = ev.xbutton;
            XRaiseWindow(dpy, start.subwindow);
            XSetInputFocus(dpy, start.subwindow, RevertToParent, CurrentTime);
        }
        else if (ev.type == MotionNotify && start.subwindow != None) {
            int xdiff = ev.xmotion.x_root - start.x_root;
            int ydiff = ev.xmotion.y_root - start.y_root;
            
            if (start.button == 1) {
                XMoveWindow(dpy, start.subwindow, attr.x + xdiff, attr.y + ydiff);
            } else if (start.button == 3) {
                XResizeWindow(dpy, start.subwindow, 
                    MAX(1, attr.width + xdiff), 
                    MAX(1, attr.height + ydiff));
            }
        }
        else if (ev.type == ButtonRelease) {
            start.subwindow = None;
        }
    }
}