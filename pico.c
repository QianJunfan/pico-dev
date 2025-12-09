#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

enum mouse_mode {
	MOUSE_MODE_NONE,
	MOUSE_MODE_MOVE,
	MOUSE_MODE_RESIZE
};

struct rule {
	const char *class;
	const char *instance;
	const char *title;
	unsigned int tags;
	int isfloating;
	int isterminal;
};

union arg {
	const void *ptr;
};

struct key {
	uint32_t mod;
	KeySym keysym;
	void (*func)(const union arg *arg);
	const union arg arg;
};

struct mon;
struct tab;
struct doc;

struct cli {
	Window win;
	struct cli *next;
	struct cli *prev;
	struct mon *mon;
	struct tab *tab;
	int x, y, w, h;
	int til_x, til_y, til_w, til_h;
	int flt_x, flt_y, flt_w, flt_h;

	bool is_sel		: 1;
	bool is_foc		: 1;
	bool is_hide		: 1;
	bool is_unmap_by_wm	: 1;
	bool is_tile		: 1;
	bool is_float		: 1;
};

struct tab {
	uint64_t id;

	struct tab *next;
	struct tab *prev;

	struct mon *mon;

	uint64_t cli_cnt;
	struct cli *clis;
	struct cli *cli_sel;

	uint64_t cli_til_cnt;
	uint64_t til_cols, til_rows;
	struct cli **clis_til;

	uint64_t cli_flt_cnt;
	struct cli *clis_flt;
	bool is_sel		: 1;
};

struct doc {
	uint64_t cli_cnt;
	struct cli *clis;
	struct cli *cli_sel;

	bool is_hide : 1;
};

struct mon {
	uint64_t id;
	struct _XDisplay *display;

	struct mon *next;
	struct mon *prev;

	uint64_t tab_cnt;
	struct tab *tabs;
	struct tab *tab_sel;

	Window root;
	int x, y, w, h;

	bool is_size_change : 1;
};

static struct {
	struct mon *mon_sel;
	struct tab *tab_sel;
	struct cli *cli_sel;
	struct cli *cli_foc;
	struct cli *cli_mouse;
	struct doc doc;
	uint64_t mon_cnt;
	struct mon *mons;
	uint64_t arrange_type;

	enum mouse_mode mouse_mode;
    Display *dpy;
} runtime;

/* function prototypes */
void c_attach_t(struct cli *c, struct tab *t);
void c_attach_d(struct cli *c, struct doc *d);
void c_detach_t(struct cli *c);
void c_detach_d(struct cli *c);
void c_init(struct tab *t, uint64_t arrange);
void c_move(struct cli *c, int x, int y);
void c_resize(struct cli *c, int w, int h);
void c_raise(struct cli *c);
void c_sel(struct cli *c);
void c_unsel(struct cli *c);
void c_foc(struct cli *c);
void c_unfoc(struct cli *c);
void c_hide(struct cli *c);
void c_show(struct cli *c);
void c_tile(struct cli *c);
void c_float(struct cli *c);
void c_moveto_t(struct cli *c, struct tab *t);
void c_moveto_m(struct cli *c, struct mon *m);
void c_kill(struct cli *c);

void t_attach_m(struct tab *t, struct mon *m);
void t_detach_m(struct tab *t);
void t_move(struct tab *t, int d_offset);
void t_moveto_m(struct tab *t, struct mon *m);
void t_remove(struct tab *t);
void t_sel(struct tab *t);
void t_unsel(struct tab *t);

void m_attach(struct mon *m);
void m_detach(struct mon *m);
void m_destroy(struct mon *m);
void m_sel(struct mon *m);
void m_unsel(struct mon *m);
void m_update(struct mon *m);

void d_sel(struct cli *c);
void d_unsel(struct cli *c);
void setup(void);
void run(void);
void quit(void);

void spawn(const union arg *arg);
void killclient(const union arg *arg);
void toggle_float(const union arg *arg);
void quit_wm(const union arg *arg);

