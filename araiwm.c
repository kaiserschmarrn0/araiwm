#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define LEN(A) sizeof(A)/sizeof(*A)

#define LOG(A) printf("araiwm: " A ".\n");

#define STR_MAX 255

#define NUM_WS 4

#define ATTR 0
#define BIND 1

#define MOD XCB_MOD_MASK_4
#define SHIFT XCB_MOD_MASK_SHIFT

enum { WM_PROTOCOLS, WM_DELETE_WINDOW, WM_COUNT };
enum { NET_SUPPORTED, NET_FULLSCREEN, NET_WM_STATE, NET_COUNT };
enum { PADDING_TOP, PADDING_BOTTOM, GAP, BORDER_WIDTH, NORMAL_COLOR, FOCUS_COLOR, SNAP_MARGIN, SNAP_CORNER };

typedef struct client_t {
	xcb_window_t id;
	struct client_t *next, *prev;
	xcb_rectangle_t snap_geom, full_geom;
	bool ignore_unmap, snap, e_full, i_full;
} client_t;

typedef struct {
	uint16_t mod;
	xcb_keysym_t key;
	void (*function) (int arg);
	int arg;
} keybind_t;

typedef struct {
	char id[STR_MAX];
	xcb_keysym_t key;
} key_map_t;

typedef struct {
	char id[STR_MAX];
	uint32_t mod;
} mod_map_t;

typedef struct {
	char id[STR_MAX];
	void (*function) (int arg);
} func_map_t;

typedef struct {
	char id[STR_MAX];
	uint32_t val;
} attr_map_t;

static xcb_connection_t *conn;
static xcb_ewmh_connection_t *ewmh;
static xcb_screen_t *scr;
static xcb_atom_t wm_atoms[WM_COUNT], net_atoms[NET_COUNT];
static xcb_key_symbols_t *keysyms = NULL;

static client_t *stack[NUM_WS] = { NULL },
                *fwin[NUM_WS] = { NULL };

static bool moving = false,
            resizing = false,
            tabbing = false;

static client_t *marker = NULL;

static int curws = 0,
           x = 0,
           y = 0;

static keybind_t *keys = NULL;
static int keys_len,
           keys_max;
static char *path;

static void close(int arg);
static void cycle(int arg);
static void snap_left(int arg);
static void snap_uleft(int arg);
static void snap_dleft(int arg);
static void snap_right(int arg);
static void snap_uright(int arg);
static void snap_dright(int arg);
static void snap_max(int arg);
static void int_fullscreen(int arg);
static void change_ws(int arg);
static void send_ws(int arg);
static void read_config(int arg);

static mod_map_t mod_map[] = {
	{ "ctrl",  XCB_MOD_MASK_CONTROL },
	{ "alt",   XCB_MOD_MASK_1       },
	{ "super", XCB_MOD_MASK_4       },
	{ "shift", XCB_MOD_MASK_SHIFT   },
};

static key_map_t key_map[] = {
	{ "0",           XK_0           },
	{ "1",           XK_1           },
	{ "2",           XK_2           },
	{ "3",           XK_3           },
	{ "4",           XK_4           },
	{ "5",           XK_5           },
	{ "6",           XK_6           },
	{ "7",           XK_7           },
	{ "8",           XK_8           },
	{ "9",           XK_9           },
	{ "a",           XK_a           },
	{ "b",           XK_b           },
	{ "backspace",   XK_BackSpace   },
	{ "c",           XK_c           },
	{ "d",           XK_d           },
	{ "delete",      XK_Delete      },
	{ "down",        XK_Down        },
	{ "e",           XK_e           },
	{ "end",         XK_End         },
	{ "escape",      XK_Escape      },
	{ "f",           XK_f           },
	{ "g",           XK_g           },
	{ "h",           XK_h           },
	{ "home",        XK_Home        },
	{ "i",           XK_i           },
	{ "j",           XK_j           },
	{ "k",           XK_k           },
	{ "l",           XK_l           },
	{ "left",        XK_Left        },
	{ "m",           XK_m           },
	{ "n",           XK_n           },
	{ "o",           XK_o           },
	{ "p",           XK_p           },
	{ "page_down",   XK_Page_Down   },
	{ "page_up",     XK_Page_Up     },
	{ "pause",       XK_Pause       },
	{ "q",           XK_q           },
	{ "r",           XK_r           },
	{ "return",      XK_Return      },
	{ "right",       XK_Right       },
	{ "s",           XK_s           },
	{ "scroll_lock", XK_Scroll_Lock },
	{ "t",           XK_t           },
	{ "tab",         XK_Tab         },
	{ "u",           XK_u           },
	{ "up",          XK_Up          },
	{ "v",           XK_v           },
	{ "w",           XK_w           },
	{ "x",           XK_x           },
	{ "y",           XK_y           },
	{ "z",           XK_z           },
};

