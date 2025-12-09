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

#define NR_WORKSPACES 9
#define BAR_HEIGHT 20
#define BAR_COLOR 0x222222
#define BAR_FOCUS_COLOR 0x005577
#define BAR_TEXT_COLOR 0xeeeeee
#define BORDER_FOCUS_COLOR 0xFF0000
#define BORDER_NORMAL_COLOR 0x000000
#define BORDER_WIDTH 1

enum {
	EV_KeyPress = 2,
	EV_ButtonPress,
	EV_MotionNotify = 6,
	EV_ButtonRelease = 5,
	EV_MapRequest = 23,
	EV_ConfigureRequest = 24,
	EV_UnmapNotify = 10,
	EV_DestroyNotify = 17,
	EV_EnterNotify = 7,
	EV_Expose = 12
};

struct client {
	struct client *next;
	Window win;
	int ws_id;
	int x, y, w, h;
	int start_x, start_y, start_w, start_h;
};

struct workspace {
	int id;
	struct client *clients;
	struct client *focus;
};

static XWindowAttributes start_attr;
static XButtonEvent start_event;
static struct client *current_drag_client = NULL;


static Display *dpy;
static Window root;
static int screen;
static int sw, sh;
static unsigned int numlockmask;

static struct workspace workspaces[NR_WORKSPACES];
static int current_ws;

static Window bar_win;
static GC gc;

static struct client *client_find(Window w);
static void client_set_border(struct client *c, unsigned long color);
static void client_focus(struct client *c);
static void client_add(Window w, XWindowAttributes *wa);
static void client_remove(struct client *c);
static void focus_next_client(void);
static void focus_prev_client(void);
static void switch_workspace(int new_ws);
static void handle_map_request(XEvent *e);
static void handle_unmap_notify(XEvent *e);
static void handle_configure_request(XEvent *e);
static void handle_destroy_notify(XEvent *e);
static void handle_enter_notify(XEvent *e);
static void handle_expose(XEvent *e);
static void handle_key_press(XEvent *e);
static void handle_button_press(XEvent *e);
static void handle_motion_notify(XEvent *e);
static void handle_button_release(XEvent *e);
static void init_bar(void);
static void draw_bar(void);
static void init_workspaces(void);
static void grabkeys(void);
static void spawn(const char *cmd);
static int xerror(Display *dpy, XErrorEvent *er);
static void updatenumlockmask(void);

int main(void)
{
	XEvent ev;

	if (!(dpy = XOpenDisplay(NULL)))
		return 1;

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
		
		switch (ev.type) {
		case EV_KeyPress:
			handle_key_press(&ev);
			break;
		case EV_ButtonPress:
			handle_button_press(&ev);
			break;
		case EV_MotionNotify:
			handle_motion_notify(&ev);
			break;
		case EV_ButtonRelease:
			handle_button_release(&ev);
			break;
		case EV_MapRequest:
			handle_map_request(&ev);
			break;
		case EV_ConfigureRequest:
			handle_configure_request(&ev);
			break;
		case EV_UnmapNotify:
			handle_unmap_notify(&ev);
			break;
		case EV_DestroyNotify:
			handle_destroy_notify(&ev);
			break;
		case EV_EnterNotify:
			handle_enter_notify(&ev);
			break;
		case EV_Expose:
			handle_expose(&ev);
			break;
		}
	}
}

static void handle_button_press(XEvent *e)
{
    XButtonEvent *be = &e->xbutton;
    
    if (be->subwindow == None || be->subwindow == bar_win)
        return;

    // 关键修复：检查 MOD_MASK 是否存在于 CLEANMASK 后的状态中
    if (!(CLEANMASK(be->state) & MOD_MASK))
        return;

    current_drag_client = client_find(be->subwindow);
    if (!current_drag_client)
        return;
    
    client_focus(current_drag_client);
    XGetWindowAttributes(dpy, be->subwindow, &start_attr);
    start_event = *be;

    if (XGrabPointer(dpy, be->subwindow, True, MOUSE_MASK, GrabModeAsync, GrabModeAsync, None, None, CurrentTime) != GrabSuccess) {
        current_drag_client = NULL;
        return;
    }
}

