#include <xcb/xcb.h>

typedef struct {
	uint16_t mod;
	xcb_keysym_t key;

	void (*function) (int arg);
	int arg;
} keybind;

typedef struct {
	uint16_t mod;
	uint32_t button;

	void (*function) (xcb_window_t win, uint32_t event_x, uint32_t event_y);
} button;

static void close(int arg);
static void cycle(int arg);

static void snap_l(int arg);
static void snap_lu(int arg);
static void snap_ld(int arg);
static void snap_r(int arg);
static void snap_ru(int arg);
static void snap_rd(int arg);
static void snap_max(int arg);

static void int_full(int arg);

static void change_ws(int arg);
static void send_ws(int arg);

static void mouse_move(xcb_window_t win, uint32_t event_x, uint32_t event_y);
static void mouse_resize(xcb_window_t win, uint32_t event_x, uint32_t event_y);