/* ************************************************************************* */
/* ** KEY BINDINGS AND FUNCTION IMPLEMENTATIONS ** */
/* ************************************************************************* */

#define XK_SHIFT	ShiftMask
#define XK_LOCK		LockMask
#define XK_CONTROL	ControlMask
#define XK_ALT		Mod1Mask
#define XK_NUM		Mod2Mask
#define XK_HYPER	Mod3Mask
#define XK_SUPER	Mod4Mask
#define XK_LOGO		Mod5Mask
#define XK_ANY		AnyModifier
#define MOUSE_MOD	XK_SUPER

static const char *termcmd[] = { "xterm", NULL };
static const char *browsercmd[] = { "firefox", NULL };

static const struct key keys[] = {
	{ XK_SUPER,   XK_Return,    spawn,      {.ptr = termcmd } },
	{ XK_SUPER,   XK_w,         spawn,      {.ptr = browsercmd } },
	{ XK_SUPER,   XK_c,         killclient, {0} },
	{ XK_SUPER,   XK_f,         toggle_float, {0} },
	{ XK_SUPER,   XK_q,         quit_wm,    {0} },
};

void spawn(const union arg *arg)
{
	if (fork() == 0) {
		if (runtime.dpy)
			close(ConnectionNumber(runtime.dpy));
		setsid();
		execvp(((char **)arg->ptr)[0], (char **)arg->ptr);
		fprintf(stderr, "pico: execvp failed for %s\n", ((char **)arg->ptr)[0]);
		exit(1);
	}
}

void killclient(const union arg *arg)
{
	if (!runtime.cli_sel)
		return;
	
	c_kill(runtime.cli_sel);
}

void toggle_float(const union arg *arg)
{
	if (!runtime.cli_sel)
		return;

	if (runtime.cli_sel->is_float)
		c_tile(runtime.cli_sel);
	else
		c_float(runtime.cli_sel);
}

void quit_wm(const union arg *arg)
{
	quit();
}


/* ************************************************************************* */
/* ** CORE EVENT HANDLERS AND SETUP/RUN/QUIT ** */
/* ************************************************************************* */

typedef void (*XEventHandler)(XEvent *);

#define LAST_EVENT_TYPE 35

static XEventHandler handler[LAST_EVENT_TYPE];

static struct cli *c_fetch(Window win)
{
	struct mon *m;
	struct tab *t;
	struct cli *c;
	struct doc *d = &runtime.doc;

	for (m = runtime.mons; m; m = m->next) {
		for (t = m->tabs; t; t = t->next) {
			for (c = t->clis; c; c = c->next) {
				if (c->win == win)
					return c;
			}
		}
	}

	for (c = d->clis; c; c = c->next) {
		if (c->win == win)
			return c;
	}

	return NULL;
}

static void c_attach_flt(struct cli *c, struct tab *t)
{
	c->tab = t;
	c->next = t->clis_flt;
	c->prev = NULL;

	if (t->clis_flt)
		t->clis_flt->prev = c;

	t->clis_flt = c;
	t->cli_flt_cnt++;
}

static void c_detach_flt(struct cli *c)
{
	struct tab *t = c->tab;

	if (c->prev)
		c->prev->next = c->next;
	if (c->next)
		c->next->prev = c->prev;

	if (t->clis_flt == c)
		t->clis_flt = c->next;

	t->cli_flt_cnt--;
	c->next = NULL;
	c->prev = NULL;
}

void c_attach_t(struct cli *c, struct tab *t)
{
	c->tab = t;
	c->mon = t->mon;

	c->next = t->clis;
	c->prev = NULL;

	if (t->clis)
		t->clis->prev = c;

	t->clis = c;
	t->cli_cnt++;
}

void c_attach_d(struct cli *c, struct doc *d)
{
	c->tab = NULL;
	c->mon = NULL;

	c->next = d->clis;
	c->prev = NULL;

	if (d->clis)
		d->clis->prev = c;

	d->clis = c;
	d->cli_cnt++;
}

