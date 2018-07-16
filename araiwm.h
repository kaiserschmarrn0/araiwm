typedef struct {
    uint16_t mod;
    xcb_keysym_t key;
    void (*function) (int arg);
    int arg;
} key;

typedef struct {
    uint16_t mod, button;
} button;

typedef struct client {
    xcb_window_t id;
    struct client *next;
    int max, app_max, x, y, w, h;
} client;

typedef struct {
    xcb_window_t id;
    int mode, xoff, yoff;
} pass;

#ifdef CONFIG_FILE
typedef struct {    
    char *key;
    uint32_t *var;
} confitem;
#endif

static void arai_kill();
static void arai_center();
static void arai_cycle();
static void arai_maxhelper();
static void arai_chws();
static void arai_sendws();
static void arai_snap();
