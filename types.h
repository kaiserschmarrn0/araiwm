typedef struct {
	uint16_t mod;
	xcb_keysym_t key;
	void (*function) (int arg);
	int arg, fwin;
} key;

typedef struct {
	uint16_t mod, button;
} button;

typedef struct client {
	xcb_window_t id;
	struct client* next;
	int max, x, y, w, h;
} client;

static void arai_kill();
static void arai_center();
static void arai_cycle();
static void max();
static void arai_chws();
static void arai_sendws();
