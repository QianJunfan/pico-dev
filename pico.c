#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MOD_MASK Mod4Mask
#define MOUSE_MASK (ButtonPressMask|ButtonReleaseMask|PointerMotionMask)
#define CLEANMASK(mask) (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))

#define NUM_WORKSPACES 9
#define BAR_HEIGHT 20
#define BAR_COLOR 0x222222
#define BAR_FOCUS_COLOR 0x005577
#define BAR_TEXT_COLOR 0xeeeeee

typedef struct {
    Window win;
    int ws_id;
} Client;

typedef struct {
    int id;
    Client *clients[64];
    int count;
} Workspace;

static Display *dpy;
static Window root;
static int screen;
static int sw, sh;
static unsigned int numlockmask = 0;

static Workspace workspaces[NUM_WORKSPACES];
static int current_ws = 0;

static Window bar_win;
static GC gc;



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

void client_add(Window w) {
    Workspace *ws = &workspaces[current_ws];
    if (ws->count >= 64) return;

    Client *c = (Client *)malloc(sizeof(Client));
    c->win = w;
    c->ws_id = current_ws;
    ws->clients[ws->count++] = c;

    // 默认边框
    XSetWindowBorderWidth(dpy, w, 1);
    XSetWindowBorder(dpy, w, 0x000000); 

    // 提升焦点
    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);
    if (focused == root || focused == None || focused == bar_win) {
         XSetWindowBorder(dpy, w, 0xFF0000);
    } else {
        XSetWindowBorder(dpy, focused, 0x000000);
    }

    XRaiseWindow(dpy, w);
    XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
}


void draw_bar(void) {
    char buf[64];
    int x = 5;

    XSetForeground(dpy, gc, BAR_COLOR);
    XFillRectangle(dpy, bar_win, gc, 0, 0, sw, BAR_HEIGHT);

    for (int i = 0; i < NUM_WORKSPACES; i++) {
        if (i == current_ws) {
            XSetForeground(dpy, gc, BAR_FOCUS_COLOR);
            XFillRectangle(dpy, bar_win, gc, x - 2, 2, 14, BAR_HEIGHT - 4);
        }

        XSetForeground(dpy, gc, BAR_TEXT_COLOR);
        snprintf(buf, sizeof(buf), "%d", i + 1);
        XDrawString(dpy, bar_win, gc, x, 15, buf, 1);
        x += 20;
    }

    snprintf(buf, sizeof(buf), "Win: %d", workspaces[current_ws].count);
    XDrawString(dpy, bar_win, gc, sw - 100, 15, buf, strlen(buf));
}
void client_remove(Window w) {
    for (int k = 0; k < NUM_WORKSPACES; k++) {
        Workspace *ws = &workspaces[k];
        for (int i = 0; i < ws->count; i++) {
            if (ws->clients[i]->win == w) {
                free(ws->clients[i]);
                for (int j = i; j < ws->count - 1; j++) {
                    ws->clients[j] = ws->clients[j+1];
                }
                ws->count--;

                if (k == current_ws) {
                    if (ws->count > 0) {
                        Window new_focus_win = ws->clients[0]->win;
                        XSetWindowBorder(dpy, new_focus_win, 0xFF0000);
                        XSetInputFocus(dpy, new_focus_win, RevertToParent, CurrentTime);
                    } else {
                        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
                    }
                    draw_bar();
                }
                return;
            }
        }
    }
}

void client_focus(Window w) {
    if (w == root || w == None || w == bar_win) return;
    
    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);
    if (focused != w && focused != root && focused != None) {
        XSetWindowBorder(dpy, focused, 0x000000);
    }
    
    XSetWindowBorder(dpy, w, 0xFF0000);
    XRaiseWindow(dpy, w);
    XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
}

void focus_next_client(void) {
    Workspace *ws = &workspaces[current_ws];
    if (ws->count == 0) return;

    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);
    
    for (int i = 0; i < ws->count; i++) {
        if (ws->clients[i]->win == focused) {
            int next_idx = (i + 1) % ws->count;
            client_focus(ws->clients[next_idx]->win);
            return;
        }
    }
    if (ws->count > 0) client_focus(ws->clients[0]->win);
}

void focus_prev_client(void) {
    Workspace *ws = &workspaces[current_ws];
    if (ws->count == 0) return;

    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);
    
    for (int i = 0; i < ws->count; i++) {
        if (ws->clients[i]->win == focused) {
            int prev_idx = (i - 1 + ws->count) % ws->count;
            client_focus(ws->clients[prev_idx]->win);
            return;
        }
    }
    if (ws->count > 0) client_focus(ws->clients[ws->count - 1]->win);
}


void switch_workspace(int new_ws) {
    if (new_ws < 0 || new_ws >= NUM_WORKSPACES || new_ws == current_ws) return;

    for (int i = 0; i < workspaces[current_ws].count; i++) {
        XUnmapWindow(dpy, workspaces[current_ws].clients[i]->win);
    }
    
    current_ws = new_ws;

    for (int i = 0; i < workspaces[current_ws].count; i++) {
        XMapWindow(dpy, workspaces[current_ws].clients[i]->win);
    }

    if (workspaces[current_ws].count > 0) {
        client_focus(workspaces[current_ws].clients[0]->win);
    } else {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    }

    draw_bar();
}