static func_map_t func_map[] = {
	{ "close",          close          },
	{ "cycle",          cycle          },
	{ "snap_left",      snap_left      },
	{ "snap_uleft",     snap_uleft     },
	{ "snap_dleft",     snap_dleft     },
	{ "snap_right",     snap_right     },
	{ "snap_uright",    snap_uright    },
	{ "snap_dright",    snap_dright    },
	{ "snap_max",       snap_max       },
	{ "int_fullscreen", int_fullscreen },
	{ "change_ws",      change_ws      },
	{ "change_ws",      change_ws      },
	{ "change_ws",      change_ws      },
	{ "change_ws",      change_ws      },
	{ "send_ws",        send_ws        },
	{ "read_config",    read_config    },
};

static attr_map_t attr_map[] = {
	{ "padding_top",    0        },
	{ "padding_bottom", 0        },
	{ "gap",            10       },
	{ "border_width",   6        },
	{ "normal_color",   0x3b4252 },
	{ "focus_color",    0x81a1c1 },
	{ "snap_margin",    4        },
	{ "snap_corner",    256      },
};

static void insert(int ws, client_t *subj) {
	subj->next = stack[ws];
	subj->prev = NULL;
	if (stack[ws]) stack[ws]->prev = subj;
	stack[ws] = subj;
}

static client_t *excise(int ws, client_t *subj) {
	if (subj->next) subj->next->prev = subj->prev;
	if (subj->prev) subj->prev->next = subj->next;
	else stack[ws] = subj->next;
	return subj;
}

static client_t *wtf(xcb_window_t id, int *ws) {
	client_t *cur = stack[curws];
	while (cur) {
		if (cur->id == id) {
			if (ws) *ws = curws;
			return cur;
		}
		cur = cur->next;
	}

	for (int i = 0; i < NUM_WS; i++) {
		if (i != curws) {
			cur = stack[i];
			while (cur) {
				if (cur->id == id) {
					if (ws) *ws = i;
					return cur;
				}
				cur = cur->next;
			}
		}
	}

	return NULL;
}

static void w_unmap_ignore(client_t *subj) {
	xcb_unmap_window(conn, subj->id);
	subj->ignore_unmap = true;
}

static void w_map(client_t *subj) {
	xcb_map_window(conn, subj->id);
}

static void w_event_release(client_t *subj) {
	xcb_change_window_attributes(conn, subj->id, XCB_CW_EVENT_MASK, (uint32_t[]){ XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_KEY_RELEASE }); 
}

static void w_event_normal(client_t *subj) {
	xcb_change_window_attributes(conn, subj->id, XCB_CW_EVENT_MASK, (uint32_t[]){ XCB_EVENT_MASK_ENTER_WINDOW }); 
}

static void traverse(client_t *list, void (*func)(client_t *)) {
	for (; list;) {
		func(list);
		list = list->next;
	}
}

static void get_atoms(const char **names, xcb_atom_t *atoms, unsigned int count) {
	int i = 0;
	xcb_intern_atom_cookie_t cookies[count];
	xcb_intern_atom_reply_t *reply;
	for (; i < count; i++) cookies[i] = xcb_intern_atom(conn, 0, strlen(names[i]), names[i]);
	for (i = 0; i < count; i++) {
		reply = xcb_intern_atom_reply(conn, cookies[i], NULL);
		if (reply) {
			atoms[i] = reply->atom;
			free(reply);
		}
	}
}

static void grab_keys() {
	if (keysyms) xcb_key_symbols_free(keysyms);
	keysyms = xcb_key_symbols_alloc(conn);
	xcb_keycode_t *keycode;

	for (int i = 0; i < keys_len; i++) {
		keycode = xcb_key_symbols_get_keycode(keysyms, keys[i].key);
		xcb_grab_key(conn, 0, scr->root, keys[i].mod, *keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
		free(keycode);
	}
}

static void focus(client_t *subj) {
	if (fwin[curws]) xcb_change_window_attributes(conn, fwin[curws]->id, XCB_CW_BORDER_PIXEL, (uint32_t[]){ attr_map[NORMAL_COLOR].val });	
	xcb_change_window_attributes(conn, subj->id, XCB_CW_BORDER_PIXEL, (uint32_t[]){ attr_map[FOCUS_COLOR].val });
	xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, subj->id, XCB_CURRENT_TIME);
	fwin[curws] = subj;
}