void c_detach_t(struct cli *c)
{
	struct tab *t = c->tab;

	if (!t)
		return;

	if (c->is_float) {
		c_detach_flt(c);
	} else if (c->is_tile) {
		t->cli_til_cnt--;
	}

	if (c->prev)
		c->prev->next = c->next;
	if (c->next)
		c->next->prev = c->prev;

	if (t->clis == c)
		t->clis = c->next;

	if (t->cli_sel == c)
		t->cli_sel = NULL;
	if (runtime.cli_sel == c)
		runtime.cli_sel = NULL;
	if (runtime.cli_foc == c)
		runtime.cli_foc = NULL;

	t->cli_cnt--;
	c->tab = NULL;
	c->mon = NULL;
	c->next = NULL;
	c->prev = NULL;
}

void c_detach_d(struct cli *c)
{
	struct doc *d = &runtime.doc;

	if (c->prev)
		c->prev->next = c->next;
	if (c->next)
		c->next->prev = c->prev;

	if (d->clis == c)
		d->clis = c->next;

	if (d->cli_sel == c)
		d->cli_sel = NULL;
	if (runtime.cli_foc == c)
		runtime.cli_foc = NULL;


	d->cli_cnt--;
	c->next = NULL;
	c->prev = NULL;
}

void t_attach_m(struct tab *t, struct mon *m)
{
	t->mon = m;

	t->next = m->tabs;
	t->prev = NULL;

	if (m->tabs)
		m->tabs->prev = t;

	m->tabs = t;
	m->tab_cnt++;
}

void t_detach_m(struct tab *t)
{
	struct mon *m = t->mon;

	if (!m)
		return;

	if (t->prev)
		t->prev->next = t->next;
	if (t->next)
		t->next->prev = t->prev;

	if (m->tabs == t)
		m->tabs = t->next;

	if (m->tab_sel == t)
		m->tab_sel = NULL;
	if (runtime.tab_sel == t)
		runtime.tab_sel = NULL;

	m->tab_cnt--;
	t->mon = NULL;
	t->next = NULL;
	t->prev = NULL;
}

void m_attach(struct mon *m)
{
	m->next = runtime.mons;
	m->prev = NULL;

	if (runtime.mons)
		runtime.mons->prev = m;

	runtime.mons = m;
	runtime.mon_cnt++;
}

void m_detach(struct mon *m)
{
	if (m->prev)
		m->prev->next = m->next;
	if (m->next)
		m->next->prev = m->prev;

	if (runtime.mons == m)
		runtime.mons = m->next;

	if (runtime.mon_sel == m)
		runtime.mon_sel = NULL;

	runtime.mon_cnt--;
	m->next = NULL;
	m->prev = NULL;
}

void m_sel(struct mon *m)
{
	if (!m || m == runtime.mon_sel)
		return;

	if (runtime.mon_sel)
		m_unsel(runtime.mon_sel);

	runtime.mon_sel = m;

	if (m->tab_sel)
		t_sel(m->tab_sel);
}

void m_unsel(struct mon *m)
{
	if (m->tab_sel)
		t_unsel(m->tab_sel);
}

void c_move(struct cli *c, int x, int y)
{
	c->x = x;
	c->y = y;

	if (c->is_tile) {
		c->til_x = x;
		c->til_y = y;
	} else if (c->is_float) {
		c->flt_x = x;
		c->flt_y = y;
	}

	XMoveWindow(c->mon->display, c->win, c->x, c->y);
}

void c_resize(struct cli *c, int w, int h)
{
	c->w = w;
	c->h = h;

	if (c->is_tile) {
		c->til_w = w;
		c->til_h = h;
	} else if (c->is_float) {
		c->flt_w = w;
		c->flt_h = h;
	}

	XResizeWindow(c->mon->display, c->win, c->w, c->h);
}

void c_raise(struct cli *c)
{
	XRaiseWindow(c->mon->display, c->win);
}

