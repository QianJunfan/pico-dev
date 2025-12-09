#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
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
	struct dpy *dpy;
	union {
		int data[4];
		struct {
			int x, y, w, h;
		};
	} geo;
};

struct dpy {
	uint64_t id;
	struct _XDisplay *display;
	uint64_t cli_cnt;
	struct cli *clis;
	struct cli *cli_focus;
	Window root;
	union {
		int data[4];
		struct {
			int x, y;
			int w, h;
		};
	} geo;
};

struct {	
	struct dpy *sel_dpy;
} runtime;

static struct dpy *g_dpy = NULL;

#define MODKEY Mod4Mask
#define LENGTH(X) (sizeof X / sizeof X[0])

static void quit(const union Arg *arg);
static void spawn(const union Arg *arg);

static void handle_maprequest(XEvent *e);
static void handle_buttonpress(XEvent *e);
static void handle_destroynotify(XEvent *e);
static void handle_keypress(XEvent *e);

static void dpy_init(void);
static void dpy_destroy(void);

static void cli_init(Window w);
static void cli_destroy(struct cli *c);
static void cli_resize(struct cli *tar, int w, int h);
static void cli_move(struct cli *tar, int x, int y);
static void cli_focus(struct cli *tar);

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
		fprintf(stderr, "dwm: execvp %s failed\n", ((char **)arg->v)[0]);
		exit(1);
	}
}

static void dpy_init(void)
{
	g_dpy = malloc(sizeof(struct dpy));
	if (!g_dpy) {
		fprintf(stderr, "Error: Could not allocate memory for dpy.\n");
		exit(1);
	}
	g_dpy->display = XOpenDisplay(NULL);
	if (!g_dpy->display) {
		fprintf(stderr, "Error: Cannot open display.\n");
		exit(1);
	}

	g_dpy->root = DefaultRootWindow(g_dpy->display);
	g_dpy->cli_cnt = 0;
	g_dpy->clis = NULL;
	g_dpy->cli_focus = NULL;
	runtime.sel_dpy = g_dpy;

	g_dpy->geo.x = 0;
	g_dpy->geo.y = 0;
	g_dpy->geo.w = DisplayWidth(g_dpy->display, DefaultScreen(g_dpy->display));
	g_dpy->geo.h = DisplayHeight(g_dpy->display, DefaultScreen(g_dpy->display));
	
	if (XSelectInput(g_dpy->display, g_dpy->root, 
			SubstructureRedirectMask | KeyPressMask | ButtonPressMask | SubstructureNotifyMask) == BadAccess) {
		fprintf(stderr, "Fatal: Another WM is already running.\n");
		XCloseDisplay(g_dpy->display);
		free(g_dpy);
		exit(1);
	}
}

static void dpy_destroy(void)
{
	if (g_dpy && g_dpy->display) {
		XCloseDisplay(g_dpy->display);
		free(g_dpy);
	}
}

static void cli_init(Window w)
{
	struct cli *c;
	c = malloc(sizeof(struct cli));
	if (!c)
		return;

	c->win = w;
	c->dpy = g_dpy;
	
	c->geo.x = g_dpy->geo.w / 4;
	c->geo.y = g_dpy->geo.h / 4;
	c->geo.w = g_dpy->geo.w / 2;
	c->geo.h = g_dpy->geo.h / 2;

	c->next = g_dpy->clis;
	g_dpy->clis = c;
	g_dpy->cli_cnt++;

	cli_move(c, c->geo.x, c->geo.y);
	cli_resize(c, c->geo.w, c->geo.h);
	XMapWindow(g_dpy->display, c->win);
	
	cli_focus(c);
	
	XSelectInput(g_dpy->display, c->win, StructureNotifyMask);
}

static void cli_destroy(struct cli *c)
{
	struct cli **curr;
	if (!c)
		return;

	for (curr = &g_dpy->clis; *curr; curr = &(*curr)->next) {
		if (*curr == c) {
			*curr = c->next;
			break;
		}
	}

	if (c == g_dpy->cli_focus)
		g_dpy->cli_focus = NULL;
	
	g_dpy->cli_cnt--;
	free(c);
}

static void cli_resize(struct cli *tar, int w, int h)
{
	XWindowChanges wc;

	tar->geo.w = w;
	tar->geo.h = h;

	wc.width = w;
	wc.height = h;

	XConfigureWindow(tar->dpy->display, tar->win, CWWidth | CWHeight, &wc);
	XSync(tar->dpy->display, False);
}

static void cli_move(struct cli *tar, int x, int y)
{
	XWindowChanges wc;
	
	tar->geo.x = x;
	tar->geo.y = y;
	
	wc.x = x;
	wc.y = y;

	XConfigureWindow(tar->dpy->display, tar->win, CWX | CWY, &wc);
	XSync(tar->dpy->display, False);
}

static void cli_focus(struct cli *tar)
{
	if (!tar)
		return;

	if (g_dpy->cli_focus && g_dpy->cli_focus != tar) {
		
	}
	
	g_dpy->cli_focus = tar;
	XSetInputFocus(g_dpy->display, tar->win, RevertToPointerRoot, CurrentTime);
}

static void init(void)
{
	dpy_init();

	for (int i = 0; i < LENGTH(keys); i++) {
		XGrabKey(g_dpy->display, XKeysymToKeycode(g_dpy->display, keys[i].keysym), 
			keys[i].mod, g_dpy->root, True, GrabModeAsync, GrabModeAsync);
	}

	handler[MapRequest]    = handle_maprequest;
	handler[ButtonPress]   = handle_buttonpress;
	handler[DestroyNotify] = handle_destroynotify;
	handler[KeyPress]      = handle_keypress;
	
	printf("Minimal Floating WM started.\n");
}

static void quit(const union Arg *arg)
{
	printf("Minimal Floating WM shutting down.\n");
	dpy_destroy();
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
		XNextEvent(g_dpy->display, &ev);
		handle(&ev);
	}
}

static void handle_maprequest(XEvent *e)
{
	XMapRequestEvent *ev = &e->xmaprequest;
	struct cli *c;
	for (c = g_dpy->clis; c; c = c->next) {
		if (c->win == ev->window)
			return;
	}

	cli_init(ev->window);
}

static void handle_destroynotify(XEvent *e)
{
	XDestroyWindowEvent *ev = &e->xdestroywindow;
	
	struct cli *c;
	for (c = g_dpy->clis; c; c = c->next) {
		if (c->win == ev->window) {
			cli_destroy(c);
			return;
		}
	}
}

static void handle_buttonpress(XEvent *e)
{
	XButtonEvent *ev = &e->xbutton;

	struct cli *c;
	for (c = g_dpy->clis; c; c = c->next) {
		if (c->win == ev->subwindow || c->win == ev->window) {
			cli_focus(c);
			return;
		}
	}
}

static void handle_keypress(XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	KeySym keysym;

	keysym = XkbKeycodeToKeysym(g_dpy->display, (KeyCode)ev->keycode, 0, 0);
	unsigned int cleanmask = ev->state & (MODKEY | ShiftMask | ControlMask); 
	
	for (int i = 0; i < LENGTH(keys); i++) {
		if (keysym == keys[i].keysym && cleanmask == keys[i].mod) {
			keys[i].func(&keys[i].arg); 
			return;
		}
	}
}

int main(void)
{
	init();
	listen();

	return 0;	
}