static void raise(client_t *subj) {
	if (subj == stack[curws]) return;
	insert(curws, excise(curws, subj));
	xcb_configure_window(conn, subj->id, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]){ XCB_STACK_MODE_ABOVE });
}

static void center_pointer(client_t *subj) {
	xcb_get_geometry_reply_t *temp = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, subj->id), NULL);
	xcb_warp_pointer(conn, XCB_NONE, subj->id, 0, 0, 0, 0, (temp->width + 2 * attr_map[BORDER_WIDTH].val)/2, (temp->height + 2 * attr_map[BORDER_WIDTH].val)/2);
	free(temp);
}

static void die() {
	client_t *temp;
	for (int i = 0; i < NUM_WS; i++) {
		while (stack[i]) {
			temp = stack[i];
			stack[i] = stack[i]->next;
			free(temp);
		}
	}
	xcb_ungrab_key(conn, XCB_GRAB_ANY, scr->root, XCB_MOD_MASK_ANY);
	free(keys);
	xcb_key_symbols_free(keysyms);
	xcb_disconnect(conn);
}

static void close(int arg) {
	if (!fwin[curws]) return;
	xcb_icccm_get_wm_protocols_reply_t pro;
	if (xcb_icccm_get_wm_protocols_reply(conn, xcb_icccm_get_wm_protocols_unchecked(conn, fwin[curws]->id, ewmh->WM_PROTOCOLS), &pro, NULL))
		for (int i = 0; i < pro.atoms_len; i++) {
			if (pro.atoms[i] == wm_atoms[WM_DELETE_WINDOW]) {
				xcb_client_message_event_t ev = { XCB_CLIENT_MESSAGE, 32, 0, fwin[curws]->id, wm_atoms[0] };
				ev.data.data32[0] = wm_atoms[WM_DELETE_WINDOW];
				ev.data.data32[1] = XCB_CURRENT_TIME;
				xcb_send_event(conn, 0, fwin[curws]->id, XCB_EVENT_MASK_NO_EVENT, (char *)&ev);
				goto a;
			}
		}
	xcb_kill_client(conn, fwin[curws]->id);
a:
	xcb_icccm_get_wm_protocols_reply_wipe(&pro);
}

static void cycle_raise(client_t *cur) {
	while (cur != fwin[curws]) {
		client_t *temp = cur->prev;
		raise(cur); 
		cur = temp;
	}
}

static void stop_cycle() {
	tabbing = false;
	traverse(stack[curws], w_event_normal); 
}

static void cycle(int arg) {
	if (!stack[curws] || !stack[curws]->next) return;

	if (!tabbing) {
		traverse(stack[curws], w_event_release);
		marker = fwin[curws];
		tabbing = true;
	}

	if (marker->next) {
		cycle_raise(marker);
		marker = fwin[curws];
		center_pointer(marker->next);
		raise(marker->next);
	} else {
		cycle_raise(marker);
		center_pointer(stack[curws]);
		marker = fwin[curws];
	}
	xcb_flush(conn);
}

static void change_ws(int arg) {
	if (arg == curws) return;

	traverse(stack[arg], w_map);
	traverse(stack[curws], w_unmap_ignore); 
	curws = arg;
	if (fwin[arg]) focus(fwin[curws]); 
	
	xcb_ewmh_set_current_desktop(ewmh, 0, arg);
}

static void send_ws(int arg) {
	if (!fwin[curws] || arg == curws) return;
	if (fwin[curws] != stack[curws]) xcb_configure_window(conn, fwin[curws]->id, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]){ XCB_STACK_MODE_ABOVE });
	fwin[curws]->ignore_unmap = true;
	insert(arg, excise(curws, fwin[curws]));
	xcb_unmap_window(conn, fwin[curws]->id);
}

static void snap_save_state(client_t *subj) {
	xcb_get_geometry_reply_t *temp = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, subj->id), NULL);
	subj->snap_geom.x = temp->x;
	subj->snap_geom.y = temp->y;
	subj->snap_geom.width = temp->width;
	subj->snap_geom.height = temp->height;
	free(temp);
	subj->snap = true;
}

static void snap_restore_state(client_t *subj) {
	subj->snap = false;
	xcb_configure_window(conn, subj->id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (uint32_t[]){ subj->snap_geom.x, subj->snap_geom.y, subj->snap_geom.width, subj->snap_geom.height });
}

#define SNAP_TEMPLATE(A, B, C, D, E) static void A(int arg) { \
	if (!fwin[curws] || fwin[curws]->e_full || fwin[curws]->i_full) return; \
	if (!fwin[curws]->snap) snap_save_state(fwin[curws]); \
	xcb_configure_window(conn, fwin[curws]->id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (uint32_t[]){ B, C, D, E}); \
	if (!moving) { \
		center_pointer(fwin[curws]); \
		raise(fwin[curws]); \
	} \
}

