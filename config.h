#include "types.h"

#define MOD		XCB_MOD_MASK_4
#define BORDER		4
#define GAP		6
#define TOP		24
#define BOT		0
#define FOCUSCOLOR	0xFFFFFF
#define UNFOCUSCOLOR	0x839496
#define NUM_WS		4
#define SNAP		4
//#define UNFOCUSCOLOR	0x9aaeb0

//#define UNFOCUSCOLOR	0xE4D1B0
//#define FOCUSCOLOR	0xa9a9a8
//#define UNFOCUSCOLOR	0x071d22

static void arai_kill(xcb_window_t window);
static void arai_center(xcb_window_t window);
static void arai_cycle(xcb_window_t window);

arai_button buttons[] = {
	{ MOD, 1 },
	{ MOD, 3 }
};

arai_key keys[] = {
	{ MOD, XK_q, arai_kill},
	{ MOD, XK_g, arai_center },
	{ MOD, XK_Tab, arai_cycle },
	{ MOD, XK_1, NULL },
	{ MOD, XK_2, NULL },
	{ MOD, XK_3, NULL },
	{ MOD, XK_4, NULL },
};
