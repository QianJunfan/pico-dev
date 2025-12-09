#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <stdarg.h> // for log_action

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
	int i;
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
	int x, y;
        unsigned int w, h;
	int til_x, til_y; 
        unsigned int til_w, til_h;
	int flt_x, flt_y;
        unsigned int flt_w, flt_h;
        int drag_x, drag_y; 
        unsigned int drag_w, drag_h;
        int drag_root_x, drag_root_y;
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
	struct cli **clis_til; /* Array of tiled clients */

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
        Atom atom_protocols;
        Atom atom_delete_window;
    Display *dpy;
} runtime;

/* ************************************************************************* */
/* ** LOGGING AND ERROR HANDLING ** */
/* ************************************************************************* */

/**
 * @brief 打印操作日志到 stderr
 * * @param format 格式字符串
 * @param ... 变长参数
 */
static void log_action(const char *format, ...)
{
	va_list args;
	fprintf(stderr, "pico: Log: ");
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");
}

static int xerror(Display *dpy, XErrorEvent *ee)
{
    const char *request_name = "Unknown";
    
    // 增加请求代码到 Xlib 函数名的映射
    switch (ee->request_code) {
        case 12: request_name = "XChangeWindowAttributes/XSelectInput"; break; // Error 2 (BadValue)
        case 43: request_name = "XGrabKey"; break;                             // Error 8 (BadMatch)
        case 7:  request_name = "XGrabButton"; break;
        case 44: request_name = "XGrabButton (Ungrab)"; break; // Request 44 is UngrabKey/UngrabButton
        case 42: request_name = "XGrabKey"; break; // Request 42 is XGrabKey
        default: break; 
    }

	if (ee->error_code == BadAccess && ee->request_code == XChangeWindowAttributes
        && ee->resourceid == RootWindow(dpy, DefaultScreen(dpy)))
	{
		fprintf(stderr, "pico: fatal: another window manager is running\n");
		exit(1);
	}
    
    // 修复：使用更详细的打印格式，包含 ResourceID
	fprintf(stderr, "pico: fatal: unhandled X error %u (Code: %s/%u, Resource: %lu, Serial: %lu)\n", 
            ee->error_code, request_name, ee->request_code, ee->resourceid, ee->serial);
            
	return 0;
}
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
void view_next_tab(const union arg *arg);
void view_prev_tab(const union arg *arg);
void focus_next_cli(const union arg *arg);
void focus_prev_cli(const union arg *arg);

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

/* 用于忽略 NumLock 和 CapsLock 的掩码，用于修饰键状态比较 */
#define IGNORED_MODS (LockMask | Mod2Mask)
#define CLEANMASK(mask) ((mask) & ~IGNORED_MODS)

static const char *termcmd[] = { "xterm", NULL };
static const char *browsercmd[] = { "firefox", NULL };

static const struct key keys[] = {
	/* modifier,   keysym,      function,   argument */
	{ XK_SUPER,   XK_Return,    spawn,      {.ptr = termcmd } },
	{ XK_SUPER,   XK_w,         spawn,      {.ptr = browsercmd } },
	{ XK_SUPER,   XK_c,         killclient, {0} },
	{ XK_SUPER,   XK_f,         toggle_float, {0} },
	{ XK_SUPER,   XK_q,         quit_wm,    {0} },

	/* Tab navigation */
	{ XK_SUPER,   XK_Right,     view_next_tab,  {0} },
	{ XK_SUPER,   XK_Left,      view_prev_tab,  {0} },

	/* Client focus */
	{ XK_SUPER,   XK_j,         focus_next_cli, {0} },
	{ XK_SUPER,   XK_k,         focus_prev_cli, {0} },
};

