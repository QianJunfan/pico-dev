#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>


union Arg {
	const void *v;
};

struct Key {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const union Arg *arg);
	const union Arg arg;
};

struct cli {
	Window win;
	struct cli *next;
	struct Monitor *mon;

	int x, y, w, h;
	int oldx, oldy, oldw, oldh;

	int isfloating;
};

struct Monitor {
	uint64_t id;
	struct _XDisplay *display;

	struct cli *clis;
	struct cli *sel;
	struct cli *cli_drag;

	Window root;
	int wx, wy, ww, wh;

	int ptr_x, ptr_y;
};

struct {	
	struct Monitor *sel_mon;
} runtime;

static struct Monitor *g_mon = NULL;

#define MODKEY Mod4Mask
#define LENGTH(X) (sizeof X / sizeof X[0])

static void quit(const union Arg *arg);
static void spawn(const union Arg *arg);

static void handle_maprequest(XEvent *e);
static void handle_buttonpress(XEvent *e);
static void handle_buttonrelease(XEvent *e);
static void handle_motionnotify(XEvent *e);
static void handle_destroynotify(XEvent *e);
static void handle_keypress(XEvent *e);

static void mon_init(void);
static void mon_destroy(void);

static void cli_init(Window w);
static void cli_destroy(struct cli *c);
static void cli_resize(struct cli *c, int x, int y, int w, int h);
static void cli_focus(struct cli *c);

static void init(void);
static void listen(void);
static void handle(XEvent *e);

static char *termcmd[] = { "xterm", NULL };

static const struct Key keys[] = {
	{ MODKEY, XK_q, quit, {0} },
	{ MODKEY, XK_Return, spawn, {.v = termcmd } },
};

void (*handler[LASTEvent]) (XEvent *) = { 0 };

static void spawn(const union Arg *arg)
{
	if (fork() == 0) {
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "Minimal WM: execvp %s failed\n", ((char **)arg->v)[0]);
		exit(1);
	}
}

static void mon_init(void)
{
	g_mon = malloc(sizeof(struct Monitor));
	if (!g_mon) {
		fprintf(stderr, "Error: Could not allocate memory for Monitor.\n");
		exit(1);
	}
	g_mon->display = XOpenDisplay(NULL);
	if (!g_mon->display) {
		fprintf(stderr, "Error: Cannot open display.\n");
		exit(1);
	}

	g_mon->root = DefaultRootWindow(g_mon->display);
	g_mon->clis = NULL;
	g_mon->sel = NULL;
	g_mon->cli_drag = NULL;
	runtime.sel_mon = g_mon;

	g_mon->wx = g_mon->wy = 0;
	g_mon->ww = DisplayWidth(g_mon->display, DefaultScreen(g_mon->display));
	g_mon->wh = DisplayHeight(g_mon->display, DefaultScreen(g_mon->display));
	
	if (XSelectInput(g_mon->display, g_mon->root, 
			SubstructureRedirectMask | KeyPressMask | ButtonPressMask | SubstructureNotifyMask) == BadAccess) {
		fprintf(stderr, "Fatal: Another WM is already running.\n");
		XCloseDisplay(g_mon->display);
		free(g_mon);
		exit(1);
	}
}

static void mon_destroy(void)
{
	if (g_mon && g_mon->display) {
		XCloseDisplay(g_mon->display);
		free(g_mon);
	}
}

static void cli_init(Window w)
{
	struct cli *c;
	c = malloc(sizeof(struct cli));
	if (!c)
		return;

	c->win = w;
	c->mon = g_mon;
	c->isfloating = 1; 
	
	c->x = g_mon->ww / 4;
	c->y = g_mon->wh / 4;
	c->w = g_mon->ww / 2;
	c->h = g_mon->wh / 2;

    c->oldx = c->x; c->oldy = c->y; c->oldw = c->w; c->oldh = c->h;

	c->next = g_mon->clis;
	g_mon->clis = c;

	cli_resize(c, c->x, c->y, c->w, c->h);
	XMapWindow(g_mon->display, c->win);
	
	cli_focus(c);
	
	XSelectInput(g_mon->display, c->win, StructureNotifyMask);
}

static void cli_destroy(struct cli *c)
{
	struct cli **curr;
	if (!c)
		return;

	for (curr = &g_mon->clis; *curr; curr = &(*curr)->next) {
		if (*curr == c) {
			*curr = c->next;
			break;
		}
	}

	if (c == g_mon->sel)
		g_mon->sel = NULL;
	
	if (c == g_mon->cli_drag)
		g_mon->cli_drag = NULL;
		
	free(c);
}