static void handle_motion_notify(XEvent *e)
{
    XMotionEvent *me = &e->xmotion;
    
    if (current_drag_client == NULL)
        return;
    
    if (me->window != current_drag_client->win)
        return;

    int xdiff = me->x_root - start_event.x_root;
    int ydiff = me->y_root - start_event.y_root;

    if (start_event.button == 1) { 
        current_drag_client->x = start_attr.x + xdiff;
        current_drag_client->y = start_attr.y + ydiff;
        XMoveWindow(dpy, current_drag_client->win, current_drag_client->x, current_drag_client->y);
    } else if (start_event.button == 3) { 
        current_drag_client->w = MAX(1, start_attr.width + xdiff);
        current_drag_client->h = MAX(1, start_attr.height + ydiff);
        XResizeWindow(dpy, current_drag_client->win, current_drag_client->w, current_drag_client->h);
    }
}

static void handle_button_release(XEvent *e)
{
    if (current_drag_client == NULL)
        return;

    XUngrabPointer(dpy, CurrentTime);
    current_drag_client = NULL;

    XEvent ev;
    while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

static struct client *client_find(Window w)
{
	int i;
	struct client *c;

	for (i = 0; i < NR_WORKSPACES; i++) {
		for (c = workspaces[i].clients; c; c = c->next) {
			if (c->win == w)
				return c;
		}
	}
	return NULL;
}

static void client_set_border(struct client *c, unsigned long color)
{
	XSetWindowBorder(dpy, c->win, color);
}

static void client_focus(struct client *c)
{
	struct client *old_focus;

	if (!c || c->win == root || c->win == bar_win)
		return;

	old_focus = workspaces[current_ws].focus;
	if (old_focus && old_focus != c)
		client_set_border(old_focus, BORDER_NORMAL_COLOR);

	workspaces[current_ws].focus = c;
	client_set_border(c, BORDER_FOCUS_COLOR);
	XRaiseWindow(dpy, c->win);
	XSetInputFocus(dpy, c->win, RevertToParent, CurrentTime);
}

static void client_add(Window w, XWindowAttributes *wa)
{
	struct client *new;
	struct workspace *ws = &workspaces[current_ws];

	new = (struct client *)malloc(sizeof(struct client));
	if (!new)
		return;

	new->win = w;
	new->ws_id = current_ws;
	new->x = wa->x;
	new->y = wa->y;
	new->w = wa->width;
	new->h = wa->height;
	new->next = ws->clients;
	ws->clients = new;

	XSetWindowBorderWidth(dpy, w, BORDER_WIDTH);
	XSetWindowBorder(dpy, w, BORDER_NORMAL_COLOR);

	client_focus(new);
}

static void client_remove(struct client *c)
{
	struct workspace *ws;
	struct client **cur;

	if (!c)
		return;

	ws = &workspaces[c->ws_id];

	if (ws->focus == c)
		ws->focus = NULL;

	for (cur = &ws->clients; *cur; cur = &(*cur)->next) {
		if (*cur == c) {
			*cur = c->next;
			free(c);
			break;
		}
	}

	if (ws->id == current_ws) {
		if (!ws->focus && ws->clients)
			client_focus(ws->clients);

		if (!ws->focus)
			XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);

		draw_bar();
	}
}

static void focus_next_client(void)
{
	struct client *c = workspaces[current_ws].focus;
	if (!c)
		return;

	if (c->next)
		client_focus(c->next);
	else
		client_focus(workspaces[current_ws].clients);
}

static void focus_prev_client(void)
{
	struct client *c = workspaces[current_ws].focus;
	struct client *last = NULL;

	if (!c)
		return;

	if (c == workspaces[current_ws].clients) {
		for (last = c; last->next; last = last->next)
			;
		client_focus(last);
		return;
	}

	for (last = workspaces[current_ws].clients; last; last = last->next) {
		if (last->next == c) {
			client_focus(last);
			return;
		}
	}
}

