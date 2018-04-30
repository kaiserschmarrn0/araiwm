#include <xcb/xcb_keysyms.h>

typedef struct {
	uint16_t mod;
	xcb_keysym_t key;
	void (*function) (xcb_window_t);
} arai_key;

typedef struct {
	uint16_t mod;
	uint16_t button;
	void (*function) (xcb_window_t);
} arai_button;

typedef struct {
	uint16_t type;
	void (*function) (xcb_generic_event_t*);
} arai_event;