static void cli_resize(struct cli *c, int x, int y, int w, int h)
{
	XWindowChanges wc;

	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;

	wc.x = x;
	wc.y = y;
	wc.width = w;
	wc.height = h;

	XConfigureWindow(c->mon->display, c->win, CWX | CWY | CWWidth | CWHeight, &wc);
	XSync(c->mon->display, False);
}

static void cli_focus(struct cli *c)
{
	if (!c)
		return;

	if (g_mon->sel && g_mon->sel != c) {
		
	}
	
	g_mon->sel = c;
	XSetInputFocus(g_mon->display, c->win, RevertToPointerRoot, CurrentTime);
}

static void init(void)
{
	mon_init();

	for (int i = 0; i < LENGTH(keys); i++) {
		XGrabKey(g_mon->display, XKeysymToKeycode(g_mon->display, keys[i].keysym), 
			keys[i].mod, g_mon->root, True, GrabModeAsync, GrabModeAsync);
	}

	XGrabButton(g_mon->display, Button1, MODKEY, g_mon->root,
		True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		GrabModeAsync, GrabModeAsync, None, None);

	XGrabButton(g_mon->display, Button3, MODKEY, g_mon->root,
		True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		GrabModeAsync, GrabModeAsync, None, None);

	handler[MapRequest]    = handle_maprequest;
	handler[ButtonPress]   = handle_buttonpress;
	handler[ButtonRelease] = handle_buttonrelease;
	handler[MotionNotify]  = handle_motionnotify;
	handler[DestroyNotify] = handle_destroynotify;
	handler[KeyPress]      = handle_keypress;
	
	printf("Minimal Floating WM started.\n");
}

static void quit(const union Arg *arg)
{
	printf("Minimal Floating WM shutting down.\n");
	mon_destroy();
	exit(0);
}

static void handle(XEvent *e)
{
	if (handler[e->type]) {
		handler[e->type](e);
	}
}

static void listen(void)
{
	XEvent ev;
	while (1) {
		XNextEvent(g_mon->display, &ev);
		handle(&ev);
	}
}

static void handle_maprequest(XEvent *e)
{
	XMapRequestEvent *ev = &e->xmaprequest;
	struct cli *c;
	for (c = g_mon->clis; c; c = c->next) {
		if (c->win == ev->window)
			return;
	}

	cli_init(ev->window);
}

static void handle_destroynotify(XEvent *e)
{
	XDestroyWindowEvent *ev = &e->xdestroywindow;
	
	struct cli *c;
	for (c = g_mon->clis; c; c = c->next) {
		if (c->win == ev->window) {
			cli_destroy(c);
			return;
		}
	}
}

static void handle_keypress(XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	KeySym keysym;

	keysym = XkbKeycodeToKeysym(g_mon->display, (KeyCode)ev->keycode, 0, 0);
	unsigned int cleanmask = ev->state & (MODKEY | ShiftMask | ControlMask); 
	
	for (int i = 0; i < LENGTH(keys); i++) {
		if (keysym == keys[i].keysym && cleanmask == keys[i].mod) {
			keys[i].func(&keys[i].arg); 
			return;
		}
	}
}

static void handle_buttonpress(XEvent *e)
{
	XButtonEvent *ev = &e->xbutton;
	struct cli *c;
	
	for (c = g_mon->clis; c; c = c->next) {
		if (c->win == ev->subwindow || c->win == ev->window) {
			cli_focus(c);
			break;
		}
	}

	if (!c || ev->subwindow == None || ev->state != MODKEY)
		return;

	XGrabPointer(g_mon->display, g_mon->root, False,
		ButtonReleaseMask | PointerMotionMask,
		GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

	g_mon->ptr_x = ev->x_root;
	g_mon->ptr_y = ev->y_root;
	g_mon->cli_drag = c;
}

static void handle_motionnotify(XEvent *e)
{
	XMotionEvent *ev = &e->xmotion;
	struct cli *c = g_mon->cli_drag;
	
	if (!c)
		return;

	int dx = ev->x_root - g_mon->ptr_x;
	int dy = ev->y_root - g_mon->ptr_y;
	
	if (ev->state & Button1Mask) { 
		cli_resize(c, c->x + dx, c->y + dy, c->w, c->h);
	} else if (ev->state & Button3Mask) { 
		int new_w = c->w + dx;
		int new_h = c->h + dy;
		if (new_w < 50) new_w = 50; 
		if (new_h < 50) new_h = 50; 

		cli_resize(c, c->x, c->y, new_w, new_h);
	}

	g_mon->ptr_x = ev->x_root;
	g_mon->ptr_y = ev->y_root;
}

static void handle_buttonrelease(XEvent *e)
{
	XUngrabPointer(g_mon->display, CurrentTime);
	g_mon->cli_drag = NULL;
}


int main(void)
{
	init();
	listen();

	return 0;	
}
