typedef struct {
	uint16_t mod;
	xcb_keysym_t key;
	void (*function) (xcb_window_t);
} arai_key;

typedef struct {
	uint16_t mod;
	uint16_t button;
} arai_button;