void c_sel(struct cli *c)
{
	if (!c || c == runtime.cli_sel)
		return;

	if (runtime.cli_sel)
		c_unsel(runtime.cli_sel);

	runtime.cli_sel = c;
	c->is_sel = true;

	if (c->tab)
		t_sel(c->tab);

	if (c->win)
		XSetInputFocus(c->mon->display, c->win,
			RevertToPointerRoot, CurrentTime);

	c_raise(c);
}

void c_unsel(struct cli *c)
{
	if (!c || !c->is_sel)
		return;

	c->is_sel = false;
	/* TBD: draw unfocused border */
}

void c_foc(struct cli *c)
{
	if (!c || c == runtime.cli_foc)
		return;

	if (runtime.cli_foc)
		c_unfoc(runtime.cli_foc);

	runtime.cli_foc = c;
	c->is_foc = true;

	/* TBD: draw focus border */
}

void c_unfoc(struct cli *c)
{
	if (!c || !c->is_foc)
		return;

	c->is_foc = false;

	/* TBD: draw normal border */
}

void t_sel(struct tab *t)
{
	if (!t || t == runtime.tab_sel)
		return;

	if (runtime.tab_sel)
		t_unsel(runtime.tab_sel);

	runtime.tab_sel = t;
	t->is_sel = true;

	if (t->mon)
		m_sel(t->mon);

	if (t->cli_sel)
		c_sel(t->cli_sel);
}

void t_unsel(struct tab *t)
{
	if (!t || !t->is_sel)
		return;

	t->is_sel = false;

	if (t->cli_sel)
		c_unsel(t->cli_sel);
}

void d_sel(struct cli *c)
{
	struct doc *d = &runtime.doc;

	if (!c || c == d->cli_sel)
		return;

	if (d->cli_sel)
		d_unsel(d->cli_sel);

	d->cli_sel = c;
}

void d_unsel(struct cli *c)
{
	/* TBD: logic */
}

void c_hide(struct cli *c)
{
	if (!c || c->is_hide)
		return;

	c->is_unmap_by_wm = true;

	XUnmapWindow(c->mon->display, c->win);

	c_unsel(c);
}

void c_show(struct cli *c)
{
	if (!c || !c->is_hide)
		return;

	XMapWindow(c->mon->display, c->win);
	c->is_hide = false;
	c_sel(c);
}

void c_tile(struct cli *c)
{
	if (!c || c->is_tile)
		return;

	if (c->is_float) {
		c_detach_flt(c);
		c->is_float = false;
	}

	c->is_tile = true;
	c->tab->cli_til_cnt++;

	m_update(c->mon);
}

void c_float(struct cli *c)
{
	if (!c || c->is_float)
		return;

	if (c->is_tile) {
		c->is_tile = false;
		c->tab->cli_til_cnt--;
	}

	c->is_float = true;
	c_attach_flt(c, c->tab);

	c_move(c, c->flt_x, c->flt_y);
	c_resize(c, c->flt_w, c->flt_h);
	c_raise(c);
}

void c_moveto_t(struct cli *c, struct tab *t)
{
	struct mon *m_old = c->mon;

	if (!c || !t || c->tab == t)
		return;

	c_detach_t(c);
	c_attach_t(c, t);

	m_update(m_old);
	m_update(t->mon);

	t_sel(t);
}

void c_moveto_m(struct cli *c, struct mon *m)
{
	if (!c || !m || c->mon == m)
		return;
	if (!m->tab_sel)
		return;

	c_moveto_t(c, m->tab_sel);
}

void c_kill(struct cli *c)
{
	struct mon *m_old = c->mon;

	/* TBD: Send WM_DELETE_WINDOW */

	c_detach_t(c);

	if (c->win)
		XDestroyWindow(c->mon->display, c->win);

	free(c);

	m_update(m_old);

	if (runtime.mon_sel)
		m_sel(runtime.mon_sel);
}

void c_init(struct tab *t, uint64_t arrange)
{
	struct cli *c;

	if (!t)
		return;

	if (!(c = calloc(1, sizeof(*c))))
		return;

	c->x = c->flt_x = 0;
	c->y = c->flt_y = 0;
	c->w = c->flt_w = t->mon->w / 2;
	c->h = c->flt_h = t->mon->h / 2;

	c_attach_t(c, t);

	if (arrange == 1) {
		c_float(c);
	} else {
		c_tile(c);
	}

	c_sel(c);
}