SNAP_TEMPLATE(snap_max, attr_map[GAP].val, attr_map[GAP].val + attr_map[PADDING_TOP].val, scr->width_in_pixels - 2 * attr_map[GAP].val - 2 * attr_map[BORDER_WIDTH].val, scr->height_in_pixels - 2 * attr_map[GAP].val - 2 * attr_map[BORDER_WIDTH].val - attr_map[PADDING_TOP].val - attr_map[PADDING_BOTTOM].val)
SNAP_TEMPLATE(snap_left, attr_map[GAP].val, attr_map[GAP].val + attr_map[PADDING_TOP].val, scr->width_in_pixels / 2 - 1.5 * attr_map[GAP].val - attr_map[BORDER_WIDTH].val * 2, scr->height_in_pixels - 2 * attr_map[GAP].val - 2 * attr_map[BORDER_WIDTH].val - attr_map[PADDING_TOP].val - attr_map[PADDING_BOTTOM].val)
SNAP_TEMPLATE(snap_uleft, attr_map[GAP].val, attr_map[GAP].val + attr_map[PADDING_TOP].val, scr->width_in_pixels / 2 - 1.5 * attr_map[GAP].val - attr_map[BORDER_WIDTH].val * 2, (scr->height_in_pixels - attr_map[PADDING_TOP].val - attr_map[PADDING_BOTTOM].val) / 2 - 1.5 * attr_map[GAP].val - 2 * attr_map[BORDER_WIDTH].val)
SNAP_TEMPLATE(snap_dleft, attr_map[GAP].val, (scr->height_in_pixels - attr_map[PADDING_TOP].val - attr_map[PADDING_BOTTOM].val) / 2 + attr_map[GAP].val / 2 + attr_map[PADDING_TOP].val, scr->width_in_pixels / 2 - 1.5 * attr_map[GAP].val - attr_map[BORDER_WIDTH].val * 2, (scr->height_in_pixels - attr_map[PADDING_TOP].val - attr_map[PADDING_BOTTOM].val) / 2 - 1.5 * attr_map[GAP].val - 2 * attr_map[BORDER_WIDTH].val)
SNAP_TEMPLATE(snap_right, scr->width_in_pixels / 2 + attr_map[GAP].val / 2, attr_map[GAP].val + attr_map[PADDING_TOP].val, scr->width_in_pixels / 2 - 1.5 * attr_map[GAP].val - attr_map[BORDER_WIDTH].val * 2, scr->height_in_pixels - 2 * attr_map[GAP].val - 2 * attr_map[BORDER_WIDTH].val - attr_map[PADDING_TOP].val - attr_map[PADDING_BOTTOM].val)
SNAP_TEMPLATE(snap_uright, scr->width_in_pixels / 2 + attr_map[GAP].val / 2, attr_map[GAP].val + attr_map[PADDING_TOP].val, scr->width_in_pixels / 2 - 1.5 * attr_map[GAP].val - attr_map[BORDER_WIDTH].val * 2, (scr->height_in_pixels - attr_map[PADDING_TOP].val - attr_map[PADDING_BOTTOM].val) / 2 - 1.5 * attr_map[GAP].val - 2 * attr_map[BORDER_WIDTH].val)
SNAP_TEMPLATE(snap_dright, scr->width_in_pixels / 2 + attr_map[GAP].val / 2, (scr->height_in_pixels - attr_map[PADDING_TOP].val - attr_map[PADDING_BOTTOM].val) / 2 + attr_map[GAP].val / 2 + attr_map[PADDING_TOP].val, scr->width_in_pixels / 2 - 1.5 * attr_map[GAP].val - attr_map[BORDER_WIDTH].val * 2, (scr->height_in_pixels - attr_map[PADDING_TOP].val - attr_map[PADDING_BOTTOM].val) / 2 - 1.5 * attr_map[GAP].val - 2 * attr_map[BORDER_WIDTH].val)

static void full_save_state(client_t *subj) {
	raise(subj);
	xcb_get_geometry_reply_t *temp = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, subj->id), NULL);
	subj->full_geom.x = temp->x;
	subj->full_geom.y = temp->y;
	subj->full_geom.width = temp->width;
	subj->full_geom.height = temp->height;
	free(temp);
}

static void full_restore_state(client_t *subj) {
	xcb_configure_window(conn, subj->id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (uint32_t[]){ subj->full_geom.x, subj->full_geom.y, subj->full_geom.width, subj->full_geom.height });
}