void maprequest(XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;
    XWindowAttributes wa;

    if (!XGetWindowAttributes(dpy, ev->window, &wa)) return;
    if (wa.override_redirect) return;

    XMoveResizeWindow(dpy, ev->window, 0, BAR_HEIGHT, sw, sh - BAR_HEIGHT);
    client_add(ev->window);
    XMapWindow(dpy, ev->window);
}

void unmapnotify(XEvent *e) {
    XUnmapEvent *ev = &e->xunmap;
    client_remove(ev->window);
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

void destroy_notify(XEvent *e) {
    XDestroyWindowEvent *ev = &e->xdestroywindow;
    client_remove(ev->window);
}

void enter_notify(XEvent *e) {
    XCrossingEvent *ev = &e->xcrossing;
    if (ev->mode != NotifyGrab) {
        client_focus(ev->window);
    }
}

void init_bar(void) {
    bar_win = XCreateSimpleWindow(dpy, root, 0, 0, sw, BAR_HEIGHT, 0, 0, BAR_COLOR);
    XSelectInput(dpy, bar_win, ExposureMask);
    XMapWindow(dpy, bar_win);

    gc = XCreateGC(dpy, bar_win, 0, NULL);
    XSetForeground(dpy, gc, BAR_TEXT_COLOR);
}



void init_workspaces(void) {
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        workspaces[i].id = i;
        workspaces[i].count = 0;
    }
}

void grabkeys(void) {
    updatenumlockmask();
    unsigned int j;
    unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    KeyCode k1 = XKeysymToKeycode(dpy, XK_F1);
    KeyCode k2 = XKeysymToKeycode(dpy, XK_F2);
    KeyCode k_quit = XKeysymToKeycode(dpy, XK_q);
    KeyCode k_tab = XKeysymToKeycode(dpy, XK_Tab);
    KeyCode k_j = XKeysymToKeycode(dpy, XK_j);
    KeyCode k_k = XKeysymToKeycode(dpy, XK_k);


    XUngrabKey(dpy, AnyKey, AnyModifier, root);

    for (j = 0; j < 4; j++) {
        if (k1) XGrabKey(dpy, k1, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
        if (k2) XGrabKey(dpy, k2, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
        if (k_quit) XGrabKey(dpy, k_quit, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
        if (k_tab) XGrabKey(dpy, k_tab, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
        if (k_j) XGrabKey(dpy, k_j, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
        if (k_k) XGrabKey(dpy, k_k, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
        
        for (int i = 0; i < NUM_WORKSPACES; i++) {
            KeyCode k_ws = XKeysymToKeycode(dpy, XK_1 + i);
            if (k_ws) XGrabKey(dpy, k_ws, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
        }

        XGrabButton(dpy, 1, MOD_MASK | modifiers[j], root, True, MOUSE_MASK, GrabModeAsync, GrabModeAsync, None, None);
        XGrabButton(dpy, 3, MOD_MASK | modifiers[j], root, True, MOUSE_MASK, GrabModeAsync, GrabModeAsync, None, None);
    }
}

int main(void) {
    XEvent ev;
    XWindowAttributes attr = {0};
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
    sw = DisplayWidth(dpy, screen);
    sh = DisplayHeight(dpy, screen);

    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | EnterWindowMask);
    
    init_workspaces();
    init_bar();
    grabkeys();
    spawn("xterm");

    for (;;) {
        XNextEvent(dpy, &ev);
        
        if (ev.type == KeyPress) {
            KeySym keysym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
            if (CLEANMASK(ev.xkey.state) == MOD_MASK) {
                if (keysym == XK_F1) {
                    spawn("xterm");
                } else if (keysym == XK_F2) {
                    Window w;
                    int revert;
                    XGetInputFocus(dpy, &w, &revert);
                    if(w != root && w != 0 && w != bar_win) XDestroyWindow(dpy, w);
                } else if (keysym == XK_q) {
                    XCloseDisplay(dpy);
                    return 0;
                } else if (keysym >= XK_1 && keysym <= XK_9) {
                    switch_workspace(keysym - XK_1);
                } else if (keysym == XK_Tab) {
                    switch_workspace((current_ws + 1) % NUM_WORKSPACES);
                } else if (keysym == XK_j) {
                    focus_next_client();
                } else if (keysym == XK_k) {
                    focus_prev_client();
                }
            }
        }
        else if (ev.type == ButtonPress) {
            if (ev.xbutton.subwindow == None || ev.xbutton.subwindow == bar_win) continue;
            
            XGetWindowAttributes(dpy, ev.xbutton.subwindow, &attr);
            start = ev.xbutton;
            
            client_focus(start.subwindow);
            XGrabPointer(dpy, start.subwindow, True, MOUSE_MASK, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
        }
        else if (ev.type == MotionNotify) {
            if (start.subwindow == None) continue;

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
            XUngrabPointer(dpy, CurrentTime);
            start.subwindow = None;
        }
        else if (ev.type == MapRequest) {
            maprequest(&ev);
        }
        else if (ev.type == ConfigureRequest) {
            configurerequest(&ev);
        }
        else if (ev.type == UnmapNotify) {
            unmapnotify(&ev);
        }
        else if (ev.type == DestroyNotify) {
            destroy_notify(&ev);
        }
        else if (ev.type == EnterNotify) {
            enter_notify(&ev);
        }
        else if (ev.type == Expose && ev.xexpose.window == bar_win) {
            draw_bar();
        }
    }
}