struct tab *t_init(struct mon *m)
{
	struct tab *t;

	if (!m)
		return NULL;

	if (!(t = calloc(1, sizeof(*t)))) {
		fprintf(stderr, "Error: Failed to allocate memory for new tab.\n");
		return NULL;
	}

	t->id = (uint64_t)t;
	t->cli_cnt = 0;
	t->cli_til_cnt = 0;
	t->is_sel = false;
	
	t_attach_m(t, m);
	
	return t;
}

void t_move(struct tab *t, int d_offset)
{
	struct mon *m = t->mon;
	struct tab *t_target;

	if (!m || m->tab_cnt < 2)
		return;

	t_target = NULL;
	if (d_offset < 0) {
		t_target = t->prev ? t->prev : m->tabs;
	} else if (d_offset > 0) {
		t_target = t->next ? t->next : m->tabs;
	}

	if (!t_target || t == t_target)
		return;

	t_detach_m(t);
	t->mon = m;

	t->next = t_target->next;
	t->prev = t_target;

	if (t_target->next)
		t_target->next->prev = t;

	t_target->next = t;

	if (m->tabs == t_target)
		m->tabs = t;
}

void t_moveto_m(struct tab *t, struct mon *m_target)
{
	struct mon *m_old = t->mon;
	struct cli *c;

	if (!t || !m_target || t->mon == m_target)
		return;

	t_detach_m(t);
	t_attach_m(t, m_target);

	for (c = t->clis; c; c = c->next)
		c->mon = m_target;

	m_update(m_old);
	t_sel(t);
	m_update(m_target);
}

void t_remove(struct tab *t)
{
	struct mon *m = t->mon;
	struct cli *c, *next_c;
	struct tab *t_fallback;

	if (!m)
		return;

	t_fallback = t->next ? t->next : t->prev;

	c = t->clis;
	while (c) {
		next_c = c->next;
		if (t_fallback) {
			c_moveto_t(c, t_fallback);
		} else {
			c_kill(c);
		}
		c = next_c;
	}

	t_detach_m(t);

	free(t);

	if (t_fallback)
		t_sel(t_fallback);
	else
		m_update(m);
}

void m_init(Display *dpy, Window root, int x, int y, int w, int h)
{
	struct mon *m;
	struct tab *t;

	if (!(m = calloc(1, sizeof(*m)))) {
		fprintf(stderr, "Error: Failed to allocate memory for new monitor.\n");
		return;
	}

	m->id = (uint64_t)m;
	m->display = dpy;
	m->root = root;
	m->x = x;
	m->y = y;
	m->w = w;
	m->h = h;
	m->tab_cnt = 0;
	m->is_size_change = false;

	m_attach(m);
	
	if (!(t = t_init(m))) {
		m_detach(m); 
		free(m);
		return; 
	}

	m->tab_sel = t;

	if (runtime.mon_cnt == 1) {
		m_sel(m);
	}
}

void m_destroy(struct mon *m)
{
	struct mon *m_fallback;
	struct tab *t, *next_t;

	if (!m)
		return;

	m_fallback = m->next ? m->next : m->prev;

	if (m->tab_cnt > 0 && !m_fallback)
		return;

	t = m->tabs;
	while (t) {
		next_t = t->next;
		if (m_fallback) {
			t_moveto_m(t, m_fallback);
		}
		t = next_t;
	}

	m_detach(m);
	free(m);

	if (m_fallback)
		m_sel(m_fallback);
}

