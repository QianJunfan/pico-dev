#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)

static const int borderpx         = 1;
static const int snap             = 32;
static const int showbar          = 1;
static const int topbar           = 1;
static const unsigned int gappx   = 5;

#define COL_BLACK 0x000000
#define COL_RED 0xFF0000
#define COL_BLUE 0x005577
#define COL_WHITE 0xFFFFFF

static const unsigned long colors[][3] = {
    { COL_BLACK, COL_RED },
    { COL_WHITE, COL_WHITE },
    { COL_BLACK, COL_BLUE },
};

static const char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)

#define MODKEY Mod4Mask

static const char *termcmd[] = { "xterm", NULL };

enum { Tile, Monocle, Floating, LayoutLast };
static const float mfact     = 0.55;
static const int nmaster     = 1;

enum { CurNormal, CurResize, CurMove, CurLast };
enum { SchemeNorm, SchemeSel };
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, ClkRootWin, ClkLast };

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
	char name[256];
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int bw, oldbw;
	unsigned int tags;
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
	Client *next;
	Client *snext;
	Monitor *mon;
	Window win;
};

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

struct Monitor {
	char ltsymbol[16];
	float mfact;
	int nmaster;
	int num;
	int by;
	int mx, my, mw, mh;
	int wx, wy, ww, wh;
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	int showbar;
	int topbar;
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	Window barwin;
	const Layout *lt[2];
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

static Display *dpy;
static Window root;
static int screen;
static int sw, sh;
static int bh = 20;
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *);
static Atom wmatom[4], netatom[9];
static int running = 1;
static Cursor cursor[CurLast];
static Monitor *mons, *selmon;
static Window wmcheckwin;
static GC gc;

static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void buttonpress(XEvent *e);
static Monitor *createmon(void);
static void drawbar(Monitor *m);
static void drawbars(void);
static void focus(Client *c);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void maprequest(XEvent *e);
static void monocle(Monitor *m);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void restack(Monitor *m);
static void run(void);
static void setup(void);
static void spawn(const Arg *arg);
static void tile(Monitor *m);
static void togglefloating(const Arg *arg);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);

#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))

static void die(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (fmt[0] && fmt[strlen(fmt)-1] == ':')
		fputc(' ', stderr), perror(NULL);
	exit(1);
}

static void *ecalloc(size_t nmemb, size_t size) {
	void *p;
	if (!(p = calloc(nmemb, size)))
		die("calloc:");
	return p;
}

static unsigned long getcolor(unsigned long color) {
    XColor c;
    char hex[8];
    sprintf(hex, "#%06lX", color);
    if (!XAllocNamedColor(dpy, DefaultColormap(dpy, screen), hex, &c, &c)) {
        return color;
    }
    return c.pixel;
}

static int getrootptr(int *x, int *y) {
	int di;
	unsigned int dui;
	Window dummy;
	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

static void pop(Client *c);
static Monitor *recttomon(int x, int y, int w, int h);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void updatebarpos(Monitor *m);
static int updategeom(void);
static void updatenumlockmask(void);
static void sendmon(Client *c, Monitor *m);
static void setlayout(const Arg *arg);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void toggleview(const Arg *arg);
static void toggletag(const Arg *arg);
static Monitor *dirtomon(int dir);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static void destroynotify(XEvent *e);
static void unmapnotify(XEvent *e);
static void enternotify(XEvent *e);
static void expose(XEvent *e);

typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	unsigned int tags;
	int isfloating;
	int monitor;
} Rule;
static const Rule rules[] = {
	{ "XTerm",    NULL,       NULL,       1 << 0,       0,           -1 },
	{ NULL,       NULL,       NULL,       0,            0,           -1 },
};