static void int_fullscreen(int arg) {
	if (!fwin[curws]) return;
	if (!fwin[curws]->e_full) {
		if (fwin[curws]->i_full) full_restore_state(fwin[curws]); 
		else {
			full_save_state(fwin[curws]);
			xcb_configure_window(conn, fwin[curws]->id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (uint32_t[]){ - attr_map[BORDER_WIDTH].val, - attr_map[BORDER_WIDTH].val, scr->width_in_pixels, scr->height_in_pixels });
		}
	}
	fwin[curws]->i_full = !fwin[curws]->i_full;
}

static void ext_fullscreen(client_t *subj) {
	if (subj->e_full) {
		if (!subj->i_full) full_restore_state(subj);
	} else {
		if (!subj->i_full) full_save_state(subj);
		xcb_configure_window(conn, subj->id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (uint32_t[]){ - attr_map[BORDER_WIDTH].val, - attr_map[BORDER_WIDTH].val, scr->width_in_pixels, scr->height_in_pixels });
	}
	subj->e_full = !subj->e_full;
}

static void map_request(xcb_generic_event_t *ev) {
	xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;
	if (wtf(e->window, NULL)) return;

	xcb_get_geometry_reply_t *init_geom = xcb_get_geometry_reply(conn, xcb_get_geometry_unchecked(conn, e->window), NULL);

	xcb_map_window(conn, e->window);

	xcb_ewmh_get_atoms_reply_t type;
	if (xcb_ewmh_get_wm_window_type_reply(ewmh, xcb_ewmh_get_wm_window_type(ewmh, e->window), &type, NULL)) {
		for (unsigned int i = 0; i < type.atoms_len; i++)
			if (type.atoms[i] == ewmh->_NET_WM_WINDOW_TYPE_DOCK || type.atoms[i] == ewmh->_NET_WM_WINDOW_TYPE_TOOLBAR || type.atoms[i] == ewmh->_NET_WM_WINDOW_TYPE_DESKTOP) {
			xcb_ewmh_get_atoms_reply_wipe(&type);
			return;
		}
		xcb_ewmh_get_atoms_reply_wipe(&type);
	}

	xcb_query_pointer_reply_t *ptr = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, scr->root), NULL);

	uint32_t vals[] = { 0, 0, attr_map[BORDER_WIDTH].val };
	if (ptr->root_x < init_geom->width / 2 + attr_map[BORDER_WIDTH].val) vals[0] = 0; 
	else if (ptr->root_x + init_geom->width / 2 + attr_map[BORDER_WIDTH].val > scr->width_in_pixels) vals[0] = scr->width_in_pixels - init_geom->width - 2 * attr_map[BORDER_WIDTH].val;
	else vals[0] = ptr->root_x - init_geom->width / 2 - attr_map[BORDER_WIDTH].val;
	if (ptr->root_y < init_geom->height / 2 + attr_map[BORDER_WIDTH].val) vals[1] = 0;
	else if (ptr->root_y + init_geom->height / 2 + attr_map[BORDER_WIDTH].val > scr->height_in_pixels) vals[1] = scr->height_in_pixels - init_geom->height - 2 * attr_map[BORDER_WIDTH].val;
	else vals[1] = ptr->root_y - init_geom->height / 2 - attr_map[BORDER_WIDTH].val;

	xcb_change_window_attributes(conn, e->window, XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK, (uint32_t[]){ attr_map[NORMAL_COLOR].val, XCB_EVENT_MASK_ENTER_WINDOW });
	xcb_configure_window(conn, e->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_BORDER_WIDTH, vals);
	free(ptr);

	free(init_geom);

	client_t *new = malloc(sizeof(client_t));
	new->id = e->window;
	new->ignore_unmap = false;
	new->snap = false;
	new->e_full = false;
	new->i_full = false;
	insert(curws, new);
	
	if (!tabbing && !moving && !resizing) focus(new);
}

static void enter_notify(xcb_generic_event_t *ev) {
	xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;
	client_t *found = wtf(e->event, NULL);
	if (!found || found == fwin[curws]) return;
	focus(found);
}