void m_update(struct mon *m)
{
	struct tab *t;
	struct cli *c, *master, *stack;
	uint64_t i;
	int x, y, w, h;
	int master_w, stack_w, stack_h;
	int gap = 0;

	if (!m)
		return;

	t = m->tab_sel;
	if (!t)
		return;

	for (c = t->clis_flt; c; c = c->next) {
		if (c->is_hide)
			c_show(c);
		c_raise(c);
	}

	if (t->cli_til_cnt == 0)
		goto end;

	master = t->clis;
	i = 0;
	while (master && !master->is_tile) {
		master = master->next;
		i++;
	}

	if (!master)
		goto end;

	x = m->x + gap;
	y = m->y + gap;
	w = m->w - 2 * gap;
	h = m->h - 2 * gap;

	if (t->cli_til_cnt == 1) {
		c_move(master, x, y);
		c_resize(master, w, h);
		goto show;
	}

	master_w = w * 55 / 100;
	stack_w = w - master_w - gap;

	c_move(master, x, y);
	c_resize(master, master_w - gap, h);

	x += master_w + gap;
	stack = master->next;
	stack_h = h / (t->cli_til_cnt - 1);
	i = 0;

	while (stack) {
		if (stack->is_tile) {
			c_move(stack, x, y + i * stack_h);
			c_resize(stack, stack_w, stack_h - gap);
			i++;
		}
		stack = stack->next;
	}

show:
	stack = t->clis;
	while (stack) {
		if (stack->is_tile && stack->is_hide)
			c_show(stack);
		stack = stack->next;
	}

end:
	if (!t->cli_sel && t->clis)
		c_sel(t->clis);
}

static KeyCode key_get(KeySym keysym)
{
	struct mon *m = runtime.mons;

	if (!m)
		return 0;

	return XKeysymToKeycode(m->display, keysym);
}

static void key_grab(void)
{
	struct mon *m = runtime.mons;
	unsigned int i;
	KeyCode code;

	if (!m)
		return;

	XUngrabKey(m->display, AnyKey, AnyModifier, m->root);

	for (i = 0; i < sizeof(keys) / sizeof(*keys); i++) {
		code = key_get(keys[i].keysym);
		if (code) {
			XGrabKey(m->display, code, keys[i].mod, m->root,
				True, GrabModeAsync, GrabModeAsync);
		}
	}
}

static void key_handle(XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	unsigned int i;
	KeySym keysym = XKeycodeToKeysym(ev->display, (KeyCode)ev->keycode, 0);

	for (i = 0; i < sizeof(keys) / sizeof(*keys); i++) {
		if (keysym == keys[i].keysym && ev->state == keys[i].mod) {
			keys[i].func(&keys[i].arg);
			return;
		}
	}
}

static void handle_maprequest(XEvent *e)
{
	XMapRequestEvent *ev = &e->xmaprequest;
	struct tab *t = runtime.tab_sel;
	struct cli *c;
	XWindowAttributes wa;
	unsigned int depth;
	Window trans = None;
	XWMHints *wmh;

	if (!t || !t->mon->display || c_fetch(ev->window))
		return;

	if (!XGetWindowAttributes(t->mon->display, ev->window, &wa))
		return;

	if (wa.override_redirect)
		return;

	c = calloc(1, sizeof(*c));
	if (!c)
		return;

	c->win = ev->window;

	XGetGeometry(t->mon->display, c->win, &wa.root,
		&c->x, &c->y, &c->w, &c->h,
		&wa.border_width, &depth);

	c->flt_x = c->x;
	c->flt_y = c->y;
	c->flt_w = c->w;
	c->flt_h = c->h;

	c->is_float = (runtime.arrange_type == 1);

	if (XGetTransientForHint(t->mon->display, c->win, &trans) && trans != None) {
		c->is_float = true;
	}

	if ((wmh = XGetWMHints(t->mon->display, c->win)))
		XFree(wmh);

	c_attach_t(c, t);

	XSelectInput(c->mon->display, c->win,
		EnterWindowMask | FocusChangeMask);

	if (c->is_float)
		c_float(c);
	else
		c_tile(c);

	XMapWindow(c->mon->display, c->win);
	c_sel(c);
	m_update(t->mon);
}

