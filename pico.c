#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

// 窗口管理器配置 (Config)
#define BORDER_WIDTH 2 // 边框宽度
#define MASTER_SIZE  55 // 主窗口占屏幕宽度的百分比 (55%)
#define BORDER_COLOR 0x005080 // 聚焦窗口边框颜色 (蓝色)
#define NORMAL_COLOR 0x222222 // 非聚焦窗口边框颜色 (深灰)

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
	int x, y;
    unsigned int w, h;
	int til_x, til_y; 
    unsigned int til_w, til_h;
	int flt_x, flt_y;
    unsigned int flt_w, flt_h;

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
	int x, y, w, h; // 屏幕工作区域
    Colormap colormap;

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
    
    // 颜色值
    unsigned long normal_border;
    unsigned long focus_border;
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

struct tab *t_init(struct mon *m);
void t_attach_m(struct tab *t, struct mon *m);
void t_detach_m(struct tab *t);
void t_move(struct tab *t, int d_offset);
void t_moveto_m(struct tab *t, struct mon *m);
void t_remove(struct tab *t);
void t_sel(struct tab *t);
void t_unsel(struct tab *t);

struct mon *m_init(Display *dpy, Window root, int x, int y, int w, int h);
void m_attach(struct mon *m);
void m_detach(struct mon *m);
void m_destroy(struct mon *m);
void m_sel(struct mon *m);
void m_unsel(struct mon *m);
void m_update(struct mon *m);

void d_sel(struct cli *c);
void d_unsel(struct cli *c);
void key_grab(void);
void setup(void);
void run(void);
void quit(void);

void spawn(const union arg *arg);
void killclient(const union arg *arg);
void toggle_float(const union arg *arg);
void quit_wm(const union arg *arg);

// 事件处理函数声明
typedef void (*XEventHandler)(XEvent *);
#define LAST_EVENT_TYPE 35 // 确保这个值足够大（Xlib.h 中是 35）
static XEventHandler handler[LAST_EVENT_TYPE];
static void handle_init(void);
static void handle_keypress(XEvent *e);
static void handle_buttonpress(XEvent *e);
static void handle_buttonrelease(XEvent *e);
static void handle_motionnotify(XEvent *e);
static void handle_maprequest(XEvent *e);
static void handle_destroynotify(XEvent *e);
static void handle_enternotify(XEvent *e);
static void handle_unmapnotify(XEvent *e);
static void handle_clientmessage(XEvent *e);
static void handle_configurenotify(XEvent *e);
static void handle_configurerequest(XEvent *e);
static void handle_propertynotify(XEvent *e);
static void handle_focusin(XEvent *e);
static void handle_focusout(XEvent *e);
static unsigned long get_color(Display *d, Colormap cm, unsigned int rgb);

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
/* ** CORE CLIENT AND WINDOW MANAGEMENT ** */
/* ************************************************************************* */

static unsigned long get_color(Display *d, Colormap cm, unsigned int rgb)
{
    XColor color;
    color.pixel = 0;
    color.red = (rgb & 0xFF0000) >> 8;
    color.green = (rgb & 0x00FF00);
    color.blue = (rgb & 0x0000FF) << 8;
    XAllocColor(d, cm, &color);
    return color.pixel;
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

	if (c->mon)
		XMoveResizeWindow(c->mon->display, c->win, 
                          c->x, c->y, c->w - 2 * BORDER_WIDTH, c->h - 2 * BORDER_WIDTH);
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
	
	if (c->mon)
		XMoveResizeWindow(c->mon->display, c->win, 
                          c->x, c->y, c->w - 2 * BORDER_WIDTH, c->h - 2 * BORDER_WIDTH);
}

void c_raise(struct cli *c)
{
	if (c->mon)
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

	if (c->win && c->mon)
    {
		XSetInputFocus(c->mon->display, c->win,
			RevertToPointerRoot, CurrentTime);
        // 设置聚焦边框颜色
        XSetWindowBorder(c->mon->display, c->win, runtime.focus_border);
    }

	c_raise(c);
}

void c_unsel(struct cli *c)
{
	if (!c || !c->is_sel)
		return;

	c->is_sel = false;
    
    if (c->win && c->mon)
    {
        // 设置非聚焦边框颜色
        XSetWindowBorder(c->mon->display, c->win, runtime.normal_border);
    }
}

