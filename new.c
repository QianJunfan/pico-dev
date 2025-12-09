#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <stdarg.h>
#include <time.h> // 新增：用于日志时间戳
#include <string.h> // 新增：用于日志路径处理
#include <X11/Xproto.h>
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

// 新增全局文件指针
static FILE *logfile = NULL;

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

// 修改 log_action 函数，增加时间戳和上下文，并写入文件
static void log_action(const char *format, ...)
{
	va_list args;
    char log_msg[512];
    char context_buf[128];
    time_t t = time(NULL);
    struct tm *tm_info;

    // 获取当前时间
    tm_info = localtime(&t);

    // 准备上下文信息 (Monitor, Tab, Client)
    snprintf(context_buf, sizeof(context_buf),
             "M:0x%lx T:0x%lx C:0x%lx",
             runtime.mon_sel ? runtime.mon_sel->id : 0,
             runtime.tab_sel ? runtime.tab_sel->id : 0,
             runtime.cli_sel ? runtime.cli_sel->win : 0);

    // 格式化主信息
	va_start(args, format);
    vsnprintf(log_msg, sizeof(log_msg), format, args);
	va_end(args);

    // 输出到标准错误 (便于调试)
	fprintf(stderr, "pico: Log: [%02d:%02d:%02d] [%s] %s\n",
        tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
        context_buf, log_msg);

    // 输出到日志文件 (~/devlog)
    if (logfile) {
        fprintf(logfile, "[%02d:%02d:%02d] pico: [%s] %s\n",
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                context_buf, log_msg);
        fflush(logfile);
    }
}
/* X error handler: ignore non-fatal errors based on dwm's robust logic. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
	/* --- dwm non-fatal error filtering logic --- */
	if (ee->error_code == BadWindow // 1. 忽略对已销毁窗口的操作 (BadWindow)
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch) // 2. 忽略焦点设置错误 (BadMatch)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch) // 3. 忽略配置窗口时的不匹配错误 (BadMatch)
    
    // 4. 解决日志中出现的 FATAL X error 2 (BadValue/BadAccess/BadWindow)
    // 错误 2 对应 BadValue。在 X_ChangeWindowAttributes 中，常见于 XSelectInput 的时序问题。
	|| (ee->request_code == X_ChangeWindowAttributes && ee->error_code == BadAccess)
	|| (ee->request_code == X_ChangeWindowAttributes && ee->error_code == BadValue)
	|| (ee->request_code == X_ChangeWindowAttributes && ee->error_code == BadWindow)
	
    // 5. 忽略抓取操作的权限错误
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
    
    // 6. 忽略常见的绘图和边框设置错误
	|| (ee->request_code == X_SetWindowBorder && ee->error_code == BadAccess)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyText16 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ImageText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ImageText16 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_CreatePixmap && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ClearArea && ee->error_code == BadDrawable)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
	{
		// 记录为非致命错误，并立即返回 0，让程序继续执行。
        log_action("IGNORED: Non-fatal X error %u (Request: %u/%u, Resource: %lu)",
                   ee->error_code, ee->request_code, ee->minor_code, ee->resourceid);
		return 0;
	}
	/* --- End dwm logic --- */

    // 针对未被忽略的、真正无法处理的错误，才视为 FATAL。
	log_action("FATAL: Unhandled X error %u (Request: %u/%u, Resource: %lu, Serial: %lu)",
               ee->error_code, ee->request_code, ee->minor_code, ee->resourceid, ee->serial);
    // 注意：如果您的 pico 窗口管理器有退出机制，可能需要在此处调用，但为了兼容性，我们保持返回 0。
	return 0;
}

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
void new_tab(const union arg *arg);
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

#define IGNORED_MODS (LockMask | Mod2Mask)
#define CLEANMASK(mask) ((mask) & ~IGNORED_MODS)

static const char *termcmd[] = { "xterm", NULL };
static const char *browsercmd[] = { "firefox", NULL };