static const Key keys[] = {
	{ MODKEY,                       XK_Return, spawn,          {.v = termcmd } },
    { MODKEY,                       XK_q,      killclient,     {0} },
	{ MODKEY,                       XK_j,      focusstack,     {.i = +1 } },
	{ MODKEY,                       XK_k,      focusstack,     {.i = -1 } },
    { MODKEY,                       XK_f,      togglefloating, {0} },
    { MODKEY,                       XK_t,      setlayout,      {.v = (void*)0 } },
    { MODKEY,                       XK_m,      setlayout,      {.v = (void*)1 } },
    { MODKEY,                       XK_space,  setlayout,      {0} },
	
	{ MODKEY,                       XK_1,      view,           {.ui = 1 << 0} },
	{ MODKEY,                       XK_2,      view,           {.ui = 1 << 1} },
	{ MODKEY,                       XK_3,      view,           {.ui = 1 << 2} },
	{ MODKEY,                       XK_4,      view,           {.ui = 1 << 3} },
	{ MODKEY,                       XK_5,      view,           {.ui = 1 << 4} },
	{ MODKEY,                       XK_6,      view,           {.ui = 1 << 5} },
	{ MODKEY,                       XK_7,      view,           {.ui = 1 << 6} },
	{ MODKEY,                       XK_8,      view,           {.ui = 1 << 7} },
	{ MODKEY,                       XK_9,      view,           {.ui = 1 << 8} },

	{ MODKEY|ShiftMask,             XK_1,      tag,            {.ui = 1 << 0} },
	{ MODKEY|ShiftMask,             XK_2,      tag,            {.ui = 1 << 1} },
	{ MODKEY|ShiftMask,             XK_3,      tag,            {.ui = 1 << 2} },
	{ MODKEY|ShiftMask,             XK_4,      tag,            {.ui = 1 << 3} },
	{ MODKEY|ShiftMask,             XK_5,      tag,            {.ui = 1 << 4} },
	{ MODKEY|ShiftMask,             XK_6,      tag,            {.ui = 1 << 5} },
	{ MODKEY|ShiftMask,             XK_7,      tag,            {.ui = 1 << 6} },
	{ MODKEY|ShiftMask,             XK_8,      tag,            {.ui = 1 << 7} },
	{ MODKEY|ShiftMask,             XK_9,      tag,            {.ui = 1 << 8} },
};

static const Button buttons[] = {
	{ ClkLtSymbol,          0,              Button1,        setlayout,      {0} },
	{ ClkClientWin,         MODKEY,         Button1,        movemouse,      {0} },
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
	{ ClkTagBar,            0,              Button1,        view,           {0} },
	{ ClkTagBar,            MODKEY,         Button1,        toggleview,     {0} },
	{ ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} },
};

static const Layout layouts[] = {
	{ "[]=",      tile },
	{ "[M]",      monocle },
	{ "><>",      NULL }
};

static void applyrules(Client *c) {
	const Rule *r;
	c->isfloating = 0;
	c->tags = 0;
	r = &rules[0];

	c->isfloating = r->isfloating;
	c->tags |= r->tags;
    
	c->mon = selmon;
	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

static void arrange(Monitor *m) {
	if (m) {
		arrangemon(m);
		restack(m);
	} else for (m = mons; m; m = m->next)
		arrangemon(m);
}

static void arrangemon(Monitor *m) {
	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);
	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
	drawbar(m);
}

static void attach(Client *c) {
	c->next = c->mon->clients;
	c->mon->clients = c;
}

