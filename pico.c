
#include <stdint.h>

#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

// all cli is floating
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


// no tab support.
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
	struct dpy *sel_dpy;	// support only one dpy currently.
} runtime;


static void dpy_init();
static void dpy_destroy();

static void cli_init();
static void cli_destroy();
static void cli_resize(struct cli *tar, int w, int h);
static void cli_move(struct cli *tar, int x, int y);
static void cli_focus(struct cli *tar);

static void init();
static void quit();
static void listen();
static void handle();

int main(void)
{

	return 0;	
}
