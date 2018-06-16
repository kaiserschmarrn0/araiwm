typedef struct {
	uint16_t mod;
	xcb_keysym_t key;
	void (*function) (int arg);
	int arg;
	int fwin;
} key;

typedef struct {
	uint16_t mod;
	uint16_t button;
} button;

typedef struct client {
	xcb_window_t id;
	struct client* next;
} client;

static void arai_kill();
static void arai_center();
static void arai_cycle();
static void arai_chws();
static void arai_sendws();
