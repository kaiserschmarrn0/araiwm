#include "araiwm.h"

/* attributes */

#define NUM_WS 4

#define TOP 34
#define BOT 0
#define GAP 10
#define BORDER 0

#define FOCUSCOL 0x81a1c1
#define UNFOCUSCOL 0x3b4252 

#define SNAP_MARGIN 5
#define SNAP_CORNER 256

//ignore gaps when maxed
#define SNAP_MAX_SMART

/* keyboard modifiers */

#define MOD XCB_MOD_MASK_4
#define SHIFT XCB_MOD_MASK_SHIFT

/* mouse controls */

static const button buttons[] = {
	{ MOD, XCB_BUTTON_INDEX_1, mouse_move   },
	{ MOD, XCB_BUTTON_INDEX_3, mouse_resize },
};

/* keyboard controls */

static const keybind keys[] = {
	{ MOD,         XK_q,     close,     0 },
	{ MOD,         XK_Tab,   cycle,     0 },
	{ MOD,         XK_Left,  snap_l,    0 },
	{ MOD,         XK_Right, snap_r,    0 },
	{ MOD,         XK_f,     snap_max,  0 },
	{ MOD | SHIFT, XK_f,     int_full,  0 },
	{ MOD,         XK_1,     change_ws, 0 },
	{ MOD,         XK_2,     change_ws, 1 },
	{ MOD,         XK_3,     change_ws, 2 },
	{ MOD,         XK_4,     change_ws, 3 },
	{ MOD | SHIFT, XK_1,     send_ws,   0 },
	{ MOD | SHIFT, XK_2,     send_ws,   1 },
	{ MOD | SHIFT, XK_3,     send_ws,   2 },
	{ MOD | SHIFT, XK_4,     send_ws,   3 },
};