static void attachstack(Client *c) {
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

static void buttonpress(XEvent *e) {
	unsigned int i, click;
	Arg arg = {0};
	Client *c;
	Monitor *m;
	XButtonPressedEvent *ev = &e->xbutton;

	click = ClkRootWin;
	
	m = wintomon(ev->window);
	if (m && m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}

	if (ev->window == selmon->barwin) {
		int x = 0;
        int tag_width = selmon->ww / LENGTH(tags);
		i = 0;
		do
			x += tag_width;
		while (ev->x >= x && ++i < LENGTH(tags));
        
		if (i < LENGTH(tags)) {
			click = ClkTagBar;
			arg.ui = 1 << i;
		} else {
			click = ClkWinTitle; 
		}
	} 
    else if ((c = wintoclient(ev->window))) {
		focus(c);
		restack(selmon);
		XAllowEvents(dpy, ReplayPointer, CurrentTime); 
		click = ClkClientWin;
	}
    
	for (i = 0; i < LENGTH(buttons); i++)
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

static Monitor *createmon(void) {
	Monitor *m;
	m = ecalloc(1, sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->mfact = mfact;
	m->nmaster = nmaster;
	m->showbar = showbar;
	m->topbar = topbar;
	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[2];
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
	return m;
}

static void drawbar(Monitor *m) {
    if (!m->showbar) return;

    int x = 0;
    int tag_width = m->ww / LENGTH(tags);
    
    XSetForeground(dpy, gc, getcolor(colors[SchemeNorm][2]));
    XFillRectangle(dpy, m->barwin, gc, 0, 0, m->ww, bh);

    for (int i = 0; i < LENGTH(tags); i++) {
        int is_selected = m->tagset[m->seltags] & (1 << i);
        unsigned long bg_color = is_selected ? getcolor(colors[SchemeSel][2]) : getcolor(colors[SchemeNorm][2]);
        unsigned long text_color = is_selected ? getcolor(colors[SchemeSel][1]) : getcolor(colors[SchemeNorm][1]);

        XSetForeground(dpy, gc, bg_color);
        XFillRectangle(dpy, m->barwin, gc, x, 0, tag_width, bh);

        XSetForeground(dpy, gc, text_color);
        XDrawString(dpy, m->barwin, gc, x + 5, bh - 5, tags[i], strlen(tags[i]));
        x += tag_width;
    }

    if (m->sel) {
        XSetForeground(dpy, gc, getcolor(colors[SchemeSel][2]));
        XFillRectangle(dpy, m->barwin, gc, x, 0, m->ww - x, bh);
        XSetForeground(dpy, gc, getcolor(colors[SchemeSel][1]));
        XDrawString(dpy, m->barwin, gc, x + 5, bh - 5, m->sel->name, MIN(strlen(m->sel->name), 250));
    } else {
        XSetForeground(dpy, gc, getcolor(colors[SchemeNorm][2]));
        XFillRectangle(dpy, m->barwin, gc, x, 0, m->ww - x, bh);
    }

    XFlush(dpy);
}

static void drawbars(void) {
	Monitor *m;
	for (m = mons; m; m = m->next)
		drawbar(m);
}

static void focus(Client *c) {
	if (!c || !ISVISIBLE(c))
		for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
	if (selmon->sel && selmon->sel != c)
		unfocus(selmon->sel, 0);
	if (c) {
		if (c->mon != selmon)
			selmon = c->mon;
		grabbuttons(c, 1);
		XSetWindowBorder(dpy, c->win, getcolor(colors[SchemeSel][0]));
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
	}
	selmon->sel = c;
	drawbars();
}

static void grabbuttons(Client *c, int focused) {
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		if (!focused)
			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
				BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
		for (i = 0; i < LENGTH(buttons); i++)
			if (buttons[i].click == ClkClientWin)
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabButton(dpy, buttons[i].button,
						buttons[i].mask | modifiers[j],
						c->win, False, MOUSEMASK,
						GrabModeAsync, GrabModeSync, None, None);
	}
}

static void grabkeys(void) {
	updatenumlockmask();
	{
		unsigned int i, j, k;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		int start, end, skip;
		KeySym *syms;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		XDisplayKeycodes(dpy, &start, &end);
		syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
		if (!syms) return;

		for (k = start; k <= end; k++)
			for (i = 0; i < LENGTH(keys); i++)
				if (keys[i].keysym == XkbKeycodeToKeysym(dpy, k, 0, 0))
					for (j = 0; j < LENGTH(modifiers); j++)
						XGrabKey(dpy, k,
							 keys[i].mod | modifiers[j],
							 root, True,
							 GrabModeAsync, GrabModeAsync);
		XFree(syms);
	}
    
    unsigned int j;
	unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    
	for (j = 0; j < LENGTH(modifiers); j++) {
		XGrabButton(dpy, 1, MODKEY | modifiers[j], root, True, MOUSEMASK, GrabModeAsync, GrabModeSync, None, None);
		XGrabButton(dpy, 3, MODKEY | modifiers[j], root, True, MOUSEMASK, GrabModeAsync, GrabModeSync, None, None);
	}
}

static void keypress(XEvent *e) {
	unsigned int i;
	KeySym keysym = XkbKeycodeToKeysym(dpy, e->xkey.keycode, 0, 0);

	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym && CLEANMASK(keys[i].mod) == CLEANMASK(e->xkey.state))
			keys[i].func(&(keys[i].arg));
}

static void killclient(const Arg *arg) {
	if (!selmon->sel) return;
	XKillClient(dpy, selmon->sel->win);
}

static void manage(Window w, XWindowAttributes *wa) {
	Client *c;
	XWindowChanges wc;

	c = ecalloc(1, sizeof(Client));
	c->win = w;

	c->x = c->oldx = wa->x; c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width; c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;
    c->bw = borderpx;
    strncpy(c->name, "N/A", sizeof(c->name)); 
    applyrules(c);

	c->x = MAX(c->x, c->mon->wx); c->y = MAX(c->y, c->mon->wy);
	
	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, getcolor(colors[SchemeNorm][0]));
	configure(c); 
    
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0);

	if (c->isfloating) XRaiseWindow(dpy, c->win);
        
	attach(c);
	attachstack(c);
	XMapWindow(dpy, w);
	focus(c);
	arrange(c->mon);
}