static void switch_workspace(int new_ws)
{
	struct client *c;

	if (new_ws < 0 || new_ws >= NR_WORKSPACES || new_ws == current_ws)
		return;

	for (c = workspaces[current_ws].clients; c; c = c->next)
		XUnmapWindow(dpy, c->win);

	current_ws = new_ws;
	
	for (c = workspaces[current_ws].clients; c; c = c->next)
		XMapWindow(dpy, c->win);

	if (workspaces[current_ws].focus)
		client_focus(workspaces[current_ws].focus);
	else if (workspaces[current_ws].clients)
		client_focus(workspaces[current_ws].clients);
	else
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);

	draw_bar();
}

static void handle_map_request(XEvent *e)
{
	XMapRequestEvent *ev = &e->xmaprequest;
	XWindowAttributes wa;

	if (!XGetWindowAttributes(dpy, ev->window, &wa))
		return;
	if (wa.override_redirect)
		return;

	int default_w = sw / 2;
	int default_h = (sh - BAR_HEIGHT) / 2;
	
	wa.x = (sw - default_w) / 2;
	wa.y = BAR_HEIGHT + (sh - BAR_HEIGHT - default_h) / 2;
	wa.width = default_w;
	wa.height = default_h;

	XMoveResizeWindow(dpy, ev->window, wa.x, wa.y, wa.width, wa.height);
	
	client_add(ev->window, &wa);
	XMapWindow(dpy, ev->window);
}

static void handle_unmap_notify(XEvent *e)
{
	XUnmapEvent *ev = &e->xunmap;
	struct client *c = client_find(ev->window);
	client_remove(c);
}

static void handle_configure_request(XEvent *e)
{
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;
	struct client *c;

	wc.x = ev->x;
	wc.y = ev->y;
	wc.width = ev->width;
	wc.height = ev->height;
	wc.border_width = ev->border_width;
	wc.sibling = ev->above;
	wc.stack_mode = ev->detail;
	
	XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	
	c = client_find(ev->window);
	if (c) {
		if (ev->value_mask & CWX) c->x = ev->x;
		if (ev->value_mask & CWY) c->y = ev->y;
		if (ev->value_mask & CWWidth) c->w = ev->width;
		if (ev->value_mask & CWHeight) c->h = ev->height;
	}
}

static void handle_destroy_notify(XEvent *e)
{
	XDestroyWindowEvent *ev = &e->xdestroywindow;
	struct client *c = client_find(ev->window);
	client_remove(c);
}

static void handle_enter_notify(XEvent *e)
{
	XCrossingEvent *ev = &e->xcrossing;
	struct client *c;

	if (ev->mode != NotifyGrab) {
		c = client_find(ev->window);
		if (c && c->ws_id == current_ws)
			client_focus(c);
	}
}

static void handle_expose(XEvent *e)
{
	if (e->xexpose.window == bar_win)
		draw_bar();
}

static void handle_key_press(XEvent *e)
{
	KeySym keysym = XkbKeycodeToKeysym(dpy, e->xkey.keycode, 0, 0);

	if (CLEANMASK(e->xkey.state) == MOD_MASK) {
		if (keysym == XK_F1) {
			spawn("xterm");
		} else if (keysym == XK_F2) {
			Window w;
			int revert;
			XGetInputFocus(dpy, &w, &revert);
			if (w != root && w != 0 && w != bar_win)
				XDestroyWindow(dpy, w);
		} else if (keysym == XK_q) {
			XCloseDisplay(dpy);
			exit(0);
		} else if (keysym >= XK_1 && keysym <= XK_9) {
			switch_workspace(keysym - XK_1);
		} else if (keysym == XK_Tab) {
			switch_workspace((current_ws + 1) % NR_WORKSPACES);
		} else if (keysym == XK_j) {
			focus_next_client();
		} else if (keysym == XK_k) {
			focus_prev_client();
		}
	}
}

