#include <stdint.h>
#include <stdbool.h>

#define VERSION "alpha 0.0.1"

#define MOUSE_MODE_NONE   0;
#define MOUSE_MODE_MOVE   1;
#define MOUSE_MODE_RESIZE 2;

#define XK_SHIFT	ShiftMask
#define XK_LOCK		LockMask
#define XK_CONTROL	ControlMask
#define XK_ALT		Mod1Mask
#define XK_NUM		Mod2Mask
#define XK_HYPER	Mod3Mask
#define XK_SUPER	Mod4Mask
#define XK_LOGO		Mod5Mask

struct arg;
struct cli;
struct tab;
struct mon;

struct arg {
	const void *ptr;
	const int i;
};

struct cli {
        struct cli *link[2];       // {next, prev}
        int pos[2];                // {til_x, til_y, flt_x, flt_y}
        unsigned int geo[4];       // {til_w, til_h, flt_w, flt_h}
        bool is_selected  : 1;
        bool is_focused   : 1;
        bool is_hidden    : 1;
        uint8_t layout    : 2;     // 0 = float, 1 = tile;
};

struct tab {
        char name[32];
        struct tab *link[2];    // {next, prev}
        uint64_t cli_cnt[2];    // {tils_cnt, flt_cnt}
        struct cli *tils;
        struct cli *flts;
        struct mon *mon;
        bool is_selected  : 1;
        bool is_focused   : 1;
        bool is_hidden    : 1;
};

struct mon {
        uint64_t tab_cnt;
        struct tab *tabs;
        int pos[2];             // {x, y}
        unsigned int geo[2];    // {w, h}
        bool is_selected  : 1;
        bool is_focused   : 1;
};

struct {
        uint64_t mon_cnt;
        struct mon *mons;
} runtime;


int xsetup;