static void maprequest(XEvent *e) {
	XMapRequestEvent *ev = &e->xmaprequest;
	XWindowAttributes wa;

	if (!XGetWindowAttributes(dpy, ev->window, &wa)) return;
	if (wa.override_redirect) return;
    if (wintoclient(ev->window)) return;

	manage(ev->window, &wa);
}

static void monocle(Monitor *m) {
	Client *c;
	for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
		resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
}

static void movemouse(const Arg *arg) {
	int x, y, ocx, ocy, nx, ny;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel) || c->isfullscreen) return;
	
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
    
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove], CurrentTime) != GrabSuccess)
		return;
	
    if (!getrootptr(&x, &y)) return;

	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		
		switch(ev.type) {
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60)) continue;
			lasttime = ev.xmotion.time;

			nx = ocx + (ev.xmotion.x_root - x);
			ny = ocy + (ev.xmotion.y_root - y);
            
			if (abs(selmon->wx - nx) < snap) nx = selmon->wx;
			else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap) nx = selmon->wx + selmon->ww - WIDTH(c);
			if (abs(selmon->wy - ny) < snap) ny = selmon->wy;
			else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap) ny = selmon->wy + selmon->wh - HEIGHT(c);

            if (!c->isfloating && selmon->lt[selmon->sellt]->arrange && (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
				togglefloating(NULL);

			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, 1);
			break;
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			if (handler[ev.type]) handler[ev.type](&ev); 
			break;
		}
	} while (ev.type != ButtonRelease);
    
	XUngrabPointer(dpy, CurrentTime);
	
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
	}
}

static Client *nexttiled(Client *c) {
	for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
}

static void resize(Client *c, int x, int y, int w, int h, int interact) {
    resizeclient(c, x, y, w, h);
}

static void resizeclient(Client *c, int x, int y, int w, int h) {
	XWindowChanges wc;
	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = wc.height = h;
	wc.border_width = c->bw;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}

static void resizemouse(const Arg *arg) {
	int ocx, ocy, nw, nh;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel) || c->isfullscreen) return;
	
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
    
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurResize], CurrentTime) != GrabSuccess)
		return;
    
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);

	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		
		switch(ev.type) {
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60)) continue;
			lasttime = ev.xmotion.time;

			nw = MAX(ev.xmotion.x_root - ocx - 2 * c->bw + 1, 1);
			nh = MAX(ev.xmotion.y_root - ocy - 2 * c->bw + 1, 1);

			if (abs((selmon->wx + selmon->ww) - (ocx + nw + 2 * c->bw)) < snap)
				nw = selmon->wx + selmon->ww - ocx - 2 * c->bw;
			if (abs((selmon->wy + selmon->wh) - (ocy + nh + 2 * c->bw)) < snap)
				nh = selmon->wy + selmon->wh - ocy - 2 * c->bw;

            if (!c->isfloating && selmon->lt[selmon->sellt]->arrange && (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
				togglefloating(NULL);

			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, c->x, c->y, nw, nh, 1);
			break;
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			if (handler[ev.type]) handler[ev.type](&ev);
			break;
		}
	} while (ev.type != ButtonRelease);

	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	XUngrabPointer(dpy, CurrentTime);
	
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