void c_foc(struct cli *c)
{
	if (!c || c == runtime.cli_foc)
		return;

	if (runtime.cli_foc)
		c_unfoc(runtime.cli_foc);

	runtime.cli_foc = c;
	c->is_foc = true;
}

void c_unfoc(struct cli *c)
{
	if (!c || !c->is_foc)
		return;

	c->is_foc = false;
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
    
    if (c->mon)
        XSetWindowBorderWidth(c->mon->display, c->win, BORDER_WIDTH);

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
    
    if (c->mon)
        XSetWindowBorderWidth(c->mon->display, c->win, BORDER_WIDTH);
}

void c_kill(struct cli *c)
{
    struct mon *m_old = c->mon;
    int n;
    Atom *protocols;
    bool supports_delete = false;

    // 修复点1: 确保指针非空，并保证 m_old 成功保存
    if (!c || !c->win || !c->mon)
            return;

    // 1. 尝试优雅关闭 (WM_DELETE_WINDOW)
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
            XEvent ev;
            ev.type = ClientMessage;
            ev.xclient.window = c->win;
            ev.xclient.message_type = runtime.atom_protocols;
            ev.xclient.format = 32;
            ev.xclient.data.l[0] = runtime.atom_delete_window;
            ev.xclient.data.l[1] = CurrentTime;

            XSendEvent(c->mon->display, c->win, False, NoEventMask, &ev);
            
            // 成功发送请求，等待客户端自行退出，不执行强制销毁。
            return;
    }

    // 2. 强制关闭：客户端不支持协议或无响应
    c_detach_t(c); // c->mon 变为 NULL

    if (c->win)
            XDestroyWindow(m_old->display, c->win); // 🚨 修复点2: 使用 m_old 访问 display

    free(c);

    m_update(m_old);

    if (runtime.mon_sel)
            m_sel(runtime.mon_sel);
}

// 缺少的部分函数实现
void c_attach_d(struct cli *c, struct doc *d) { /* TBD */ }
void d_sel(struct cli *c) { /* TBD */ }
void d_unsel(struct cli *c) { /* TBD */ }
void c_detach_d(struct cli *c) { /* TBD */ }
void c_hide(struct cli *c) { /* TBD */ }
void c_show(struct cli *c) { /* TBD */ }
void c_moveto_t(struct cli *c, struct tab *t) { /* TBD */ }
void c_moveto_m(struct cli *c, struct mon *m) { /* TBD */ }

/* ************************************************************************* */
/* ** TAB AND MONITOR MANAGEMENT ** */
/* ************************************************************************* */

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
    else if (t->clis) // 如果当前tab没有选中的，选择第一个
        c_sel(t->clis);
        
    m_update(t->mon); // 切换 Tab 后更新布局
}

void t_unsel(struct tab *t)
{
	if (!t || !t->is_sel)
		return;

	t->is_sel = false;

	if (t->cli_sel)
		c_unsel(t->cli_sel);
}

void t_move(struct tab *t, int d_offset) { /* TBD */ }
void t_moveto_m(struct tab *t, struct mon *m_target) { /* TBD */ }
void t_remove(struct tab *t) { /* TBD */ }

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

void m_destroy(struct mon *m) { /* TBD */ }

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

struct mon *m_init(Display *dpy, Window root, int x, int y, int w, int h)
{
    struct mon *m;
    if (!(m = calloc(1, sizeof(*m)))) {
        fprintf(stderr, "Error: Failed to allocate memory for new monitor.\n");
        return NULL;
    }

    m->id = (uint64_t)m;
    m->display = dpy;
    m->root = root;
    m->x = x;
    m->y = y;
    m->w = w;
    m->h = h;
    m->colormap = DefaultColormap(dpy, DefaultScreen(dpy));
    m->is_size_change = false;

    m_attach(m);
    m->tab_sel = t_init(m); 
    
    if (runtime.mon_cnt == 1) {
        m_sel(m);
    }
    
    return m;
}