static void handle_destroynotify(XEvent *e)
{
	XDestroyWindowEvent *ev = &e->xdestroywindow;
	struct cli *c;
	struct mon *m_old;

	if (!(c = c_fetch(ev->window)))
		return;

	m_old = c->mon;

	if (c->tab) {
		c_detach_t(c);
	} else {
		c_detach_d(c);
	}

	free(c);

	if (m_old)
		m_update(m_old);
	else
		m_sel(runtime.mon_sel);
}

static void handle_enternotify(XEvent *e)
{
	XCrossingEvent *ev = &e->xcrossing;
	struct cli *c;

	if (ev->mode != NotifyNormal || ev->detail == NotifyInferior)
		return;

	if (!(c = c_fetch(ev->window)))
		return;

	c_foc(c);
	c_sel(c);
}

static void handle_buttonpress(XEvent *e)
{
	XButtonEvent *ev = &e->xbutton;
	struct cli *c;
	Display *dpy = ev->display;

	if (!(c = c_fetch(ev->window)))
		return;

	c_sel(c);

	if (!c->is_float || ev->state != MOUSE_MOD)
		return;

	if (runtime.mouse_mode != MOUSE_MODE_NONE)
		return;

	runtime.cli_mouse = c;
	c_raise(c);

	if (ev->button == Button1) {
		runtime.mouse_mode = MOUSE_MODE_MOVE;

		XGrabPointer(dpy, c->win, False,
			     ButtonMotionMask | ButtonReleaseMask,
			     GrabModeAsync, GrabModeAsync,
			     None, None, CurrentTime);

		XWarpPointer(dpy, None, c->win, 0, 0, 0, 0,
			     c->w / 2, c->h / 2);

	} else if (ev->button == Button3) {
		runtime.mouse_mode = MOUSE_MODE_RESIZE;

		XGrabPointer(dpy, c->win, False,
			     ButtonMotionMask | ButtonReleaseMask,
			     GrabModeAsync, GrabModeAsync,
			     None, None, CurrentTime);

		XWarpPointer(dpy, None, c->win, 0, 0, 0, 0,
			     c->w, c->h);
	}
}

static void handle_motionnotify(XEvent *e)
{
	XMotionEvent *ev = &e->xmotion;
	struct cli *c = runtime.cli_mouse;
	int dx, dy;
	int new_w, new_h;
	int min_size = 50;

	if (runtime.mouse_mode == MOUSE_MODE_NONE || !c)
		return;

	dx = ev->x;
	dy = ev->y;

	switch (runtime.mouse_mode) {
	case MOUSE_MODE_MOVE:
		c_move(c, c->x + dx - (c->w / 2),
			  c->y + dy - (c->h / 2));
		break;

	case MOUSE_MODE_RESIZE:
		new_w = c->w + dx;
		new_h = c->h + dy;

		if (new_w < min_size)
			new_w = min_size;
		if (new_h < min_size)
			new_h = min_size;

		c_resize(c, new_w, new_h);
		break;

	default:
		break;
	}

	XSync(ev->display, False);
}

static void handle_buttonrelease(XEvent *e)
{
	XButtonEvent *ev = &e->xbutton;
	Display *dpy = ev->display;

	if (runtime.mouse_mode == MOUSE_MODE_NONE)
		return;

	XUngrabPointer(dpy, CurrentTime);

	runtime.mouse_mode = MOUSE_MODE_NONE;
	runtime.cli_mouse = NULL;

	if (runtime.mon_sel)
		m_update(runtime.mon_sel);

	if (runtime.cli_sel)
		c_sel(runtime.cli_sel);
}

static void handle_configurerequest(XEvent *e)
{
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;
	struct cli *c;

	if (!(c = c_fetch(ev->window))) {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = 0;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(ev->display, ev->window, ev->value_mask, &wc);
		return;
	}

	if (c->is_float) {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = 0;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;

		if (ev->value_mask & CWX) c->flt_x = ev->x;
		if (ev->value_mask & CWY) c->flt_y = ev->y;
		if (ev->value_mask & CWWidth) c->flt_w = ev->width;
		if (ev->value_mask & CWHeight) c->flt_h = ev->height;

		XConfigureWindow(ev->display, ev->window, ev->value_mask, &wc);

	} else {
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;

		wc.x = c->x;
		wc.y = c->y;
		wc.width = c->w;
		wc.height = c->h;
		wc.border_width = 0;

		ev->value_mask &= ~CWX;
		ev->value_mask &= ~CWY;
		ev->value_mask &= ~CWWidth;
		ev->value_mask &= ~CWHeight;

		XConfigureWindow(ev->display, ev->window, ev->value_mask, &wc);
	}
}