static void restack(Monitor *m) {
	Client *c;
	XWindowChanges wc;

	drawbar(m);
	if (!m->sel) return;
    
	wc.stack_mode = (m->lt[m->sellt]->arrange && !m->sel->isfloating) ? Below : Above;
	wc.sibling = m->barwin;
	for (c = m->stack; c; c = c->snext)
		if (ISVISIBLE(c)) {
			XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
			wc.sibling = c->win;
		}
	XSync(dpy, False);
}

static void run(void) {
	XEvent ev;
	XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev))
		if (handler[ev.type])
			handler[ev.type](&ev);
}

static void setup(void) {
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);

	updategeom(); 
	
	cursor[CurNormal] = XCreateFontCursor(dpy, XC_left_ptr);
	cursor[CurResize] = XCreateFontCursor(dpy, XC_sizing);
	cursor[CurMove] = XCreateFontCursor(dpy, XC_fleur);
    
	XSetWindowAttributes wa;
	wa.cursor = cursor[CurNormal];
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
    
    updatebarpos(mons);
	Monitor *m = mons;
	m->barwin = XCreateSimpleWindow(dpy, root, m->wx, m->by, m->ww, bh, 0, getcolor(colors[SchemeNorm][0]), getcolor(colors[SchemeNorm][2]));
	XSelectInput(dpy, m->barwin, ExposureMask | ButtonPressMask);
	XMapWindow(dpy, m->barwin);
    
    gc = XCreateGC(dpy, m->barwin, 0, NULL);
    
	grabkeys();
	focus(NULL);
}

static void spawn(const Arg *arg) {
	struct sigaction sa;
	if (fork() == 0) {
		if (dpy) close(ConnectionNumber(dpy));
		setsid();
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;
		sigaction(SIGCHLD, &sa, NULL);
		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("execvp %s failed:", ((char **)arg->v)[0]);
	}
}

static void tile(Monitor *m) {
	unsigned int i, n, h, mw, my, ty;
	Client *c;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0) return;

	mw = m->nmaster ? m->ww * m->mfact : 0;
	h = m->wh / (nmaster > 0 ? MIN(n, m->nmaster) : n);
	my = ty = m->wy;

	for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < m->nmaster) {
			resize(c, m->wx, my, mw - 2 * c->bw, h - 2 * c->bw, 0);
			my += HEIGHT(c);
		} else {
			h = m->wh / (n - m->nmaster);
			resize(c, m->wx + mw, ty, m->ww - mw - 2 * c->bw, h - 2 * c->bw, 0);
			ty += HEIGHT(c);
		}
}

static void togglefloating(const Arg *arg) {
	if (!selmon->sel || selmon->sel->isfullscreen) return;
	
	selmon->sel->isfloating = !selmon->sel->isfloating;
	if (selmon->sel->isfloating)
		resize(selmon->sel, selmon->sel->oldx, selmon->sel->oldy, selmon->sel->oldw, selmon->sel->oldh, 0); 
        
	arrange(selmon);
}

static void view(const Arg *arg) {
	unsigned int tag_mask = arg->ui & TAGMASK;
	if (tag_mask == selmon->tagset[selmon->seltags]) return;
	
	selmon->seltags ^= 1;
	if (tag_mask) selmon->tagset[selmon->seltags] = tag_mask;
        
	focus(NULL);
	arrange(selmon);
}

static Client *wintoclient(Window w) {
	Monitor *m;
	Client *c;
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->win == w) return c;
	return NULL;
}

static Monitor *wintomon(Window w) {
	int x, y;
	Client *c;

	if (w == root && getrootptr(&x, &y)) return recttomon(x, y, 1, 1);
	for (Monitor *m = mons; m; m = m->next)
		if (w == m->barwin) return m;
	if ((c = wintoclient(w))) return c->mon;
	return selmon;
}

static int xerror(Display *dpy, XErrorEvent *ee) {
	if (ee->error_code == BadWindow || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch))
		return 0;
	fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee);
}