// 完善后的 m_update 函数：实现 Master/Stack 布局
void m_update(struct mon *m)
{
	struct tab *t = m->tab_sel;
	struct cli *c;
	unsigned int i, n;
	int x, y, w, h;
	int master_width;
    
	if (!t)
		return;

    // 1. 统计平铺客户端数量
	for (n = 0, c = t->clis; c; c = c->next) {
		if (c->is_tile)
			n++;
	}

	if (n == 0) {
        // 确保浮动窗口在最上层
        for (c = t->clis_flt; c; c = c->next) {
            c_raise(c);
        }
		return;
    }

	// 2. 定义工作区域 (排除边框)
	x = m->x;
	y = m->y;
	w = m->w;
	h = m->h;
    
    // Master 区域宽度：如果有多个窗口，占 MASTER_SIZE% ；否则占满
	master_width = (n > 1) ? (w * MASTER_SIZE / 100) : w;

	// 3. 布局逻辑 (Master/Stack)
	for (i = 0, c = t->clis; c; c = c->next) {
		if (!c->is_tile) continue;

        // Master 窗口 (i=0)
		if (i == 0) { 
			c_move(c, x, y);
			c_resize(c, master_width, h);
		} 
        // Stack 窗口 (i>0)
        else { 
			int stack_x = x + master_width;
            int stack_y_start = y;
            int stack_count = n - 1;
            
            // 垂直分割 Stack 区域
			int stack_y = stack_y_start + (h / stack_count * (i - 1)); 
			int stack_h = h / stack_count;
			int stack_w = w - master_width;
            
            // 确保最后一个 Stack 窗口填满剩余高度
            if (i == n - 1) {
                stack_h = y + h - stack_y;
            }

			c_move(c, stack_x, stack_y);
			c_resize(c, stack_w, stack_h);
		}
		i++;
	}

    // 4. 确保浮动窗口始终在最上层
    for (c = t->clis_flt; c; c = c->next) {
        c_raise(c);
    }
}

void c_init(struct tab *t, uint64_t arrange)
{
    // 这个函数在 handle_maprequest 中不再使用，但保留其定义
    // ...
}

/* ************************************************************************* */
/* ** EVENT HANDLERS AND MAIN LOOP ** */
/* ************************************************************************* */

void key_grab(void)
{
    if (!runtime.mons) return;
    XUngrabKey(runtime.dpy, AnyKey, AnyModifier, runtime.mons->root);
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        // 抓取按键
        XGrabKey(runtime.dpy, XKeysymToKeycode(runtime.dpy, keys[i].keysym),
                 keys[i].mod, runtime.mons->root, True, GrabModeAsync, GrabModeAsync);
    }
}

static void handle_init(void)
{
    // 初始化所有事件处理函数
    for (size_t i = 0; i < LAST_EVENT_TYPE; i++)
        handler[i] = NULL;
        
    handler[KeyPress]           = handle_keypress;
    handler[ButtonPress]        = handle_buttonpress;
    handler[ButtonRelease]      = handle_buttonrelease;
    handler[MotionNotify]       = handle_motionnotify;
    handler[MapRequest]         = handle_maprequest;
    handler[DestroyNotify]      = handle_destroynotify;
    handler[UnmapNotify]        = handle_unmapnotify;
    handler[EnterNotify]        = handle_enternotify;
    handler[ClientMessage]      = handle_clientmessage;
    handler[ConfigureRequest]   = handle_configurerequest;
    handler[ConfigureNotify]    = handle_configurenotify;
    handler[PropertyNotify]     = handle_propertynotify;
    handler[FocusIn]            = handle_focusin;
    handler[FocusOut]           = handle_focusout;
}