static void init_bar(void)
{
	bar_win = XCreateSimpleWindow(dpy, root, 0, 0, sw, BAR_HEIGHT, 0, 0, BAR_COLOR);
	XSelectInput(dpy, bar_win, ExposureMask);
	XMapWindow(dpy, bar_win);

	gc = XCreateGC(dpy, bar_win, 0, NULL);
	XSetForeground(dpy, gc, BAR_TEXT_COLOR);
}

static void draw_bar(void)
{
	char buf[64];
	int x = 5;
	int count = 0;
	struct client *c;

	XSetForeground(dpy, gc, BAR_COLOR);
	XFillRectangle(dpy, bar_win, gc, 0, 0, sw, BAR_HEIGHT);

	for (int i = 0; i < NR_WORKSPACES; i++) {
		if (i == current_ws) {
			XSetForeground(dpy, gc, BAR_FOCUS_COLOR);
			XFillRectangle(dpy, bar_win, gc, x - 2, 2, 14, BAR_HEIGHT - 4);
			
			for (c = workspaces[i].clients; c; c = c->next)
				count++;
		}

		XSetForeground(dpy, gc, BAR_TEXT_COLOR);
		snprintf(buf, sizeof(buf), "%d", i + 1);
		XDrawString(dpy, bar_win, gc, x, 15, buf, 1);
		x += 20;
	}

	snprintf(buf, sizeof(buf), "Win: %d", count);
	XDrawString(dpy, bar_win, gc, sw - 100, 15, buf, strlen(buf));
}

static void init_workspaces(void)
{
	for (int i = 0; i < NR_WORKSPACES; i++) {
		workspaces[i].id = i;
		workspaces[i].clients = NULL;
		workspaces[i].focus = NULL;
	}
}

static void grabkeys(void)
{
	updatenumlockmask();
	unsigned int j;
	unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask, Mod2Mask, Mod2Mask|LockMask, Mod2Mask|numlockmask, Mod2Mask|numlockmask|LockMask };
	KeyCode k1 = XKeysymToKeycode(dpy, XK_F1);
	KeyCode k2 = XKeysymToKeycode(dpy, XK_F2);
	KeyCode k_quit = XKeysymToKeycode(dpy, XK_q);
	KeyCode k_tab = XKeysymToKeycode(dpy, XK_Tab);
	KeyCode k_j = XKeysymToKeycode(dpy, XK_j);
	KeyCode k_k = XKeysymToKeycode(dpy, XK_k);

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
    XUngrabButton(dpy, AnyButton, AnyModifier, root);

	for (j = 0; j < 8; j++) {
		if (k1) XGrabKey(dpy, k1, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
		if (k2) XGrabKey(dpy, k2, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
		if (k_quit) XGrabKey(dpy, k_quit, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
		if (k_tab) XGrabKey(dpy, k_tab, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
		if (k_j) XGrabKey(dpy, k_j, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
		if (k_k) XGrabKey(dpy, k_k, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
		
		for (int i = 0; i < NR_WORKSPACES; i++) {
			KeyCode k_ws = XKeysymToKeycode(dpy, XK_1 + i);
			if (k_ws) XGrabKey(dpy, k_ws, MOD_MASK | modifiers[j], root, True, GrabModeAsync, GrabModeAsync);
		}

		XGrabButton(dpy, 1, MOD_MASK | modifiers[j], root, True, MOUSE_MASK, GrabModeAsync, GrabModeAsync, None, None);
		XGrabButton(dpy, 3, MOD_MASK | modifiers[j], root, True, MOUSE_MASK, GrabModeAsync, GrabModeAsync, None, None);
	}
}

static void spawn(const char *cmd)
{
	if (fork() == 0) {
		if (dpy) close(ConnectionNumber(dpy));
		setsid();
		execlp(cmd, cmd, NULL);
		_exit(0);
	}
}

static int xerror(Display *dpy, XErrorEvent *er)
{
	return 0;
}

static void updatenumlockmask(void)
{
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