static void checkotherwm(void) {
	int xerrorstart(Display *dpy, XErrorEvent *ee);
	xerrorxlib = XSetErrorHandler(xerrorstart);
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}
int xerrorstart(Display *dpy, XErrorEvent *ee) { 
        die("dwm: another window manager is already running"); 
        return -1; 
}
static int xerrordummy(Display *dpy, XErrorEvent *ee) { return 0; }
static void cleanupmon(Monitor *mon) {}
static void configure(Client *c) {
	XConfigureEvent ce;
	ce.type = ConfigureNotify; ce.display = dpy; ce.event = c->win; ce.window = c->win;
	ce.x = c->x; ce.y = c->y; ce.width = c->w; ce.height = c->h; ce.border_width = c->bw;
	ce.above = None; ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}
static void configurenotify(XEvent *e) {
	Monitor *m; Client *c; XConfigureEvent *ev = &e->xconfigure; int dirty;
	if (ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width; sh = ev->height;
		if (updategeom() || dirty) {
			updatebarpos(NULL); drawbars();
			for (m = mons; m; m = m->next) {
				XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);
			}
			focus(NULL); arrange(NULL);
		}
	}
}
static void configurerequest(XEvent *e) {
	Client *c; Monitor *m; XConfigureRequestEvent *ev = &e->xconfigurerequest; XWindowChanges wc;
	if ((c = wintoclient(ev->window))) {
		if (ev->value_mask & CWBorderWidth) c->bw = ev->border_width;
		else if (c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) {
			m = c->mon; if (ev->value_mask & CWX) c->x = m->mx + ev->x;
			if (ev->value_mask & CWY) c->y = m->my + ev->y;
			if (ev->value_mask & CWWidth) c->w = ev->width;
			if (ev->value_mask & CWHeight) c->h = ev->height;
			if (ISVISIBLE(c)) XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		} else configure(c);
	} else {
		wc.x = ev->x; wc.y = ev->y; wc.width = ev->width; wc.height = ev->height;
		wc.border_width = ev->border_width; wc.sibling = ev->above; wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	} XSync(dpy, False);
}
static void destroynotify(XEvent *e) { Client *c; XDestroyWindowEvent *ev = &e->xdestroywindow; if ((c = wintoclient(ev->window))) unmanage(c, 1); }
static void detach(Client *c) { Client **tc; for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next); *tc = c->next; }
static void detachstack(Client *c) { Client **tc, *t; for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext); *tc = c->snext; if (c == c->mon->sel) { for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext); c->mon->sel = t; } }
static void enternotify(XEvent *e) {
	Client *c; Monitor *m; XCrossingEvent *ev = &e->xcrossing;
	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root) return;
	c = wintoclient(ev->window); m = c ? c->mon : wintomon(ev->window);
	if (m != selmon) { unfocus(selmon->sel, 1); selmon = m; } else if (!c || c == selmon->sel) return;
	focus(c);
}
static void expose(XEvent *e) { Monitor *m; XExposeEvent *ev = &e->xexpose; if (ev->count == 0 && (m = wintomon(ev->window))) drawbar(m); }
static Monitor *dirtomon(int dir) { Monitor *m = NULL; if (dir > 0) { if (!(m = selmon->next)) m = mons; } else if (selmon == mons) for (m = mons; m->next; m = m->next); else for (m = mons; m->next != selmon; m = m->next); return m; }
static void focusmon(const Arg *arg) { Monitor *m; if (!mons->next) return; if ((m = dirtomon(arg->i)) == selmon) return; unfocus(selmon->sel, 0); selmon = m; focus(NULL); }
static void focusstack(const Arg *arg) {
	Client *c = NULL, *i; if (!selmon->sel) return;
	if (arg->i > 0) { for (c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next); if (!c) for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next); }
	else { for (i = selmon->clients; i != selmon->sel; i = i->next) if (ISVISIBLE(i)) c = i; if (!c) for (; i; i = i->next) if (ISVISIBLE(i)) c = i; }
	if (c) { focus(c); restack(selmon); }
}
static void pop(Client *c) {
	void detach(Client *c); void attach(Client *c);
	detach(c); attach(c); focus(c); arrange(c->mon);
}
static Monitor *recttomon(int x, int y, int w, int h) {
	Monitor *m, *r = selmon;
	for (m = mons; m; m = m->next)
		if (x + w > m->mx && y + h > m->my && x < m->mx + m->mw && y < m->my + m->mh) r = m;
	return r;
}
static void sendmon(Client *c, Monitor *m) {
	if (c->mon == m) return;
	void detach(Client *c); void detachstack(Client *c); void attach(Client *c); void attachstack(Client *c);
	unfocus(c, 1); detach(c); detachstack(c); c->mon = m; c->tags = m->tagset[m->seltags]; attach(c); attachstack(c); focus(NULL); arrange(NULL);
}
static void setlayout(const Arg *arg) {
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt]) selmon->sellt ^= 1;
	if (arg && arg->v) selmon->lt[selmon->sellt] = (Layout *)arg->v;
	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol);
	if (selmon->sel) arrange(selmon); else drawbar(selmon);
}
static void tag(const Arg *arg) {
	unsigned int tag_mask = arg->ui & TAGMASK;
	if (!selmon->sel || tag_mask == selmon->sel->tags) return;
	selmon->sel->tags = tag_mask; focus(NULL); arrange(selmon);
}
static void tagmon(const Arg *arg) {
	if (!selmon->sel || !mons->next) return; sendmon(selmon->sel, dirtomon(arg->i));
}
static void toggleview(const Arg *arg) {
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);
	if (newtagset) { selmon->tagset[selmon->seltags] = newtagset; focus(NULL); arrange(selmon); }
}
static void toggletag(const Arg *arg) {
	unsigned int newtags;
	if (!selmon->sel) return;
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) { selmon->sel->tags = newtags; focus(NULL); arrange(selmon); }
}
static void unfocus(Client *c, int setfocus) {
	if (!c) return; grabbuttons(c, 0); XSetWindowBorder(dpy, c->win, getcolor(colors[SchemeNorm][0]));
	if (setfocus) { XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime); }
}
static void unmanage(Client *c, int destroyed) {
	Monitor *m = c->mon; XWindowChanges wc;
	void detach(Client *c); void detachstack(Client *c);
	detach(c); detachstack(c);
	if (!destroyed) {
		wc.border_width = c->oldbw; XGrabServer(dpy); XSetErrorHandler(xerrordummy);
		XSelectInput(dpy, c->win, NoEventMask); XConfigureWindow(dpy, c->win, CWBorderWidth, &wc);
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win); XSync(dpy, False);
		XSetErrorHandler(xerror); XUngrabServer(dpy);
	} free(c); focus(NULL); arrange(m);
}
static void unmapnotify(XEvent *e) {
	Client *c; XUnmapEvent *ev = &e->xunmap;
	if ((c = wintoclient(ev->window))) unmanage(c, 0);
}
static void updatebarpos(Monitor *m) {
    m = mons; if (!m) return;
    m->wy = m->my; m->wh = m->mh;
	if (m->showbar) {
		m->by = m->topbar ? m->wy : m->wy + m->wh - bh;
		m->wy = m->topbar ? m->wy + bh : m->wy;
		m->wh -= bh;
	} else { m->by = -bh; }
}
static int updategeom(void) {
    int dirty = 0;
	if (!mons) mons = createmon();
	if (mons->mw != sw || mons->mh != sh) {
		dirty = 1;
		mons->mw = mons->ww = sw; mons->mh = mons->wh = sh;
        updatebarpos(mons);
	} selmon = mons; return dirty;
}
static void updatenumlockmask(void) {
	unsigned int i, j; XModifierKeymap *modmap; numlockmask = 0; modmap = XGetModifierMapping(dpy);
	for (i = 0; i < 8; i++)
		for (j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j] == XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

int main(void)
{
	if (!(dpy = XOpenDisplay(NULL)))
		die("dwm: cannot open display");
    
    void checkotherwm(void);
    checkotherwm();

	handler[ButtonPress] = buttonpress;
	handler[ConfigureRequest] = configurerequest;
	handler[ConfigureNotify] = configurenotify;
	handler[DestroyNotify] = destroynotify;
	handler[EnterNotify] = enternotify;
	handler[Expose] = expose;
	handler[KeyPress] = keypress;
	handler[MapRequest] = maprequest;
	handler[UnmapNotify] = unmapnotify;
    
	setup();
    spawn(&(Arg){.v = termcmd});

    void cleanup(void);
	run();
	
	XCloseDisplay(dpy);
	return 0;
}