static void handle_maprequest(XEvent *e)
{
    XMapRequestEvent *ev = &e->xmaprequest;
    struct cli *c;
    XWindowAttributes wa;
    
    // 忽略已知的窗口或不可重定向的窗口
    if ((c = c_fetch(ev->window))) {
        XMapWindow(runtime.dpy, ev->window);
        return;
    }
    if (!XGetWindowAttributes(runtime.dpy, ev->window, &wa) || wa.override_redirect)
        return;
    
    if (!runtime.tab_sel) return;

    // 创建新的客户端结构
    if (!(c = calloc(1, sizeof(*c)))) return;
    
    c->win = ev->window;
    
    // 附着到当前选中的 Tab
    c_attach_t(c, runtime.tab_sel);
    
    // 从 X Server 获取初始几何信息
    c->x = c->flt_x = wa.x;
    c->y = c->flt_y = wa.y;
    c->w = c->flt_w = wa.width + 2 * BORDER_WIDTH; // 包含边框
    c->h = c->flt_h = wa.height + 2 * BORDER_WIDTH;

    // 默认平铺
    c_tile(c);

    // 设置边框和事件
    XSetWindowBorderWidth(runtime.dpy, c->win, BORDER_WIDTH);
    XSetWindowBorder(runtime.dpy, c->win, runtime.normal_border);
    XSelectInput(runtime.dpy, c->win, EnterWindowMask | FocusChangeMask | PropertyChangeMask | StructureNotifyMask);
    
    XMapWindow(runtime.dpy, c->win);
    c_sel(c);

    m_update(runtime.mon_sel);
}

static void handle_enternotify(XEvent *e)
{
    XCrossingEvent *ev = &e->xcrossing;
    struct cli *c;
    
    if (ev->mode != NotifyNormal || ev->detail == NotifyInferior)
        return;

    if ((c = c_fetch(ev->window))) {
        c_sel(c); // 鼠标进入时选中并聚焦
    }
}

static void handle_keypress(XEvent *e)
{
    XKeyEvent *ev = &e->xkey;
    KeySym keysym;
    
    keysym = XKeycodeToKeysym(runtime.dpy, ev->keycode, 0);

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (keys[i].keysym == keysym && keys[i].mod == ev->state) {
            keys[i].func(&keys[i].arg);
        }
    }
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

    if (m_old) {
        m_update(m_old);
        // 如果当前选中客户端被销毁，自动选择下一个客户端
        if (!runtime.cli_sel && runtime.tab_sel && runtime.tab_sel->clis) {
            c_sel(runtime.tab_sel->clis);
        }
    }
}

static void handle_unmapnotify(XEvent *e)
{
    XUnmapEvent *ev = &e->xunmap;
    struct cli *c;
    
    if (!(c = c_fetch(ev->window)))
        return;
    
    if (c->is_unmap_by_wm) {
        c->is_unmap_by_wm = false;
        return;
    }
    
    c->is_hide = true;
    m_update(c->mon);
}

static void handle_buttonpress(XEvent *e)
{
    XButtonPressedEvent *ev = &e->xbutton;
    struct cli *c;
    
    if (!(c = c_fetch(ev->window))) {
        c = runtime.cli_sel;
        if (c) c_sel(c);
        return;
    }

    c_sel(c); // 鼠标点击时选中并聚焦

    if (ev->button == Button1 && ev->state == MOUSE_MOD) {
        runtime.mouse_mode = MOUSE_MODE_MOVE;
    } else if (ev->button == Button3 && ev->state == MOUSE_MOD) {
        runtime.mouse_mode = MOUSE_MODE_RESIZE;
    } else {
        return;
    }
    
    runtime.cli_mouse = c;
    
    XGrabPointer(runtime.dpy, runtime.mons->root, False, 
                 ButtonReleaseMask | PointerMotionMask, 
                 GrabModeAsync, GrabModeAsync, 
                 None, None, CurrentTime);
}

static void handle_motionnotify(XEvent *e)
{
    XMotionEvent *ev = &e->xmotion;
    struct cli *c = runtime.cli_mouse;
    
    if (!c || runtime.mouse_mode == MOUSE_MODE_NONE)
        return;

    // 切换到浮动模式
    if (c->is_tile) {
        c_float(c);
    }
    
    if (runtime.mouse_mode == MOUSE_MODE_MOVE) {
        int x = ev->x_root - (c->w / 2);
        int y = ev->y_root - (c->h / 2);
        c_move(c, x, y);
    } else if (runtime.mouse_mode == MOUSE_MODE_RESIZE) {
        int w = ev->x_root - c->x;
        int h = ev->y_root - c->y;
        
        if (w < 50) w = 50; 
        if (h < 50) h = 50; 
        
        c_resize(c, w, h);
    }
}

static void handle_buttonrelease(XEvent *e)
{
    if (runtime.mouse_mode == MOUSE_MODE_NONE)
        return;

    XUngrabPointer(runtime.dpy, CurrentTime);
    runtime.mouse_mode = MOUSE_MODE_NONE;
    runtime.cli_mouse = NULL;
}

