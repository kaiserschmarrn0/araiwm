//comment this if you don't want a config file
#define CONFIG_FILE

#include "araiwm.h"

//maximum path length for config file
#define STR_MAX         255

#define MOD		XCB_MOD_MASK_4
#define SHIFT		XCB_MOD_MASK_SHIFT
#define NUM_WS		4

#ifndef CONFIG_FILE
#define BORDER		5
#define GAP		9
#define TOP		33
#define BOT		0
#define FOCUSCOLOR	0x9baeb1
#define UNFOCUSCOLOR	0x12333b
#define SNAP_X		4
#define SNAP_Y		200
#endif

static const button buttons[] = {
//	Modkey	Mouse button
	{ MOD, 	1 },
	{ MOD, 	3 }
};

static const key keys[] = {
//	Modkey		Key	Function	Arg
	{ MOD, 		XK_q,	arai_kill, 	0 },
	{ MOD, 		XK_g,	arai_center, 	0 },
	{ MOD, 		XK_Tab,	arai_cycle,	0 },
	{ MOD, 		XK_f, 	arai_snap,	7 },
	{ MOD | SHIFT, 	XK_f,	arai_max,	0 },
	{ MOD, 		XK_1,	arai_chws, 	0 },
	{ MOD, 		XK_2, 	arai_chws, 	1 },
	{ MOD, 		XK_3, 	arai_chws, 	2 },
	{ MOD, 		XK_4, 	arai_chws, 	3 },
	{ MOD | SHIFT, 	XK_1, 	arai_sendws, 	0 },
	{ MOD | SHIFT, 	XK_2, 	arai_sendws, 	1 },
	{ MOD | SHIFT, 	XK_3, 	arai_sendws, 	2 },
	{ MOD | SHIFT, 	XK_4, 	arai_sendws, 	3 },
};
