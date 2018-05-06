#include "types.h"

#define MOD		XCB_MOD_MASK_4
#define BORDER		4
#define GAP		6
#define TOP		0
#define BOT		0
#define FOCUSCOLOR	0xFFFFFF
#define UNFOCUSCOLOR	0xE4D1B0

static void arai_kill(xcb_window_t window);
static void arai_center(xcb_window_t window);
static void arai_tile_left(xcb_window_t window);
static void arai_tile_right(xcb_window_t window);
static void arai_cycle(xcb_window_t window);
static void arai_max(xcb_window_t window);
static void arai_fullscreen(xcb_window_t window);
static void arai_cycle(xcb_window_t window);

arai_button buttons[] = {
	{ MOD, 1 },
	{ MOD, 3 }
};

arai_key keys[] = {
	{ MOD, XK_q, arai_kill},
	{ MOD, XK_g, arai_center },
	{ MOD, XK_Left, arai_tile_left },
	{ MOD, XK_Right, arai_tile_right },
	{ MOD, XK_f, arai_max },
	{ MOD, XK_y, arai_fullscreen },
	{ MOD, XK_Tab, arai_cycle }
};