static void handle_clientmessage(XEvent *e)
{
    // 处理 _NET_WM_STATE 消息（例如最小化/最大化）
    XClientMessageEvent *ev = &e->xclient;
    // TBD
}

static void handle_configurerequest(XEvent *e)
{
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges wc;
    
    wc.x = ev->x;
    wc.y = ev->y;
    wc.width = ev->width;
    wc.height = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;

    // 简单地允许客户端自己配置窗口（如果它是浮动的或者我们还没有处理）
    XConfigureWindow(runtime.dpy, ev->window, ev->value_mask, &wc);
}

static void handle_configurenotify(XEvent *e)
{
    // X Server 通知窗口配置已更改
    XConfigureEvent *ev = &e->xconfigure;
    struct cli *c;
    
    if (ev->window == runtime.mons->root)
    {
        // 根窗口大小更改，意味着显示器大小更改，需要重新布局
        // TBD: 重新获取屏幕尺寸并调用 m_update(m)
    }
    else if ((c = c_fetch(ev->window)))
    {
        // 客户端窗口配置更改，更新记录的几何信息
        if (c->x != ev->x || c->y != ev->y || 
            c->w != ev->width + 2*BORDER_WIDTH || c->h != ev->height + 2*BORDER_WIDTH)
        {
             // 如果不是 WM 触发的配置更改，则更新浮动状态
             if (c->is_float) {
                c->x = c->flt_x = ev->x;
                c->y = c->flt_y = ev->y;
                c->w = c->flt_w = ev->width + 2*BORDER_WIDTH;
                c->h = c->flt_h = ev->height + 2*BORDER_WIDTH;
             }
        }
    }
}

static void handle_propertynotify(XEvent *e)
{
    // 窗口属性更改 (例如窗口标题/类名更改)
    XPropertyEvent *ev = &e->xproperty;
    struct cli *c;
    
    if ((c = c_fetch(ev->window)))
    {
        // TBD: 检查是否需要根据属性更改来改变浮动/平铺状态
    }
}

static void handle_focusin(XEvent *e) { /* TBD */ }
static void handle_focusout(XEvent *e) { /* TBD */ }


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

        // 设置根窗口的事件掩码，以重定向窗口管理事件
		XSelectInput(runtime.dpy, root, 
			SubstructureRedirectMask | SubstructureNotifyMask | 
			KeyPressMask | ButtonPressMask | EnterWindowMask | StructureNotifyMask);
		
		m_init(runtime.dpy, root, x, y, w, h);
	}
	
    // 修复点: 初始化 WM 协议原子
    runtime.atom_protocols = XInternAtom(runtime.dpy, "WM_PROTOCOLS", False);
    runtime.atom_delete_window = XInternAtom(runtime.dpy, "WM_DELETE_WINDOW", False);

    // 初始化边框颜色
    runtime.normal_border = get_color(runtime.dpy, DefaultColormap(runtime.dpy, DefaultScreen(runtime.dpy)), NORMAL_COLOR);
    runtime.focus_border = get_color(runtime.dpy, DefaultColormap(runtime.dpy, DefaultScreen(runtime.dpy)), BORDER_COLOR);

	key_grab();
	
	XSync(runtime.dpy, False);
}

void run(void)
{
	XEvent e;
	while (!XNextEvent(runtime.dpy, &e)) {
        if (e.type < LAST_EVENT_TYPE && handler[e.type]) {
            handler[e.type](&e);
        }
	}
}

void quit(void)
{
	// 尝试向所有客户端发送 WM_DELETE_WINDOW
    struct mon *m;
    struct tab *t;
    struct cli *c;
    
    for (m = runtime.mons; m; m = m->next) {
        for (t = m->tabs; t; t = t->next) {
            for (c = t->clis; c; c = c->next) {
                 // 强制销毁所有客户端 (不使用 c_kill 以避免复杂的退出等待逻辑)
                if (c->win)
                   XDestroyWindow(m->display, c->win);
            }
        }
    }
    
	XCloseDisplay(runtime.dpy);
	exit(0);
}

int main(void)
{
	setup();
	run();
	return 0;
}