static void button_press(xcb_generic_event_t *ev) {
	xcb_button_press_event_t *e = (xcb_button_press_event_t *)ev;
	client_t *found = wtf(e->child, NULL);
	if (!found) return;
	
	if (found != fwin[curws]) focus(found);
	raise(found);
	if (found->e_full || found->i_full) return;

	xcb_get_geometry_reply_t *geom = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, fwin[curws]->id), NULL);
	if (e->detail == XCB_BUTTON_INDEX_1) {
		if (fwin[curws]->snap) {
			x = fwin[curws]->snap_geom.width * (e->event_x - geom->x) / geom->width;
			y = fwin[curws]->snap_geom.height * (e->event_y - geom->y) / geom->height; 
		} else	{
			x = e->event_x - geom->x;
			y = e->event_y - geom->y;
		}
		moving = true; 
	} else {
		fwin[curws]->snap = false;
		x = geom->width - e->event_x;
		y = geom->height - e->event_y;
		resizing = true;
	}

	free(geom);
	xcb_grab_pointer(conn, 0, scr->root, XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION_HINT, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, scr->root, XCB_NONE, XCB_CURRENT_TIME);
}

static void motion_notify(xcb_generic_event_t *ev) {
	xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;

	xcb_query_pointer_reply_t *p = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, scr->root), 0);
	if (!p) return;
	
	if (moving) {

		if (p->root_x < attr_map[SNAP_MARGIN].val) {
			if (p->root_y < attr_map[SNAP_CORNER].val) snap_uleft(0);
			else if (p->root_y > scr->height_in_pixels - attr_map[SNAP_CORNER].val) snap_dleft(0);
			else snap_left(0);
		} else if (p->root_y < attr_map[SNAP_MARGIN].val) {
			if (p->root_x < attr_map[SNAP_CORNER].val) snap_uleft(0);
			else if (p->root_x > scr->width_in_pixels - attr_map[SNAP_CORNER].val) snap_uright(0);
			else snap_max(0);
		} else if (p->root_x > scr->width_in_pixels - attr_map[SNAP_MARGIN].val) {
			if (p->root_y < attr_map[SNAP_CORNER].val) snap_uright(0);
			else if (p->root_y > scr->height_in_pixels - attr_map[SNAP_CORNER].val) snap_dright(0);
			else snap_right(0);
		} else if (p->root_y > scr->height_in_pixels - attr_map[SNAP_MARGIN].val) {
			if (p->root_x < attr_map[SNAP_CORNER].val) snap_dleft(0);
			else if (p->root_x > scr->width_in_pixels - attr_map[SNAP_CORNER].val) snap_dright(0);
			else goto b;
		} else {
b:
			if (fwin[curws]->snap) snap_restore_state(fwin[curws]);
			xcb_configure_window(conn, fwin[curws]->id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (uint32_t[]){ p->root_x - x, p->root_y - y });
			goto a;
		}
	} else if (resizing) xcb_configure_window(conn, fwin[curws]->id, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (uint32_t[]){ p->root_x + x, p->root_y + y });
a:
	free(p);
}

static void button_release(xcb_generic_event_t *ev) {
	xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
	resizing = moving = false;
}

static void key_press(xcb_generic_event_t *ev) {
	xcb_key_press_event_t *e = (xcb_key_press_event_t *)ev;
	xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keysyms, e->detail, 0);

	if (keysym != XK_Tab && tabbing) {
		stop_cycle();
	}

	for (int i = 0; i < keys_len; i++) {
		if (keysym == keys[i].key && keys[i].mod == e->state) {
			keys[i].function(keys[i].arg);
			break;
		}
	}
}

static void key_release(xcb_generic_event_t *ev) {
	xcb_key_release_event_t *e = (xcb_key_release_event_t *)ev;
	xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keysyms, e->detail, 0);

	if (keysym == XK_Super_L && tabbing) stop_cycle();
}

static void forget_client(client_t *subj, int ws) {
	if ((moving || resizing) && subj == fwin[curws]) button_release(NULL);
	
	free(excise(ws, subj));
	
	if (ws == curws) {
		if (fwin[curws] == subj) {
			if (!stack[curws]) fwin[curws] = NULL;
			else focus(stack[curws]);
		}
	}
}

static void unmap_notify(xcb_generic_event_t *ev) {
	xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;
	int ws;
	client_t *found = wtf(e->window, &ws);
	if (!found) return; 

	if (found->ignore_unmap) found->ignore_unmap = false;
	else forget_client(found, ws);
}

static void destroy_notify(xcb_generic_event_t *ev) {
	xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;
	int ws;
	client_t *found = wtf(e->window, &ws);
	if (found) forget_client(found, ws);
}