void spawn(const union arg *arg)
{
	log_action("Spawn: %s", ((char **)arg->ptr)[0]);
	if (fork() == 0) {
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
	
	log_action("KillClient: window 0x%lx", runtime.cli_sel->win);
	c_kill(runtime.cli_sel);
}

void toggle_float(const union arg *arg)
{
	if (!runtime.cli_sel)
		return;

	log_action("ToggleFloat: window 0x%lx, current float: %d", 
        runtime.cli_sel->win, runtime.cli_sel->is_float);

	if (runtime.cli_sel->is_float)
		c_tile(runtime.cli_sel);
	else
		c_float(runtime.cli_sel);
}

void quit_wm(const union arg *arg)
{
	log_action("Quit WM command received");
	quit();
}

void view_next_tab(const union arg *arg)
{
	struct tab *t = runtime.tab_sel;
	if (!t || !t->mon)
		return;

	struct tab *next = t->next ? t->next : t->mon->tabs;
	if (next && next != t) {
		log_action("ViewNextTab: Switching from tab 0x%lx to 0x%lx", t->id, next->id);
		t_sel(next);
	}
}

void view_prev_tab(const union arg *arg)
{
	struct tab *t = runtime.tab_sel;
	if (!t || !t->mon)
		return;

	struct tab *prev = t->prev ? t->prev : t->mon->tabs;
	if (prev && prev != t) {
		log_action("ViewPrevTab: Switching from tab 0x%lx to 0x%lx", t->id, prev->id);
		t_sel(prev);
	}
}

void focus_next_cli(const union arg *arg)
{
	struct cli *c = runtime.cli_sel;
	if (!c || !c->tab)
		return;

	struct cli *next = c->next ? c->next : c->tab->clis;

	if (next && next != c) {
		log_action("FocusNextCli: Focusing client 0x%lx", next->win);
		c_sel(next);
	}
}

void focus_prev_cli(const union arg *arg)
{
	struct cli *c = runtime.cli_sel;
	if (!c || !c->tab)
		return;

	struct cli *prev;

	if (c->prev) {
		prev = c->prev;
	} else {
		prev = c->tab->clis;
		while (prev && prev->next)
			prev = prev->next;
	}

	if (prev && prev != c) {
		log_action("FocusPrevCli: Focusing client 0x%lx", prev->win);
		c_sel(prev);
	}
}

/* ************************************************************************* */
/* ** CORE EVENT HANDLERS AND SETUP/RUN/QUIT ** */
/* ************************************************************************* */

typedef void (*XEventHandler)(XEvent *);

#define LAST_EVENT_TYPE 35

static XEventHandler handler[LAST_EVENT_TYPE];

/* Helper function for tiled client array management */
static void c_til_append(struct cli *c, struct tab *t)
{
	c->tab = t;
	t->cli_til_cnt++;
	t->clis_til = realloc(t->clis_til, t->cli_til_cnt * sizeof(struct cli *));
	t->clis_til[t->cli_til_cnt - 1] = c;
	log_action("Client 0x%lx attached as tiled to tab 0x%lx", c->win, t->id);
}

static void c_til_remove(struct cli *c)
{
	struct tab *t = c->tab;
	uint64_t i;
	
	if (!t || !t->clis_til)
		return;

	for (i = 0; i < t->cli_til_cnt; i++) {
		if (t->clis_til[i] == c) {
			t->cli_til_cnt--;
			if (t->cli_til_cnt > 0) {
				for (; i < t->cli_til_cnt; i++)
					t->clis_til[i] = t->clis_til[i + 1];
				t->clis_til = realloc(t->clis_til, t->cli_til_cnt * sizeof(struct cli *));
			} else {
				free(t->clis_til);
				t->clis_til = NULL;
			}
			log_action("Client 0x%lx removed from tiled list of tab 0x%lx", c->win, t->id);
			return;
		}
	}
}

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
	log_action("Client 0x%lx attached as floating to tab 0x%lx", c->win, t->id);
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
	log_action("Client 0x%lx detached from floating list of tab 0x%lx", c->win, t->id);
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
	log_action("Client 0x%lx attached to tab 0x%lx (general list)", c->win, t->id);
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
	log_action("Client 0x%lx attached to document list", c->win);
}

