#include "types.h"

#define MOD		XCB_MOD_MASK_4
#define SHIFT		XCB_MOD_MASK_SHIFT
#define BORDER		4
#define GAP		6
#define TOP		24
#define BOT		0
#define FOCUSCOLOR	0xFFFFFF
#define UNFOCUSCOLOR	0x839496
#define NUM_WS		4
#define SNAP_X		4
#define SNAP_Y		128

static void arai_kill();
static void arai_center();
static void arai_cycle();

static const arai_button buttons[] = {
	{ MOD, 1 },
	{ MOD, 3 }
};

static const voidkey voidkeys[] = {
	{ MOD, XK_q, arai_kill},
	{ MOD, XK_g, arai_center },
	{ MOD, XK_Tab, arai_cycle },
};

static const argkey wskeys[] = {
	{ MOD, XK_1},
	{ MOD, XK_2},
	{ MOD, XK_3},
	{ MOD, XK_4},
};

static const argkey sendkeys[] = {
	{ MOD | SHIFT, XK_1},
	{ MOD | SHIFT, XK_2},
	{ MOD | SHIFT, XK_3},
	{ MOD | SHIFT, XK_4}
};