static void client_message(xcb_generic_event_t *ev) {
	xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;
	client_t *found = wtf(e->window, NULL);
	if (!found) return;

	if (e->type == ewmh->_NET_WM_STATE) {
		uint32_t action = e->data.data32[0];
		for (int i = 1; i < 3; i++) {
			xcb_atom_t atom = (xcb_atom_t)e->data.data32[i];
			if (atom == ewmh->_NET_WM_STATE_FULLSCREEN) {
				if (action == XCB_EWMH_WM_STATE_ADD && !found->e_full) ext_fullscreen(found);
				else if (action == XCB_EWMH_WM_STATE_REMOVE && found->e_full) ext_fullscreen(found);
				else if (action == XCB_EWMH_WM_STATE_TOGGLE) ext_fullscreen(found);
			}
		}
	}
}

static void mapping_notify(xcb_generic_event_t *ev) {
	xcb_mapping_notify_event_t *e = (xcb_mapping_notify_event_t *)ev;
	if (e->request != XCB_MAPPING_MODIFIER && e->request != XCB_MAPPING_KEYBOARD) return;
	xcb_ungrab_key(conn, XCB_GRAB_ANY, scr->root, XCB_MOD_MASK_ANY);
	grab_keys();
}

static int mask_to_geo(xcb_configure_request_event_t *e, uint32_t *vals) {
	int i = 0;
	if (e->value_mask & XCB_CONFIG_WINDOW_X) vals[i++] = e->x;
	if (e->value_mask & XCB_CONFIG_WINDOW_Y) vals[i++] = e->y;
	if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH) vals[i++] = e->width;
	if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) vals[i++] = e->height;
	return i;
}

static void configure_request(xcb_generic_event_t *ev) {
	xcb_configure_request_event_t *e = (xcb_configure_request_event_t *)ev;
	client_t *found = wtf(e->window, NULL);
	
	uint32_t vals[6];

	if (!found) {
		int i = mask_to_geo(e, vals);
		if (e->value_mask & XCB_CONFIG_WINDOW_SIBLING) vals[i++] = e->sibling;
		if (e->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) vals[i] = e->stack_mode;
		xcb_configure_window(conn, e->window, e->value_mask, vals);
		return;
	}

	if (!found->i_full && !found->e_full) {
		mask_to_geo(e, vals);
		xcb_configure_window(conn, found->id, e->value_mask & ~(XCB_CONFIG_WINDOW_STACK_MODE | XCB_CONFIG_WINDOW_SIBLING), vals);
	}
}

static int first(char key, int depth, int l, int r) {
	int piv = l + (r - l) / 2;
	if (l == r + 1) return piv;
	else if (key_map[piv].id[depth] > key - 1) return first(key, depth, l, piv - 1);
	else if (key_map[piv].id[depth] < key) return first(key, depth, piv + 1, r);
}

static int search(char *key, int depth, int l, int r) {
	int a = first(key[depth], depth, l, r);
	int b = first(key[depth]+1, depth, l, r) - 1;
	if (a == b) return a;
	else return search(key, depth+1, a, b);
}

static void add_key(keybind_t subject) {
	if (keys_len + 1 == keys_max) {
		keys_max *= 2;
		keybind_t *temp = malloc(keys_max * sizeof(keybind_t));
		for (int i = 0; i < keys_len; i++) *(temp + i) = *(keys + i);
		free(keys);
		keys = temp;
	}
	*(keys + keys_len) = subject;
	keys_len++;
}

int nscmp(char *one, char *two) {
	for (int i = 0; *one && *two; i++)
		if (*one == ' ') one++;
		else if (*one == ' ') two++;
		else if (*one++ != *two++) return 0;
	return 1;
}

char kgetc(FILE *subj) {
	char found = fgetc(subj);
	while (found == ' ') found = fgetc(subj);
	return found;
}

static void get_mod(FILE *config, char *scan, keybind_t *new) {
	for (int i = 0; i < LEN(mod_map); i++) {
		if (nscmp(scan, mod_map[i].id)) new->mod |= mod_map[i].mod;
	}
}