void c_detach_t(struct cli *c)
{
	struct tab *t = c->tab;

	if (!t)
		return;

	if (c->is_float) {
		c_detach_flt(c);
	} else if (c->is_tile) {
		c_til_remove(c);
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
	log_action("Client 0x%lx detached from tab 0x%lx", c->win, t->id);
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
	log_action("Client 0x%lx detached from document list", c->win);
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
	log_action("Tab 0x%lx attached to monitor 0x%lx", t->id, m->id);
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
	log_action("Tab 0x%lx detached from monitor 0x%lx", t->id, m->id);
}

void m_attach(struct mon *m)
{
	m->next = runtime.mons;
	m->prev = NULL;

	if (runtime.mons)
		runtime.mons->prev = m;

	runtime.mons = m;
	runtime.mon_cnt++;
	log_action("Monitor 0x%lx attached. Count: %lu", m->id, runtime.mon_cnt);
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
	log_action("Monitor 0x%lx detached. Count: %lu", m->id, runtime.mon_cnt);
}

void m_sel(struct mon *m)
{
	if (!m || m == runtime.mon_sel)
		return;

	log_action("Monitor select: 0x%lx", m->id);
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
	log_action("Client 0x%lx move: %d, %d", c->win, x, y);

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
	log_action("Client 0x%lx resize: %d x %d", c->win, w, h);

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
	log_action("Client 0x%lx raise", c->win);
	XRaiseWindow(c->mon->display, c->win);
}

void c_sel(struct cli *c)
{
	if (!c || c == runtime.cli_sel)
		return;

	log_action("Client select: 0x%lx", c->win);

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

	log_action("Client unselect: 0x%lx", c->win);
	c->is_sel = false;
	/* TBD: draw unfocused border */
}

void c_foc(struct cli *c)
{
	if (!c || c == runtime.cli_foc)
		return;

	log_action("Client focus: 0x%lx", c->win);

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

	log_action("Client unfocus: 0x%lx", c->win);
	c->is_foc = false;

	/* TBD: draw normal border */
}

void t_sel(struct tab *t)
{
	if (!t || t == runtime.tab_sel)
		return;

	log_action("Tab select: 0x%lx", t->id);

	if (runtime.tab_sel)
		t_unsel(runtime.tab_sel);

	runtime.tab_sel = t;
	t->is_sel = true;

	if (t->mon)
		m_sel(t->mon);

	/* TBD: Select the first client if none is selected */
	if (!t->cli_sel && t->clis) {
		c_sel(t->clis);
	} else if (t->cli_sel) {
		c_sel(t->cli_sel);
	}
	
	m_update(t->mon);
}

void t_unsel(struct tab *t)
{
	if (!t || !t->is_sel)
		return;

	log_action("Tab unselect: 0x%lx", t->id);
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
	log_action("Document client select: 0x%lx", c->win);
}

void d_unsel(struct cli *c)
{
	/* TBD: logic */
}

void c_hide(struct cli *c)
{
	if (!c || c->is_hide)
		return;

	log_action("Client 0x%lx hide (unmap by wm)", c->win);
	c->is_unmap_by_wm = true;

	XUnmapWindow(c->mon->display, c->win);

	c_unsel(c);
}

void c_show(struct cli *c)
{
	if (!c || !c->is_hide)
		return;

	log_action("Client 0x%lx show (map)", c->win);
	XMapWindow(c->mon->display, c->win);
	c->is_hide = false;
	c_sel(c);
}

void c_tile(struct cli *c)
{
	if (!c || c->is_tile)
		return;

	log_action("Client 0x%lx to TILE mode", c->win);

	if (c->is_float) {
		c_detach_flt(c);
		c->is_float = false;
	}

	c->is_tile = true;
	c_til_append(c, c->tab);

	c_move(c, c->til_x, c->til_y);
	c_resize(c, c->til_w, c->til_h);

	m_update(c->mon);
}

void c_float(struct cli *c)
{
	if (!c || c->is_float)
		return;

	log_action("Client 0x%lx to FLOAT mode", c->win);

	if (c->is_tile) {
		c->is_tile = false;
		c_til_remove(c);
	}

	c->is_float = true;
	c_attach_flt(c, c->tab);

	c_move(c, c->flt_x, c->flt_y);
	c_resize(c, c->flt_w, c->flt_h);
	c_raise(c);

	m_update(c->mon);
}

void c_moveto_t(struct cli *c, struct tab *t)
{
	struct mon *m_old = c->mon;

	if (!c || !t || c->tab == t)
		return;

	log_action("Client 0x%lx move to tab 0x%lx", c->win, t->id);

	if (c->is_tile)
		c_til_remove(c);
	if (c->is_float)
		c_detach_flt(c);

	c_detach_t(c);
	c_attach_t(c, t);

	if (c->is_float)
		c_attach_flt(c, t);
	else
		c_til_append(c, t);

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

	log_action("Client 0x%lx move to monitor 0x%lx", c->win, m->id);
	c_moveto_t(c, m->tab_sel);
}
void c_kill(struct cli *c)
{
        struct mon *m_old = c->mon;
        int n;
        Atom *protocols;
        bool supports_delete = false;
        
        if (!c || !c->win || !c->mon)
                return;
        
        // 修复：在 detach 之前保存 display 指针
        Display *dpy = c->mon->display; 
        log_action("Attempting to kill client 0x%lx", c->win);

        if (XGetWMProtocols(c->mon->display, c->win, &protocols, &n))
        {
                for (int i = 0; i < n; i++)
                {
                        if (protocols[i] == runtime.atom_delete_window)
                        {
                                supports_delete = true;
                                break;
                        }
                }
                XFree(protocols);
        }

        if (supports_delete)
        {
                log_action("Client 0x%lx supports WM_DELETE_WINDOW, sending message", c->win);
                XEvent ev;
                ev.type = ClientMessage;
                ev.xclient.window = c->win;
                ev.xclient.message_type = runtime.atom_protocols;
                ev.xclient.format = 32;
                ev.xclient.data.l[0] = runtime.atom_delete_window;
                ev.xclient.data.l[1] = CurrentTime;

                XSendEvent(c->mon->display, c->win, False, NoEventMask, &ev);

                return;
        }

        log_action("Client 0x%lx does not support WM_DELETE_WINDOW, destroying window", c->win);
        c_detach_t(c);

        // 修复：使用保存的 dpy 指针
        if (c->win)
                XDestroyWindow(dpy, c->win); 

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
	log_action("New client 0x%lx initialized on tab 0x%lx", c->win, t->id);
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
	t->clis_til = NULL;
	
	t_attach_m(t, m);
	log_action("New tab 0x%lx initialized on monitor 0x%lx", t->id, m->id);
	
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

	log_action("Tab 0x%lx move operation (offset: %d)", t->id, d_offset);

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

	log_action("Tab 0x%lx move to monitor 0x%lx", t->id, m_target->id);

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

	log_action("Tab 0x%lx remove operation", t->id);

	t_fallback = t->next ? t->next : t->prev;

	c = t->clis;
	while (c) {
		next_c = c->next;
		if (t_fallback) {
			log_action("  Moving client 0x%lx to fallback tab 0x%lx", c->win, t_fallback->id);
			c_moveto_t(c, t_fallback);
		} else {
			log_action("  Killing client 0x%lx (no fallback tab)", c->win);
			c_kill(c);
		}
		c = next_c;
	}

	t_detach_m(t);

	free(t->clis_til);
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
	log_action("Monitor 0x%lx initialized: %dx%d @ %d,%d", m->id, w, h, x, y);
	
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

	log_action("Monitor 0x%lx destroy operation", m->id);

	t = m->tabs;
	while (t) {
		next_t = t->next;
		if (m_fallback) {
			log_action("  Moving tab 0x%lx to fallback monitor 0x%lx", t->id, m_fallback->id);
			t_moveto_m(t, m_fallback);
		}
		t = next_t;
	}

	m_detach(m);
	free(m);

	if (m_fallback)
		m_sel(m_fallback);
}

/* Master-Stack Tiling Layout Implementation */
void m_update(struct mon *m)
{
	struct tab *t;
	struct cli *c, *master;
	uint64_t i;
	int x, y, w, h;
	int master_w, stack_w, stack_h;
	int gap = 0;
	uint64_t n_til;

	if (!m)
		return;

	t = m->tab_sel;
	if (!t)
		return;

	log_action("Monitor 0x%lx update (layout)", m->id);
	n_til = t->cli_til_cnt;

	/* Raise and show all floating clients */
	for (c = t->clis_flt; c; c = c->next) {
		if (c->is_hide)
			c_show(c);
		c_raise(c);
	}

	if (n_til == 0)
		goto end;
	
	/* Master client is the first tiled client in the array */
	master = t->clis_til[0];

	x = m->x + gap;
	y = m->y + gap;
	w = m->w - 2 * gap;
	h = m->h - 2 * gap;

	/* If only one tiled client, make it fullscreen */
	if (n_til == 1) {
		c_move(master, x, y);
		c_resize(master, w, h);
		goto show_tiled;
	}

	/* Split into Master and Stack areas */
	master_w = w * 55 / 100;
	stack_w = w - master_w - gap;

	/* Master window */
	c_move(master, x, y);
	c_resize(master, master_w - gap, h);

	/* Stack windows */
	x += master_w + gap;
	stack_h = h / (n_til - 1);

	for (i = 1; i < n_til; i++) {
		c = t->clis_til[i];
		c_move(c, x, y + (i - 1) * stack_h);
		c_resize(c, stack_w, stack_h - gap);
	}

show_tiled:
	/* Show all tiled clients and raise the selected one */
	for (i = 0; i < n_til; i++) {
		c = t->clis_til[i];
		if (c->is_hide)
			c_show(c);
	}
	if (t->cli_sel && t->cli_sel->is_tile)
		c_raise(t->cli_sel);

end:
	/* Ensure a client is selected if available */
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
/* ************************************************************************* */
/* ** MOUSE GRAB ** */
/* ************************************************************************* */

static void mouse_grab(void)
{
	struct mon *m = runtime.mons;
    // NumLock 状态所需的 Mod 掩码： 0 (无锁) 和 XK_NUM (Mod2Mask)
    const unsigned int numlock_masks[] = { 0, XK_NUM }; 
    unsigned int j;
    
	if (!m)
		return;

    // 清除现有的 Button 抓取
    XUngrabButton(m->display, AnyButton, AnyModifier, m->root);

    // 遍历所有 NumLock 组合 (0 和 Mod2Mask)
    for (j = 0; j < sizeof(numlock_masks) / sizeof(*numlock_masks); j++) {
        // 抓取 Super + Button1 (移动)
        XGrabButton(m->display, Button1, MOUSE_MOD | numlock_masks[j], m->root,
                    True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                    GrabModeAsync, GrabModeAsync, None, None);

        // 抓取 Super + Button3 (缩放)
        XGrabButton(m->display, Button3, MOUSE_MOD | numlock_masks[j], m->root,
                    True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                    GrabModeAsync, GrabModeAsync, None, None);
    }
	log_action("Mouse grabs (Button1/3 + MOUSE_MOD) completed on root window");
}

static void key_grab(void)
{
	struct mon *m = runtime.mons;
	unsigned int i;
	KeyCode code;
    // NumLock 状态所需的 Mod 掩码： 0 (无锁) 和 XK_NUM (Mod2Mask)
    const unsigned int numlock_masks[] = { 0, XK_NUM }; 
    unsigned int j;

	if (!m)
		return;

	XUngrabKey(m->display, AnyKey, AnyModifier, m->root);

	for (i = 0; i < sizeof(keys) / sizeof(*keys); i++) {
		code = key_get(keys[i].keysym);
		
		if (code == 0) {
            // 打印警告，如果是无效 KeySym，跳过抓取，避免 BadMatch
            fprintf(stderr, "pico: Warning: KeySym %lu not mapped to a KeyCode. Skipping grab.\n", keys[i].keysym);
            continue;
        }
        
        // 修复：遍历所有 NumLock 组合，确保抓取成功
        for (j = 0; j < sizeof(numlock_masks) / sizeof(*numlock_masks); j++) {
            XGrabKey(m->display, code, keys[i].mod | numlock_masks[j], m->root,
                True, GrabModeAsync, GrabModeAsync);
        }
	}
	log_action("Key grabs completed");
}

static void key_handle(XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	unsigned int i;
	KeySym keysym = XKeycodeToKeysym(ev->display, (KeyCode)ev->keycode, 0);
	
	// 修复: 清理修饰键状态，忽略 LockMask 和 Mod2Mask (NumLock)
	uint32_t clean_state = CLEANMASK(ev->state);

	for (i = 0; i < sizeof(keys) / sizeof(*keys); i++) {
		if (keysym == keys[i].keysym && clean_state == keys[i].mod) {
			log_action("KeyPress: Mod 0x%x, KeySym %s, Function executed", clean_state, XKeysymToString(keysym));
			keys[i].func(&keys[i].arg);
			return;
		}
	}
	log_action("KeyPress: Mod 0x%x, KeySym %s, No matching binding found", clean_state, XKeysymToString(keysym));
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

	log_action("MapRequest for window 0x%lx", ev->window);

	if (!t || !t->mon || !t->mon->display || c_fetch(ev->window))
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
    
    // 初始化拖拽数据，防止后续拖拽逻辑读取未初始化内存
    c->drag_x = c->x;
    c->drag_y = c->y;
    c->drag_w = c->w;
    c->drag_h = c->h;
    c->drag_root_x = 0;
    c->drag_root_y = 0;

	c->is_float = (runtime.arrange_type == 1);

	if (XGetTransientForHint(t->mon->display, c->win, &trans) && trans != None) {
		c->is_float = true;
	}

	if ((wmh = XGetWMHints(t->mon->display, c->win)))
		XFree(wmh);

	// 确保 c_attach_t 在 XSelectInput 之前调用，以设置 c->mon
	c_attach_t(c, t); 

	// 🚀 核心修复：添加 ButtonPressMask，允许 WM 接收鼠标点击事件
	XSelectInput(c->mon->display, c->win,
		EnterWindowMask | FocusChangeMask | ButtonPressMask);

	if (c->is_float) {
		log_action("  Client is floating.");
		c_float(c);
	}
	else {
		log_action("  Client is tiled.");
		c_tile(c);
	}

	XMapWindow(c->mon->display, c->win);
	c_sel(c);
	m_update(t->mon);

    if (c->mon)
	    XSync(c->mon->display, False); 
}
static void handle_destroynotify(XEvent *e)
{
	XDestroyWindowEvent *ev = &e->xdestroywindow;
	struct cli *c;
	struct mon *m_old;

	log_action("DestroyNotify for window 0x%lx", ev->window);

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

	log_action("EnterNotify: client 0x%lx entered", c->win);
	c_foc(c);
	c_sel(c);
}
static void handle_buttonpress(XEvent *e)
{
	XButtonEvent *ev = &e->xbutton;
	struct cli *c;
	Display *dpy = ev->display;
    Window root; // 新增：用于存储根窗口
	
	log_action("ButtonPress: Button %u on window 0x%lx, state 0x%x", ev->button, ev->window, ev->state);

	if (!(c = c_fetch(ev->window)))
		return;

	c_sel(c);

	// 核心修复: 清理修饰键状态，忽略 LockMask 和 Mod2Mask (NumLock)
	uint32_t clean_state = CLEANMASK(ev->state);

	if (!c->is_float || clean_state != MOUSE_MOD)
		return;

	if (runtime.mouse_mode != MOUSE_MODE_NONE)
		return;

	runtime.cli_mouse = c;
	c_raise(c);
    
    // 确保 monitor 存在，并获取根窗口
    if (!c->mon) return; 
    root = c->mon->root; 

    // 修复：保存拖拽/缩放的初始状态和根窗口坐标
    c->drag_x = c->x;
    c->drag_y = c->y;
    c->drag_w = c->w;
    c->drag_h = c->h;
    c->drag_root_x = ev->x_root;
    c->drag_root_y = ev->y_root;

	if (ev->button == Button1) {
		runtime.mouse_mode = MOUSE_MODE_MOVE;
		log_action("  Starting MOVE mode for client 0x%lx", c->win);

		// 核心修复：在根窗口上抓取指针
		XGrabPointer(dpy, root, False, 
			     ButtonMotionMask | ButtonReleaseMask,
			     GrabModeAsync, GrabModeAsync,
			     None, None, CurrentTime);

	} else if (ev->button == Button3) {
		runtime.mouse_mode = MOUSE_MODE_RESIZE;
		log_action("  Starting RESIZE mode for client 0x%lx", c->win);

		// 核心修复：在根窗口上抓取指针
		XGrabPointer(dpy, root, False, 
			     ButtonMotionMask | ButtonReleaseMask,
			     GrabModeAsync, GrabModeAsync,
			     None, None, CurrentTime);
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

    // 修复 2：使用根窗口坐标的差值来计算位移 (dx, dy)
	dx = ev->x_root - c->drag_root_x;
	dy = ev->y_root - c->drag_root_y;

	switch (runtime.mouse_mode) {
	case MOUSE_MODE_MOVE:
        // 修复 2a：使用初始位置加上位移
		c_move(c, c->drag_x + dx, c->drag_y + dy);
		break;

	case MOUSE_MODE_RESIZE:
        // 修复 2b：使用初始尺寸加上位移
		new_w = c->drag_w + dx;
		new_h = c->drag_h + dy;

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
	
	log_action("ButtonRelease: Ending mouse mode %d", runtime.mouse_mode);

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
    
    // 如果找不到客户端，先配置窗口并返回
	if (!(c = c_fetch(ev->window))) {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = 0;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(ev->display, ev->window, ev->value_mask, &wc);
		log_action("ConfigureRequest: Window 0x%lx (unmanaged) configured", ev->window);
		return;
	}

	log_action("ConfigureRequest: Window 0x%lx (managed, float: %d)", c->win, c->is_float);

    // 复制客户端的当前几何图形
    wc.x = c->x;
    wc.y = c->y;
    wc.width = c->w;
    wc.height = c->h;
    wc.border_width = 0;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;

    // 如果是浮动窗口，允许客户端改变它的浮动几何图形
	if (c->is_float) {
        
        // 更新浮动尺寸/位置
		if (ev->value_mask & CWX) c->flt_x = ev->x;
		if (ev->value_mask & CWY) c->flt_y = ev->y;
		if (ev->value_mask & CWWidth) c->flt_w = ev->width;
		if (ev->value_mask & CWHeight) c->flt_h = ev->height;

        // 使用客户端请求的值来配置窗口
        wc.x = c->flt_x;
        wc.y = c->flt_y;
        wc.width = c->flt_w;
        wc.height = c->flt_h;
        
		XConfigureWindow(ev->display, ev->window, ev->value_mask, &wc);
		log_action("  Configuring as floating: %d,%d %dx%d", wc.x, wc.y, wc.width, wc.height);

	} else {
        // 如果是平铺窗口，忽略客户端请求的几何图形，使用 WM 决定的几何图形来配置窗口
        
        // 确保使用 WM 决定的几何图形
        wc.x = c->x;
        wc.y = c->y;
        wc.width = c->w;
        wc.height = c->h;
        
        // 修复：发送 ConfigureNotify 告知客户端其真实的几何图形
		XConfigureWindow(ev->display, ev->window, 
            CWX | CWY | CWWidth | CWHeight | CWBorderWidth, &wc);
		log_action("  Configuring as tiled: %d,%d %dx%d (ignoring client request)", wc.x, wc.y, wc.width, wc.height);
	}
}

static void handle_unmapnotify(XEvent *e)
{
	XUnmapEvent *ev = &e->xunmap;
	struct cli *c;
	struct mon *m_old;

	log_action("UnmapNotify for window 0x%lx (SendEvent: %d)", ev->window, ev->send_event);

	if (!(c = c_fetch(ev->window)))
		return;

	m_old = c->mon;

	if (c->is_unmap_by_wm) {
		log_action("  Unmap caused by WM (hiding client)");
		c->is_hide = true;
		c->is_sel = false;
		c->is_unmap_by_wm = false;

		if (m_old)
			m_update(m_old);

	} else {
		log_action("  Unmap caused by client (destroy/hide)");
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
	
	log_action("MapNotify for window 0x%lx", ev->window);

	if (!(c = c_fetch(ev->window)))
		return;

	if (ev->override_redirect)
		return;

	if (c->is_hide) {
		log_action("  Window was hidden, now showing.");
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
	log_action("Event handlers initialized");
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

    // 修复：安装 X 错误处理程序
    XSetErrorHandler(xerror);

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
		
        // 修复：抢占后立即同步，如果失败（BadAccess），xerror 会捕获并退出
        XSync(runtime.dpy, False); 
        
		m_init(runtime.dpy, root, x, y, w, h);
	}
    
    runtime.atom_protocols = XInternAtom(runtime.dpy, "WM_PROTOCOLS", False);
    runtime.atom_delete_window = XInternAtom(runtime.dpy, "WM_DELETE_WINDOW", False);
	log_action("WM_PROTOCOLS atoms fetched");
	key_grab();
	
	XSync(runtime.dpy, False);
	log_action("Setup complete. Entering main loop.");
}

void run(void)
{
	XEvent ev;

	while (1) {
		XNextEvent(runtime.dpy, &ev); 
		
		// 日志: 记录接收到的事件类型
		// log_action("Received XEvent type: %d", ev.type);

        if (handler[ev.type])
            handler[ev.type](&ev);
	}
}

void quit(void)
{
	struct mon *m;
	
	log_action("Quitting WM");
	for (m = runtime.mons; m; m = m->next) {
		XUngrabButton(m->display, AnyButton, AnyModifier, m->root);
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