static void handle_unmapnotify(XEvent *e)
{
	XUnmapEvent *ev = &e->xunmap;
	struct cli *c;
	struct mon *m_old;

	if (!(c = c_fetch(ev->window)))
		return;

	m_old = c->mon;

	if (c->is_unmap_by_wm) {
		c->is_hide = true;
		c->is_sel = false;
		c->is_unmap_by_wm = false;

		if (m_old)
			m_update(m_old);

	} else {
		if (c->tab) {
			c_detach_t(c);
		} else {
			c_detach_d(c);
		}

		free(c);

		if (m_old)
			m_update(m_old);
		else if (runtime.mon_sel)
			m_sel(runtime.mon_sel);
	}
}

static void handle_mapnotify(XEvent *e)
{
	XMapEvent *ev = &e->xmap;
	struct cli *c;
	struct tab *t;

	if (!(c = c_fetch(ev->window)))
		return;

	if (ev->override_redirect)
		return;

	if (c->is_hide) {
		c->is_hide = false;

		if (c->tab) {
			t = c->tab;

			if (t == runtime.tab_sel) {
				if (c->is_float)
					c_raise(c);

				c_sel(c);
				m_update(c->mon);
			} else {
				t_sel(t);
			}
		}
	}
}

void handle_init(void)
{
	int i;

	for (i = 0; i < LAST_EVENT_TYPE; i++)
		handler[i] = NULL;

	handler[KeyPress]	= key_handle;
	handler[ButtonPress]	= handle_buttonpress;
	handler[ButtonRelease]	= handle_buttonrelease;
	handler[MotionNotify]	= handle_motionnotify;
	handler[MapRequest]	= handle_maprequest;
	handler[DestroyNotify]	= handle_destroynotify;
	handler[UnmapNotify]	= handle_unmapnotify;
	handler[MapNotify]	= handle_mapnotify;
	handler[EnterNotify]	= handle_enternotify;
	handler[ConfigureRequest] = handle_configurerequest;
}

void setup(void)
{
	Screen *s;
	int i, screen_count;

	runtime.dpy = XOpenDisplay(NULL);
	if (!runtime.dpy) {
		fprintf(stderr, "fatal: cannot open display\n");
		exit(1);
	}

	handle_init();

	screen_count = XScreenCount(runtime.dpy);

	for (i = 0; i < screen_count; i++) {
		s = XScreenOfDisplay(runtime.dpy, i);
		Window root = RootWindowOfScreen(s);
		int x = 0;
		int y = 0;
		int w = s->width;
		int h = s->height;

		XSelectInput(runtime.dpy, root, 
			SubstructureRedirectMask | SubstructureNotifyMask | 
			KeyPressMask | ButtonPressMask | EnterWindowMask);
		
		m_init(runtime.dpy, root, x, y, w, h);
	}
	
	key_grab();
	
	XSync(runtime.dpy, False);
}

void run(void)
{
	XEvent ev;

	while (1) {
		XNextEvent(runtime.dpy, &ev); 

                if (handler[ev.type])
                        handler[ev.type](&ev);
	}
}

void quit(void)
{
	struct mon *m;
	
	for (m = runtime.mons; m; m = m->next) {
		XUngrabKey(m->display, AnyKey, AnyModifier, m->root);
		XSelectInput(m->display, m->root, 0); 
	}

	if (runtime.dpy)
		XCloseDisplay(runtime.dpy);

	exit(0);
}

int main(void)
{
        setup();
        run();
        quit();
	return 0;
}