static void read_config(int arg) {
	FILE *config;
	if (!(config = fopen(path, "r"))) {
		LOG("can't open config");
		return;	
	}

	if (keys) free(keys);
	keys_max = 8;
	keys = malloc(keys_max * sizeof(keybind_t));
	
	int mode = ATTR,
			i;
	char scan[STR_MAX] = { '\0' },
	     c;

	c = kgetc(config);
	for (keys_len = 0; c != EOF;) {
		
		//get first token
		for (i = 0; c != '\n' && c != '+' && c != '='; i++) {
			scan[i] = c;
			c = kgetc(config);
		}
		scan[i] = '\0';

		//mode
		if (nscmp(scan, "attr")) mode = ATTR;
		else if (nscmp(scan, "bind") ) mode = BIND;
		else if (mode == ATTR) {
			c = kgetc(config);
			for (int i = 0; i < LEN(attr_map) ; i++) {
				if (nscmp(scan, attr_map[i].id)) {
					int j;
					for (j = 0; c != '\n'; j++) {
						scan[j] = c;
						c = kgetc(config);
					}
					scan[j] = '\0';

					//hex or dec
					if (nscmp(attr_map[i].id, "normal_color") || nscmp(attr_map[i].id, "focus_color")) attr_map[i].val = strtoul(scan, NULL, 16);
					else attr_map[i].val = atoi(scan);
				}
			}
		} else if (mode == BIND) {
			keybind_t new;
			new.mod = 0;
			
			//account for first modifier
			get_mod(config, scan, &new);

			//other modifiers
			c = kgetc(config);
			while (c != '=') {
				for (i = 0; c!= '+' && c != '='; i++) {
					scan[i] = c;
					c = kgetc(config);
				}
				
				scan[i] = '\0';
				
				if (c == '=') break;
				
				get_mod(config, scan, &new);
				c = kgetc(config);
			}
			
			//get actual key
			int found = search(scan, 0, 0, LEN(key_map) - 1);
			new.key = key_map[found].key;
			
			//get function
			c = kgetc(config);
			for (i = 0; c != ' ' && c != '\n'; i++) {
			  scan[i] = c;
			  c = fgetc(config);
			}
			scan[i] = '\0';
			
			for (int i = 0; i < LEN(func_map); i ++) {
				if (nscmp(scan, func_map[i].id)) new.function = func_map[i].function;
			}

			//check for arg
			if (c != '\n') {
				for (i = 0; c != '\n'; i++) {
					scan[i] = c;
					c = kgetc(config);
				}
				scan[i+1] = '\0';
				new.arg = atoi(scan);
			}
			
			add_key(new);
		}
		
		c = kgetc(config);
	}

	fclose(config);

	grab_keys();
}

int main(int argc, char **argv) {

	if (argc > 2) {
		LOG("too many arguments");
		return 0;
	} else if (argc < 2) {
		LOG("no config specified");
		return 0;
	} 

	path = argv[1];

	conn = xcb_connect(NULL, NULL);
	scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

	xcb_change_window_attributes(conn, scr->root, XCB_CW_EVENT_MASK, (uint32_t[]){ XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT });

	ewmh = calloc(1, sizeof(xcb_ewmh_connection_t));
	xcb_ewmh_init_atoms_replies(ewmh, xcb_ewmh_init_atoms(conn, ewmh), (void *)0);

	const char *WM_ATOM_NAME[] = { "WM_PROTOCOLS", "WM_DELETE_WINDOW" };
	get_atoms(WM_ATOM_NAME, wm_atoms, WM_COUNT);
	const char *NET_ATOM_NAME[] = { "_NET_SUPPORTED", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE" };
	get_atoms(NET_ATOM_NAME, net_atoms, NET_COUNT);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root, net_atoms[NET_SUPPORTED], XCB_ATOM_ATOM, 32, NET_COUNT, net_atoms);

	read_config(0);

	xcb_grab_button(conn, 0, scr->root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, scr->root, XCB_NONE, XCB_BUTTON_INDEX_1, XCB_MOD_MASK_4);
	xcb_grab_button(conn, 0, scr->root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, scr->root, XCB_NONE, XCB_BUTTON_INDEX_3, XCB_MOD_MASK_4);
	
	static void (*events[XCB_NO_OPERATION])(xcb_generic_event_t *event);
	for (int i = 0; i < XCB_NO_OPERATION; i++) events[i] = NULL;
	events[XCB_BUTTON_PRESS]				= button_press;
	events[XCB_BUTTON_RELEASE]			= button_release;
	events[XCB_MOTION_NOTIFY]			= motion_notify;
	events[XCB_CLIENT_MESSAGE]			= client_message;
	events[XCB_CONFIGURE_REQUEST] = configure_request;
	events[XCB_KEY_PRESS]					= key_press;
	events[XCB_KEY_RELEASE]				= key_release;
	events[XCB_MAP_REQUEST]				= map_request;
	events[XCB_UNMAP_NOTIFY]				= unmap_notify;
	events[XCB_DESTROY_NOTIFY]			= destroy_notify;
	events[XCB_ENTER_NOTIFY]				= enter_notify;
	events[XCB_MAPPING_NOTIFY]			= mapping_notify;
	
	atexit(die);

	xcb_generic_event_t *ev;
	for (; !xcb_connection_has_error(conn);) {
		xcb_flush(conn);
		ev = xcb_wait_for_event(conn);
		if (events[ev->response_type & ~0x80]) events[ev->response_type & ~0x80](ev);
		free(ev);
	}

	return 0;
}