static const struct key keys[] = {
	{ XK_SUPER,   XK_Return,    spawn,      {.ptr = termcmd } },
	{ XK_SUPER,   XK_w,         spawn,      {.ptr = browsercmd } },
	{ XK_SUPER,   XK_c,         killclient, {0} },
	{ XK_SUPER,   XK_f,         toggle_float, {0} },
	{ XK_SUPER,   XK_q,         quit_wm,    {0} },

	{ XK_SUPER,   XK_Right,     view_next_tab,  {0} },
	{ XK_SUPER,   XK_Left,      view_prev_tab,  {0} },
    { XK_SUPER,   XK_t,         new_tab,        {0} },
	{ XK_SUPER,   XK_j,         focus_next_cli, {0} },
	{ XK_SUPER,   XK_k,         focus_prev_cli, {0} },
};

void spawn(const union arg *arg)
{
	log_action("Spawn: %s", ((char **)arg->ptr)[0]);
	if (fork() == 0) {
		setsid();
		execvp(((char **)arg->ptr)[0], (char **)arg->ptr);
		fprintf(stderr, "pico: execvp failed for %s\n",
			((char **)arg->ptr)[0]);
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
	struct tab *next;

	if (!t || !t->mon)
		return;

	// 从当前 Tab 开始，找下一个 Tab
	next = t->next;
	// 如果当前 Tab 是最后一个，则绕回到第一个 Tab
	if (!next) {
		next = t->mon->tabs;
	}

	if (next && next != t) {
		log_action("ViewNextTab: Switching from tab 0x%lx to 0x%lx",
			t->id, next->id);
		t_sel(next);
	}
}

void view_prev_tab(const union arg *arg)
{
	struct tab *t = runtime.tab_sel;
	struct tab *prev;

	if (!t || !t->mon)
		return;

	// 从当前 Tab 开始，找上一个 Tab
	prev = t->prev;
	// 如果当前 Tab 是第一个，则绕回到最后一个 Tab
	if (!prev) {
		prev = t->mon->tabs;
		while (prev && prev->next)
			prev = prev->next;
	}

	if (prev && prev != t) {
		log_action("ViewPrevTab: Switching from tab 0x%lx to 0x%lx",
			t->id, prev->id);
		t_sel(prev);
	}
}

void focus_next_cli(const union arg *arg)
{
	struct cli *c = runtime.cli_sel;
	struct cli *next;

	if (!c || !c->tab)
		return;

	next = c->next ? c->next : c->tab->clis;

	if (next && next != c) {
		log_action("FocusNextCli: Focusing client 0x%lx", next->win);
		c_sel(next);
	}
}

void focus_prev_cli(const union arg *arg)
{
	struct cli *c = runtime.cli_sel;
	struct cli *prev;

	if (!c || !c->tab)
		return;

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

typedef void (*XEventHandler)(XEvent *);

#define LAST_EVENT_TYPE 36 // 修正：确保事件类型数组足够大，即使 XEvent::type 的最大值是 35

static XEventHandler handler[LAST_EVENT_TYPE];

static void c_til_append(struct cli *c, struct tab *t)
{
	c->tab = t;
	t->cli_til_cnt++;
	t->clis_til = realloc(t->clis_til,
			      t->cli_til_cnt * sizeof(struct cli *));
	t->clis_til[t->cli_til_cnt - 1] = c;
	log_action("Client 0x%lx attached as tiled to tab 0x%lx",
		c->win, t->id);
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
				// 重新分配内存（可能变小）
				t->clis_til = realloc(t->clis_til,
					t->cli_til_cnt * sizeof(struct cli *));
			} else {
				// 客户端数量为 0，释放内存并置空指针
				free(t->clis_til);
				t->clis_til = NULL;
			}
			log_action("Client 0x%lx removed from tiled list of "
				"tab 0x%lx", c->win, t->id);
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
	log_action("Client 0x%lx attached as floating to tab 0x%lx",
		c->win, t->id);
}

static void c_detach_flt(struct cli *c)
{
	struct tab *t = c->tab;

	if (!t) return;

	if (c->prev)
		c->prev->next = c->next;
	if (c->next)
		c->next->prev = c->prev;

	if (t->clis_flt == c)
		t->clis_flt = c->next;

	t->cli_flt_cnt--;
	c->next = NULL;
	c->prev = NULL;
	log_action("Client 0x%lx detached from floating list of tab 0x%lx",
		c->win, t->id);
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
	log_action("Client 0x%lx attached to tab 0x%lx (general list)",
		c->win, t->id);
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
}

void c_unfoc(struct cli *c)
{
	if (!c || !c->is_foc)
		return;

	log_action("Client unfocus: 0x%lx", c->win);
	c->is_foc = false;
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

	if (!t->cli_sel && t->clis) {
		c_sel(t->clis);
	} else if (t->cli_sel) {
		c_sel(t->cli_sel);
	}

	m_update(t->mon); 
}

void t_unsel(struct tab *t)
{
	struct cli *c;

	if (!t || !t->is_sel)
		return;

	log_action("Tab unselect: 0x%lx", t->id);
	t->is_sel = false;

	if (t->cli_sel)
		c_unsel(t->cli_sel);

	// 隐藏此 Tab 上的所有客户端
	for (c = t->clis; c; c = c->next) {
		if (!c->is_hide) {
			c_hide(c);
		}
	}
    
	// 强制 X 服务器立即处理隐藏请求
	if (t->mon && t->mon->display)
		XSync(t->mon->display, False);
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

	// 移除客户端在旧 Tab 上的特殊列表位置
	if (c->is_tile)
		c_til_remove(c);
	if (c->is_float)
		c_detach_flt(c);

	// 从旧 Tab 的 general list 移除
	c_detach_t(c);
	// 附加到新 Tab 的 general list
	c_attach_t(c, t);

	// 重新附加到新 Tab 的特殊列表
	if (c->is_float)
		c_attach_flt(c, t);
	else
		c_til_append(c, t);

	m_update(m_old);
	t_sel(t);
	m_update(t->mon);
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
	Display *dpy;

	if (!c || !c->win || !c->mon)
		return;

	dpy = c->mon->display;
	log_action("Attempting to kill client 0x%lx", c->win);

	if (XGetWMProtocols(c->mon->display, c->win, &protocols, &n)) {
		for (int i = 0; i < n; i++) {
			if (protocols[i] == runtime.atom_delete_window) {
				supports_delete = true;
				break;
			}
		}
		XFree(protocols);
	}

	if (supports_delete) {
		log_action("Client 0x%lx supports WM_DELETE_WINDOW, "
			"sending message", c->win);
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

	log_action("Client 0x%lx does not support WM_DELETE_WINDOW, "
		"destroying window", c->win);
	c_detach_t(c);

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


void new_tab(const union arg *arg)
{
	struct mon *m = runtime.mon_sel;
	struct tab *t;

	if (!m)
		return;

	t = t_init(m);

	if (t) {
		log_action("New Tab created: 0x%lx, activating it.", t->id);
		t_sel(t);
	}
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
			log_action("  Moving client 0x%lx to fallback tab 0x%lx",
				c->win, t_fallback->id);
			c_moveto_t(c, t_fallback);
		} else {
			log_action("  Killing client 0x%lx (no fallback tab)",
				c->win);
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
		fprintf(stderr,
			"Error: Failed to allocate memory for new monitor.\n");
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
	log_action("Monitor 0x%lx initialized: %dx%d @ %d,%d",
		m->id, w, h, x, y);

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
			log_action("  Moving tab 0x%lx to fallback monitor 0x%lx",
				t->id, m_fallback->id);
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

	// 强制映射所有浮动窗口
	for (c = t->clis_flt; c; c = c->next) {
		XMapWindow(c->mon->display, c->win); 
		c->is_hide = false; 
		c_raise(c);
	}

	if (n_til == 0)
		goto end;

	master = t->clis_til[0];

	x = m->x + gap;
	y = m->y + gap;
	w = m->w - 2 * gap;
	h = m->h - 2 * gap;

	if (n_til == 1) {
		c_move(master, x, y);
		c_resize(master, w, h);
		goto show_tiled;
	}

	master_w = w * 55 / 100;
	stack_w = w - master_w - gap;

	c_move(master, x, y);
	c_resize(master, master_w - gap, h);

	x += master_w + gap;
	stack_h = h / (n_til - 1);

	for (i = 1; i < n_til; i++) {
		c = t->clis_til[i];
		c_move(c, x, y + (i - 1) * stack_h);
		c_resize(c, stack_w, stack_h - gap);
	}

show_tiled:
	for (i = 0; i < n_til; i++) {
		c = t->clis_til[i];
		// 强制映射所有平铺窗口
		XMapWindow(c->mon->display, c->win);
		c->is_hide = false; 
	}
	if (t->cli_sel && t->cli_sel->is_tile)
		c_raise(t->cli_sel);

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
	const unsigned int numlock_masks[] = { 0, XK_NUM };
	unsigned int j;

	if (!m)
		return;

	XUngrabKey(m->display, AnyKey, AnyModifier, m->root);

	for (i = 0; i < sizeof(keys) / sizeof(*keys); i++) {
		code = key_get(keys[i].keysym);

		if (code == 0) {
			log_action("Warning: KeySym %s (0x%lx) not mapped to a KeyCode. Skipping grab.",
				XKeysymToString(keys[i].keysym), keys[i].keysym);
			continue;
		}

		for (j = 0; j < sizeof(numlock_masks) / sizeof(*numlock_masks); j++) {
			XGrabKey(m->display, code, keys[i].mod | numlock_masks[j],
				m->root, True, GrabModeAsync, GrabModeAsync);
		}
	}
	log_action("Key grabs completed");
}

static void mouse_grab(void)
{
	struct mon *m = runtime.mons;
	const unsigned int numlock_masks[] = { 0, XK_NUM };
	unsigned int j;

	if (!m)
		return;

	XUngrabButton(m->display, AnyButton, AnyModifier, m->root);

	for (j = 0; j < sizeof(numlock_masks) / sizeof(*numlock_masks); j++) {
		XGrabButton(m->display, Button1, MOUSE_MOD | numlock_masks[j],
			m->root, True, ButtonPressMask | ButtonReleaseMask |
			PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);

		XGrabButton(m->display, Button3, MOUSE_MOD | numlock_masks[j],
			m->root, True, ButtonPressMask | ButtonReleaseMask |
			PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
	}
	log_action("Mouse grabs (Button1/3 + MOUSE_MOD) completed on root window");
}


static void key_handle(XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	unsigned int i;
	KeySym keysym = XKeycodeToKeysym(ev->display, (KeyCode)ev->keycode, 0);
	uint32_t clean_state = CLEANMASK(ev->state);

	for (i = 0; i < sizeof(keys) / sizeof(*keys); i++) {
		if (keysym == keys[i].keysym && clean_state == keys[i].mod) {
			log_action("KeyPress: Mod 0x%x, KeySym %s, "
				"Function executed", clean_state,
				XKeysymToString(keysym));
			keys[i].func(&keys[i].arg);
			return;
		}
	}
	log_action("KeyPress: Mod 0x%x, KeySym %s, No matching binding found",
		clean_state, XKeysymToString(keysym));
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

	// 获取窗口的初始几何信息
	XGetGeometry(t->mon->display, c->win, &wa.root,
		&c->x, &c->y, &c->w, &c->h,
		&wa.border_width, &depth);

	// 设置浮动窗口的初始值
	c->flt_x = c->x;
	c->flt_y = c->y;
	c->flt_w = c->w;
	c->flt_h = c->h;

	// 初始化拖拽信息
	c->drag_x = c->x;
	c->drag_y = c->y;
	c->drag_w = c->w;
	c->drag_h = c->h;
	c->drag_root_x = 0;
	c->drag_root_y = 0;

	// 确定初始布局模式（根据 arrange_type 或 transient hint）
	c->is_float = (runtime.arrange_type == 1);

	if (XGetTransientForHint(t->mon->display, c->win, &trans) &&
	    trans != None) {
		c->is_float = true;
	}

	if ((wmh = XGetWMHints(t->mon->display, c->win)))
		XFree(wmh);

	// 1. 将客户端附加到 WM 的内部结构中
	c_attach_t(c, t);

	// 2. 关键修复：在附加后、MapWindow之前，设置事件监听（修复 BadValue/XError 2 风险）
	XSelectInput(c->mon->display, c->win,
		EnterWindowMask | FocusChangeMask | ButtonPressMask);

	if (c->is_float) {
		log_action("  Client is floating.");
		c_float(c); // c_float 会调用 c_attach_flt 和 m_update
	} else {
		log_action("  Client is tiled.");
		c_tile(c); // c_tile 会调用 c_til_append 和 m_update
	}
    
    // 如果客户端被添加到非活动 Tab，需要立即隐藏它。
    if (t != runtime.tab_sel) {
        log_action("  Client mapped on UNSELECTED tab 0x%lx. Hiding it immediately.", t->id);
        c_hide(c);
        // c_hide 中包含了 XUnmapWindow，这里不应该调用 XMapWindow
    } else {
		// 如果是选中 Tab，c_tile/c_float 中的 m_update 会处理 XMapWindow，
		// 但为了确保万无一失，这里可以保留 XMapWindow
        XMapWindow(c->mon->display, c->win);
        c_sel(c);
        m_update(t->mon);
    }
    
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
    
    // 只有当窗口在选中的 Tab 上，才聚焦它
    if (c->tab != runtime.tab_sel) {
        log_action("EnterNotify: client 0x%lx ignored (not on selected tab)", c->win);
        return;
    }

	log_action("EnterNotify: client 0x%lx entered", c->win);
	c_foc(c);
	c_sel(c);
}

static void handle_buttonpress(XEvent *e)
{
	XButtonEvent *ev = &e->xbutton;
	struct cli *c;
	Display *dpy = ev->display;
	Window root;
	uint32_t clean_state;

	log_action("ButtonPress: Button %u on window 0x%lx, state 0x%x",
		ev->button, ev->window, ev->state);

	if (!(c = c_fetch(ev->window))) {
		if (ev->window != RootWindowOfScreen(DefaultScreenOfDisplay(dpy)))
			return;

		c = runtime.cli_sel;
		if (!c)
			return;
	}
    
    // 只有当窗口在选中的 Tab 上，才进行操作（根窗口除外）
    if (c->tab && c->tab != runtime.tab_sel && c->win != c->mon->root) {
        log_action("ButtonPress: client 0x%lx ignored (not on selected tab)", c->win);
        return;
    }

	c_sel(c);

	clean_state = CLEANMASK(ev->state);

	if (!c->is_float || clean_state != MOUSE_MOD)
		return;

	if (runtime.mouse_mode != MOUSE_MODE_NONE)
		return;

	runtime.cli_mouse = c;
	c_raise(c);

	if (!c->mon)
		return;
	root = c->mon->root;

	c->drag_x = c->x;
	c->drag_y = c->y;
	c->drag_w = c->w;
	c->drag_h = c->h;
	c->drag_root_x = ev->x_root;
	c->drag_root_y = ev->y_root;

	if (ev->button == Button1) {
		runtime.mouse_mode = MOUSE_MODE_MOVE;
		log_action("  Starting MOVE mode for client 0x%lx", c->win);

		XGrabPointer(dpy, root, False,
			     ButtonMotionMask | ButtonReleaseMask,
			     GrabModeAsync, GrabModeAsync,
			     None, None, CurrentTime);

	} else if (ev->button == Button3) {
		runtime.mouse_mode = MOUSE_MODE_RESIZE;
		log_action("  Starting RESIZE mode for client 0x%lx", c->win);

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

	dx = ev->x_root - c->drag_root_x;
	dy = ev->y_root - c->drag_root_y;

	switch (runtime.mouse_mode) {
	case MOUSE_MODE_MOVE:
		c_move(c, c->drag_x + dx, c->drag_y + dy);
		break;

	case MOUSE_MODE_RESIZE:
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

	if (!(c = c_fetch(ev->window))) {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = 0;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(ev->display, ev->window, ev->value_mask, &wc);
		log_action("ConfigureRequest: Window 0x%lx (unmanaged) "
			"configured", ev->window);
		return;
	}

	log_action("ConfigureRequest: Window 0x%lx (managed, float: %d)",
		c->win, c->is_float);
    
    // 修复 3: 忽略非选中 Tab 上的平铺客户端的 ConfigureRequest
    // 只有在选中的 Tab 上，或者窗口是浮动的（即使在未选中 Tab 也应处理配置），才继续。
    if (c->tab != runtime.tab_sel && c->is_tile) {
        log_action("  Ignoring ConfigureRequest on unselected tiled client 0x%lx", c->win);
        return;
    }

	wc.x = c->x;
	wc.y = c->y;
	wc.width = c->w;
	wc.height = c->h;
	wc.border_width = 0;
	wc.sibling = ev->above;
	wc.stack_mode = ev->detail;

	if (c->is_float) {
		if (ev->value_mask & CWX) c->flt_x = ev->x;
		if (ev->value_mask & CWY) c->flt_y = ev->y;
		if (ev->value_mask & CWWidth) c->flt_w = ev->width;
		if (ev->value_mask & CWHeight) c->flt_h = ev->height;

		wc.x = c->flt_x;
		wc.y = c->flt_y;
		wc.width = c->flt_w;
		wc.height = c->flt_h;

		XConfigureWindow(ev->display, ev->window, ev->value_mask, &wc);
		log_action("  Configuring as floating: %d,%d %dx%d",
			wc.x, wc.y, wc.width, wc.height);

	} else {
		// 平铺窗口忽略客户端请求的 size/pos
		wc.x = c->x;
		wc.y = c->y;
		wc.width = c->w;
		wc.height = c->h;

		XConfigureWindow(ev->display, ev->window,
			CWX | CWY | CWWidth | CWHeight | CWBorderWidth, &wc);
		log_action("  Configuring as tiled: %d,%d %dx%d (ignoring "
			"client request)", wc.x, wc.y, wc.width, wc.height);
	}
}

static void handle_unmapnotify(XEvent *e)
{
	XUnmapEvent *ev = &e->xunmap;
	struct cli *c;
	struct mon *m_old;

	log_action("UnmapNotify for window 0x%lx (SendEvent: %d)",
		ev->window, ev->send_event);

	if (!(c = c_fetch(ev->window)))
		return;

	m_old = c->mon;

	if (c->is_unmap_by_wm) {
		log_action("  Unmap caused by WM (hiding client)");
		c->is_hide = true;
		c->is_sel = false;
		c->is_unmap_by_wm = false;

		if (m_old == runtime.mon_sel) // 只有当旧窗口在当前选中的显示器上时，才更新布局
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

    // 当窗口的 is_hide 标志为 true 时，说明它被 WM 隐藏了，现在被 X Server 映射了。
	if (c->is_hide) {
		log_action("  Window was hidden, now showing.");
		c->is_hide = false;

		if (c->tab) {
			t = c->tab;

			if (t == runtime.tab_sel) {
				// 窗口在当前选中的 Tab 上，正常处理
				if (c->is_float)
					c_raise(c);

				c_sel(c);
				m_update(c->mon);
			} else {
				// 关键修复 2: 窗口被意外映射，但它不在当前选中的 Tab 上。
				// 立即再次隐藏它，以保证 Tab 隔离。
                log_action("  CRITICAL FIX: Window mapped on UNSELECTED tab 0x%lx. Hiding again.", t->id);
                c_hide(c);
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

    // 新增：打开日志文件 ~/devlog
    char *home = getenv("HOME");
    if (home) {
        char log_path[512];
        // 使用 "w" 模式覆盖现有文件，或 "a" 模式追加。此处使用 "a" 追加。
        snprintf(log_path, sizeof(log_path), "%s/devlog", home);
        logfile = fopen(log_path, "a");
        if (!logfile) {
            fprintf(stderr, "pico: Warning: Could not open log file %s\n", log_path);
        } else {
            // 写入分隔线，标记新的 WM 会话
            fprintf(logfile, "\n============================ WM START ============================\n");
            log_action("Log file opened successfully: %s", log_path);
        }
    }
    // ----------------------------------------------------

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

		XSync(runtime.dpy, False);

		m_init(runtime.dpy, root, x, y, w, h);
	}

	runtime.atom_protocols = XInternAtom(runtime.dpy, "WM_PROTOCOLS", False);
	runtime.atom_delete_window = XInternAtom(runtime.dpy,
						 "WM_DELETE_WINDOW", False);
	log_action("WM_PROTOCOLS atoms fetched");

	key_grab();
	mouse_grab();

	XSync(runtime.dpy, False);
	log_action("Setup complete. Entering main loop.");
}

void run(void)
{
	XEvent ev;

	while (1) {
		XNextEvent(runtime.dpy, &ev);

		if (ev.type >= 0 && ev.type < LAST_EVENT_TYPE && handler[ev.type])
			handler[ev.type](&ev);
	}
}

void quit(void)
{
	struct mon *m;

	log_action("Quitting WM");
	for (m = runtime.mons; m; m = m->next) {
		XUngrabKey(m->display, AnyKey, AnyModifier, m->root);
		XUngrabButton(m->display, AnyButton, AnyModifier, m->root);
		XSelectInput(m->display, m->root, 0);
	}

	if (runtime.dpy)
		XCloseDisplay(runtime.dpy);
    
    if (logfile)
        fclose(logfile);

	exit(0);
}

int main(void)
{
	setup();
	run();
	quit();
	return 0;
}