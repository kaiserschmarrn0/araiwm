#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>

#include "config.h"

#define LEN(A) sizeof(A)/sizeof(*A)

#define LOG(A) printf("araiwm: " A ".\n");

enum { DEFAULT, MOVE, RESIZE, CYCLE, };
enum { WM_PROTOCOLS, WM_DELETE_WINDOW, WM_COUNT, };
enum { NET_SUPPORTED, NET_FULLSCREEN, NET_WM_STATE, NET_COUNT, };

typedef struct window {
	struct window *next;
	struct window *prev;

	xcb_window_t child;

	xcb_rectangle_t snap;
	int is_snap;
	
	xcb_rectangle_t full;	
	int is_i_full;
	int is_e_full;

	int ignore_unmap;
} window;

static xcb_connection_t *conn;
static xcb_ewmh_connection_t *ewmh;
static xcb_screen_t *scr;

static xcb_atom_t wm_atoms[WM_COUNT];
static xcb_atom_t net_atoms[NET_COUNT];

static xcb_key_symbols_t *keysyms = NULL;

static window *stack[NUM_WS] = { NULL };
static window *fwin[NUM_WS] = { NULL };

static unsigned int state = DEFAULT;

static window *marker = NULL;

static int curws = 0;
static uint32_t x = 0;
static uint32_t y = 0;

static void insert(int ws, window *subj) {
	subj->next = stack[ws];
	subj->prev = NULL;

	if (stack[ws]) {
		stack[ws]->prev = subj;
	}

	stack[ws] = subj;
}

static window *excise(int ws, window *subj) {
	if (subj->next) {
		subj->next->prev = subj->prev;
	}
	
	if (subj->prev) {
		subj->prev->next = subj->next;
	} else {
		stack[ws] = subj->next;
	}

	return subj;
}

static window *ws_wtf(xcb_window_t id, int ws) {
	window *cur;
	for (cur = stack[ws]; cur; cur = cur->next) {
		if (cur->child == id) {
			break;
		}
	}
	return cur;
}

static window *all_wtf(xcb_window_t id, int *ws) {
	window *ret;
	for (int i = 0; i < NUM_WS; i++) {
		ret = ws_wtf(id, i);
		if (ret) {
			if (ws) {
				*ws = i;
			}
			break;
		}
	}
	return ret;
}

static void ignore_unmap(window *subj) {
	xcb_unmap_window(conn, subj->child);
	subj->ignore_unmap = 1;
}

static void map(window *subj) {
	xcb_map_window(conn, subj->child);
}

static void release_events(window *subj) {
	uint32_t mask = XCB_CW_EVENT_MASK;
	uint32_t val = XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_KEY_RELEASE;
	xcb_change_window_attributes(conn, subj->child, mask, &val); 
}

static void normal_events(window *subj) {
	uint32_t mask = XCB_CW_EVENT_MASK;
	uint32_t val = XCB_EVENT_MASK_ENTER_WINDOW;
	xcb_change_window_attributes(conn, subj->child, mask, &val); 
}

static void move_resize(xcb_window_t win, int x, int y, int w, int h) {
	uint32_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_X |
			XCB_CONFIG_WINDOW_Y;
	uint32_t vals[4];
	vals[0] = x;
	vals[1] = y;
	vals[2] = w;
	vals[3] = h;
	xcb_configure_window(conn, win, mask, vals);
}

static void move(xcb_window_t win, uint32_t x, uint32_t y) {
	uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
	uint32_t vals[2];
	vals[0] = x;
	vals[1] = y;
	xcb_configure_window(conn, win, mask, vals);
}

static void resize(xcb_window_t win, int w, int h) {
	uint32_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
	uint32_t vals[2];
	vals[0] = w;
	vals[1] = h;
	xcb_configure_window(conn, win, mask, vals);
}

static xcb_get_geometry_reply_t *w_get_geometry(xcb_window_t win) {
	xcb_get_geometry_cookie_t cookie = xcb_get_geometry(conn, win);
	return xcb_get_geometry_reply(conn, cookie, NULL);
}

static xcb_query_pointer_reply_t *w_query_pointer() {
	xcb_query_pointer_cookie_t cookie = xcb_query_pointer(conn, scr->root);
	return xcb_query_pointer_reply(conn, cookie, NULL);
}

static void traverse(window *list, void (*func)(window *)) {
	for (; list;) {
		window *temp = list;
		list = temp->next;
		func(temp);
	}
}

static void get_atoms(const char **names, xcb_atom_t *atoms, unsigned int count) {
	xcb_intern_atom_cookie_t cookies[count];
	for (int i = 0; i < count; i++) {
		cookies[i] = xcb_intern_atom(conn, 0, strlen(names[i]), names[i]);
	}
	for (int i = 0; i < count; i++) {
		xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookies[i], NULL);
		if (reply) {
			atoms[i] = reply->atom;
			free(reply);
		}
	}
}

static void grab_keys() {
	xcb_key_symbols_free(keysyms);

	keysyms = xcb_key_symbols_alloc(conn);

	for (int i = 0; i < LEN(keys); i++) {
		xcb_keycode_t *keycode = xcb_key_symbols_get_keycode(keysyms, keys[i].key);
		xcb_grab_key(conn, 0, scr->root, keys[i].mod, *keycode, XCB_GRAB_MODE_ASYNC,
				XCB_GRAB_MODE_ASYNC);
		free(keycode);
	}
}

static void color(xcb_window_t win, uint32_t val) {
	uint32_t mask = XCB_CW_BORDER_PIXEL;
	xcb_change_window_attributes(conn, win, mask, &val);
}

static void focus(window *subj) {
	if (subj == fwin[curws]) {
		return;
	}

	if (fwin[curws]) {
		color(fwin[curws]->child, UNFOCUSCOL);
	}

	color(subj->child, FOCUSCOL);
	
	xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, subj->child, XCB_CURRENT_TIME);
	fwin[curws] = subj;
}

static void stack_above(window *subj) {
	uint32_t mask = XCB_CONFIG_WINDOW_STACK_MODE;
	uint32_t val  = XCB_STACK_MODE_ABOVE;
	xcb_configure_window(conn, subj->child, mask, &val);
}

static void raise(window *subj) {
	if (subj == stack[curws]) {
		return;
	}

	insert(curws, excise(curws, subj));

	stack_above(subj);
}

static void center_pointer(window *subj) {
	xcb_get_geometry_reply_t *temp = w_get_geometry(subj->child);
	uint32_t x = (temp->width + 2 * BORDER)/2;
	uint32_t y = (temp->height + 2 * BORDER)/2;
	xcb_warp_pointer(conn, XCB_NONE, subj->child, 0, 0, 0, 0, x, y);
	free(temp);
}

static void kill(xcb_window_t win) {
	xcb_icccm_get_wm_protocols_reply_t pro;
	xcb_get_property_cookie_t cookie;
	cookie = xcb_icccm_get_wm_protocols_unchecked(conn, win, ewmh->WM_PROTOCOLS);
	if (!xcb_icccm_get_wm_protocols_reply(conn, cookie, &pro, NULL)) {
		xcb_kill_client(conn, win);
		return;
	}

	for (int i = 0; i < pro.atoms_len; i++) {
		if (pro.atoms[i] == wm_atoms[WM_DELETE_WINDOW]) {
			xcb_icccm_get_wm_protocols_reply_wipe(&pro);
			goto a;
		}
	}
	xcb_icccm_get_wm_protocols_reply_wipe(&pro);
	xcb_kill_client(conn, win);
	return;

	a:;
	
	xcb_client_message_event_t ev;
	ev.response_type = XCB_CLIENT_MESSAGE;
	ev.format = 32;
	ev.sequence = 0;
	ev.window = win;
	ev.type = wm_atoms[WM_PROTOCOLS];
	ev.data.data32[0] = wm_atoms[WM_DELETE_WINDOW];
	ev.data.data32[1] = XCB_CURRENT_TIME;
	uint32_t mask = XCB_EVENT_MASK_NO_EVENT;
	xcb_send_event(conn, 0, win, mask, (char *)&ev);	
}

static void close(int arg) {
	if (fwin[curws]) {
		kill(fwin[curws]->child);
	}
}

static void cycle_raise(window *cur) {
	for (; cur != fwin[curws];) {
		window *temp = cur->prev;
		raise(cur); 
		cur = temp;
	}
}

static void stop_cycle() {
	state = DEFAULT;
	traverse(stack[curws], normal_events); 
}

static void cycle(int arg) {
	if (!stack[curws] || !stack[curws]->next) {
		return;
	}

	if (state != CYCLE) {
		traverse(stack[curws], release_events);
		marker = fwin[curws];
		state = CYCLE;
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
}

static void change_ws(int arg) {
	if (arg == curws) {
		return;
	}
	
	traverse(stack[arg], map);
	traverse(stack[curws], ignore_unmap); 
	
	if (fwin[arg]) {
		focus(fwin[arg]);
	}
	
	curws = arg;
}

static void send_ws(int arg) {
	if (!fwin[curws] || arg == curws) {
		return;
	}

	stack_above(fwin[curws]);
	
	fwin[curws]->ignore_unmap = 1;
	insert(arg, excise(curws, fwin[curws]));
	xcb_unmap_window(conn, fwin[curws]->child);
}

static void save_state(xcb_window_t win, xcb_rectangle_t *state) {
	xcb_get_geometry_reply_t *temp = w_get_geometry(win);
	state->x = temp->x;
	state->y = temp->y;
	state->width = temp->width;
	state->height = temp->height;
	free(temp);
}

static void snap_save_state(window *win) {
	save_state(win->child, &win->snap);
	
	win->is_snap = 1;
}

static void snap_restore_state(window *win) {
	move_resize(win->child, win->snap.x, win->snap.y, win->snap.width, win->snap.height);
	
	win->is_snap = 0;
}

#define SNAP_TEMPLATE(A, B, C, D, E) static void A(int arg) {                   \
	if (!fwin[curws] || fwin[curws]->is_e_full || fwin[curws]->is_i_full) { \
		return;                                                         \
	}                                                                       \
	                                                                        \
	if (!fwin[curws]->is_snap) {                                            \
		snap_save_state(fwin[curws]);                                   \
	}                                                                       \
	                                                                        \
	move_resize(fwin[curws]->child, B, C, D, E);                            \
	                                                                        \
	if (state == MOVE) {                                                    \
		return;                                                         \
	}                                                                       \
	                                                                        \
	center_pointer(fwin[curws]);                                            \
	raise(fwin[curws]);                                                     \
}

#ifndef SNAP_MAX_SMART
SNAP_TEMPLATE(snap_max,
	GAP,
	GAP + TOP,
	scr->width_in_pixels - 2 * GAP - 2 * BORDER,
	scr->height_in_pixels - 2 * GAP - 2 * BORDER - TOP - BOT)
#endif
#ifdef SNAP_MAX_SMART 
SNAP_TEMPLATE(snap_max,
	0,
	TOP,
	scr->width_in_pixels - 2 * BORDER,
	scr->height_in_pixels - 2 * BORDER - TOP - BOT)
#endif

SNAP_TEMPLATE(snap_l,
	GAP,
	GAP + TOP,
	scr->width_in_pixels / 2 - 1.5 * GAP - BORDER * 2,
	scr->height_in_pixels - 2 * GAP - 2 * BORDER - TOP - BOT)

SNAP_TEMPLATE(snap_lu,
	GAP,
	GAP + TOP,
	scr->width_in_pixels / 2 - 1.5 * GAP - BORDER * 2,
	(scr->height_in_pixels - TOP - BOT) / 2 - 1.5 * GAP - 2 * BORDER)

SNAP_TEMPLATE(snap_ld,
	GAP,
	(scr->height_in_pixels - TOP - BOT) / 2 + GAP / 2 + TOP,
	scr->width_in_pixels / 2 - 1.5 * GAP - BORDER * 2,
	(scr->height_in_pixels - TOP - BOT) / 2 - 1.5 * GAP - 2 * BORDER)

SNAP_TEMPLATE(snap_r,
	scr->width_in_pixels / 2 + GAP / 2,
	GAP + TOP,
	scr->width_in_pixels / 2 - 1.5 * GAP - BORDER * 2,
	scr->height_in_pixels - 2 * GAP - 2 * BORDER - TOP - BOT)

SNAP_TEMPLATE(snap_ru,
	scr->width_in_pixels / 2 + GAP / 2,
	GAP + TOP,
	scr->width_in_pixels / 2 - 1.5 * GAP - BORDER * 2,
	(scr->height_in_pixels - TOP - BOT) / 2 - 1.5 * GAP - 2 * BORDER)

SNAP_TEMPLATE(snap_rd,
	scr->width_in_pixels / 2 + GAP / 2,
	(scr->height_in_pixels - TOP - BOT) / 2 + GAP / 2 + TOP,
	scr->width_in_pixels / 2 - 1.5 * GAP - BORDER * 2,
	(scr->height_in_pixels - TOP - BOT) / 2 - 1.5 * GAP - 2 * BORDER)

static void full_save_state(window *win) {
	raise(win);

	save_state(win->child, &win->full);
}

static void full_restore_state(window *win) {
	move_resize(win->child, win->full.x, win->full.y, win->full.width, win->full.height);
}

static void full(window *win) {
	move_resize(win->child, -BORDER, -BORDER, scr->width_in_pixels, scr->height_in_pixels);
}

static void int_full(int arg) {
	if (!fwin[curws]) {
		return;
	}

	fwin[curws]->is_i_full = !fwin[curws]->is_i_full;

	if (fwin[curws]->is_e_full) {
		return;
	}

	if (!fwin[curws]->is_i_full) {
		full_restore_state(fwin[curws]); 
		return;
	}
	
	full_save_state(fwin[curws]);

	full(fwin[curws]);
}

static void ext_full(window *subj) {
	subj->is_e_full = !subj->is_e_full;

	if (!subj->is_e_full) {
		if (!subj->is_i_full) {
			full_restore_state(subj);
		}

		return;
	}
		
	if (!subj->is_i_full) {
		full_save_state(subj);
	}

	full(fwin[curws]);	
}

static uint32_t size_helper(uint32_t win_sze, uint32_t scr_sze) {
	return win_sze > scr_sze ? scr_sze : win_sze;
}

static uint32_t place_helper(uint32_t ptr_pos, uint32_t win_sze, uint32_t scr_sze) {
	if (ptr_pos < win_sze / 2 + BORDER) {
		return 0;
	} else if (ptr_pos + win_sze / 2 + BORDER > scr_sze) {
		return scr_sze - win_sze - 2 * BORDER;
	} else {
		return ptr_pos - win_sze / 2 - BORDER;
	}
}

static void map_request(xcb_generic_event_t *ev) {
	xcb_map_request_event_t *e = (xcb_map_request_event_t *)ev;
	if (all_wtf(e->window, NULL)) {
		return;
	}
	
	xcb_get_property_cookie_t cookie = xcb_ewmh_get_wm_window_type(ewmh, e->window);
	xcb_ewmh_get_atoms_reply_t type;
	if (xcb_ewmh_get_wm_window_type_reply(ewmh, cookie, &type, NULL)) {
		for (unsigned int i = 0; i < type.atoms_len; i++) {		
			if (type.atoms[i] == ewmh->_NET_WM_WINDOW_TYPE_DOCK 
					|| type.atoms[i] == ewmh->_NET_WM_WINDOW_TYPE_TOOLBAR
					|| type.atoms[i] == ewmh->_NET_WM_WINDOW_TYPE_DESKTOP) {
				xcb_ewmh_get_atoms_reply_wipe(&type);
				xcb_map_window(conn, e->window);
				return;
			}
		}
		xcb_ewmh_get_atoms_reply_wipe(&type);
	}

	window *win = malloc(sizeof(window));
	win->child = e->window;
	win->ignore_unmap = 0;
	win->is_snap = 0;
	win->is_e_full = 0;
	win->is_i_full = 0;

	xcb_get_geometry_reply_t *init_geom = w_get_geometry(win->child);

	if (init_geom->height > scr->height_in_pixels) {

	}

	xcb_query_pointer_reply_t *ptr = w_query_pointer();
	uint32_t w = size_helper(init_geom->width, scr->width_in_pixels);
	uint32_t h = size_helper(init_geom->height, scr->height_in_pixels);
	uint32_t x = place_helper(ptr->root_x, w, scr->width_in_pixels);
	uint32_t y = place_helper(ptr->root_y, h, scr->height_in_pixels);
	move_resize(win->child, x, y, w, h);
	free(ptr);
	free(init_geom);

	uint32_t mask = XCB_CONFIG_WINDOW_BORDER_WIDTH;
	uint32_t val = BORDER;
	xcb_configure_window(conn, win->child, mask, &val);

	color(win->child, UNFOCUSCOL);

	normal_events(win);
	
	xcb_map_window(conn, e->window);
	
	insert(curws, win);
	if (!state) {
		focus(win);
	}
}

static void enter_notify(xcb_generic_event_t *ev) {
	xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;
	window *found = ws_wtf(e->event, curws);
	if (found) {
		focus(found);
	}
}

static int move_resize_helper(xcb_window_t win, xcb_get_geometry_reply_t **geom) {
	window *found = ws_wtf(win, curws);
	if (!found || found != fwin[curws]) {
		return 0;
	}

	raise(fwin[curws]);
	
	if (fwin[curws]->is_e_full || fwin[curws]->is_i_full) {
		return 0;
	}

	*geom = w_get_geometry(fwin[curws]->child);

	return 1;
}

static void grab_pointer() {
	uint32_t mask = XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION |
			XCB_EVENT_MASK_POINTER_MOTION_HINT;
	uint32_t mode = XCB_GRAB_MODE_ASYNC;
	xcb_grab_pointer(conn, 0, scr->root, mask, mode, mode, scr->root, XCB_NONE,
			XCB_CURRENT_TIME);
}

static void mouse_move(xcb_window_t win, uint32_t event_x, uint32_t event_y) {
	xcb_get_geometry_reply_t *geom;
	
	if (!move_resize_helper(win, &geom)) {
		return;
	}

	if (fwin[curws]->is_snap) {
		x = fwin[curws]->snap.width * (event_x - geom->x) / geom->width;
		y = fwin[curws]->snap.height * (event_y - geom->y) / geom->height; 
	} else {
		x = event_x - geom->x;
		y = event_y - geom->y;
	}
	
	free(geom);
	
	state = MOVE;

	grab_pointer();
}

static void mouse_resize(xcb_window_t win, uint32_t event_x, uint32_t event_y) {
	xcb_get_geometry_reply_t *geom;

	if (!move_resize_helper(win, &geom)) {
		return;
	}

	fwin[curws]->is_snap = 0;
	x = geom->width - event_x;
	y = geom->height - event_y;

	state = RESIZE;
	
	free(geom);
	
	grab_pointer();
}

static void button_press(xcb_generic_event_t *ev) {
	xcb_button_press_event_t *e = (xcb_button_press_event_t *)ev;

	for (int i = 0; i < LEN(buttons); i++) {
		if (e->detail == buttons[i].button && buttons[i].mod == e->state) {
			buttons[i].function(e->child, e->event_x, e->event_y);
			break;
		}
	}
}

static void mouse_snap(uint32_t ptr_pos, uint32_t tolerance, void (*snap_x)(int arg),
		void (*snap_y)(int arg), void (*snap_z)(int arg)) {
	if (ptr_pos < SNAP_CORNER) {
		snap_x(0);
	} else if (ptr_pos > tolerance - SNAP_CORNER) {
		snap_y(0);
	} else {
		snap_z(0);
	}
}

static void motion_notify(xcb_generic_event_t *ev) {
	xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;

	xcb_query_pointer_reply_t *p = w_query_pointer();
	if (!p) {
		return;
	}
	
	if (state == MOVE) {
		if (p->root_x < SNAP_MARGIN) {
			mouse_snap(p->root_y, scr->height_in_pixels, snap_lu, snap_ld, snap_l);
		} else if (p->root_y < SNAP_MARGIN) {
			mouse_snap(p->root_x, scr->width_in_pixels, snap_lu, snap_ru, snap_max);
		} else if (p->root_x > scr->width_in_pixels - SNAP_MARGIN) {
			mouse_snap(p->root_y, scr->height_in_pixels, snap_ru, snap_rd, snap_r);
		} else if (p->root_y > scr->height_in_pixels - SNAP_MARGIN) {
			mouse_snap(p->root_x, scr->width_in_pixels, snap_ld, snap_rd, snap_max);
		} else {
			if (fwin[curws]->is_snap) {
				snap_restore_state(fwin[curws]);
			}

			move(fwin[curws]->child, p->root_x - x, p->root_y - y);
		}
	} else if (state == RESIZE) {
		resize(fwin[curws]->child, p->root_x + x, p->root_y + y);
	}

	free(p);
}

static void button_release(xcb_generic_event_t *ev) {
	xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
	state = DEFAULT;
}

static void key_press(xcb_generic_event_t *ev) {
	xcb_key_press_event_t *e = (xcb_key_press_event_t *)ev;
	xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keysyms, e->detail, 0);

	if (keysym != XK_Tab && state == CYCLE) {
		stop_cycle();
	}

	for (int i = 0; i < LEN(keys); i++) {
		if (keysym == keys[i].key && keys[i].mod == e->state) {
			keys[i].function(keys[i].arg);
			break;
		}
	}
}

static void key_release(xcb_generic_event_t *ev) {
	xcb_key_release_event_t *e = (xcb_key_release_event_t *)ev;
	xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keysyms, e->detail, 0);

	if (keysym == XK_Super_L && state == CYCLE) {
		stop_cycle();
	}
}

static void forget_client(window *subj, int ws) {
	if ((state == MOVE || state == RESIZE) && subj == fwin[curws]) {
		button_release(NULL);
	}

	free(excise(ws, subj));
	
	if (ws != curws || fwin[curws] != subj) {
		return;
	}

	if (!stack[curws]) {
		fwin[curws] = NULL;
	} else {
		focus(stack[curws]);
	}
}

static void unmap_notify(xcb_generic_event_t *ev) {
	xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t *)ev;

	window *found = ws_wtf(e->window, curws);
	if (!found) {
		return;
	}

	if (found->ignore_unmap) {
		found->ignore_unmap = 0;
	} else {
		forget_client(found, curws);
	}
}

static void destroy_notify(xcb_generic_event_t *ev) {
	xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)ev;

	int ws;
	window *found = all_wtf(e->window, &ws);
	if (found) {
		forget_client(found, ws);
	}
}

static void client_message(xcb_generic_event_t *ev) {
	xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;

	window *found = all_wtf(e->window, NULL);

	if (!found || e->type != ewmh->_NET_WM_STATE) {
		return;
	}	

	for (int i = 1; i < 3; i++) {
		xcb_atom_t atom = (xcb_atom_t)e->data.data32[i];
		if (atom == ewmh->_NET_WM_STATE_FULLSCREEN) {
			switch (2 * e->data.data32[0] + found->is_e_full) {
				case 2 * XCB_EWMH_WM_STATE_ADD:
					ext_full(found);
					break;
				case 2 * XCB_EWMH_WM_STATE_REMOVE + 1:
					ext_full(found);
					break;
				case 2 * XCB_EWMH_WM_STATE_TOGGLE:
					/* FALLTHROUGH */
				case 2 * XCB_EWMH_WM_STATE_TOGGLE + 1:
					ext_full(found);
			}
		}
	}
}

static void mapping_notify(xcb_generic_event_t *ev) {
	xcb_mapping_notify_event_t *e = (xcb_mapping_notify_event_t *)ev;
	if (e->request != XCB_MAPPING_MODIFIER && e->request != XCB_MAPPING_KEYBOARD) {
		return;
	}

	xcb_ungrab_key(conn, XCB_GRAB_ANY, scr->root, XCB_MOD_MASK_ANY);
	grab_keys();
}

#define CHECK_MASK(A, B, C, D, E) \
	if (D & E) {              \
		A[B++] = C;       \
	}

static int mask_to_geo(xcb_configure_request_event_t *e, uint32_t *vals) {
	int i = 0;

	CHECK_MASK(vals, i, e->x, e->value_mask, XCB_CONFIG_WINDOW_X)
	CHECK_MASK(vals, i, e->y, e->value_mask, XCB_CONFIG_WINDOW_X)
	CHECK_MASK(vals, i, e->width, e->value_mask, XCB_CONFIG_WINDOW_WIDTH)
	CHECK_MASK(vals, i, e->height, e->value_mask, XCB_CONFIG_WINDOW_X)

	return i;
}

static void configure_request(xcb_generic_event_t *ev) {
	xcb_configure_request_event_t *e = (xcb_configure_request_event_t *)ev;
	window *found = all_wtf(e->window, NULL);
	
	uint32_t vals[6];

	if (!found) {
		int i = mask_to_geo(e, vals);

		CHECK_MASK(vals, i, e->sibling, e->value_mask, XCB_CONFIG_WINDOW_SIBLING)
		CHECK_MASK(vals, i, e->stack_mode, e->value_mask, XCB_CONFIG_WINDOW_STACK_MODE)

		xcb_configure_window(conn, e->window, e->value_mask, vals);
	} else if (!found->is_i_full && !found->is_e_full) {
		mask_to_geo(e, vals);
		uint32_t ignore = XCB_CONFIG_WINDOW_STACK_MODE | XCB_CONFIG_WINDOW_SIBLING;
		xcb_configure_window(conn, found->child, e->value_mask & ~ignore, vals);
	}
}

static void cleanup(window *win) {
	kill(win->child);
	free(win);
}

static void die() {
	for (int i = 0; i < NUM_WS; i++) {
		traverse(stack[i], cleanup);
	}

	xcb_ungrab_key(conn, XCB_GRAB_ANY, scr->root, XCB_MOD_MASK_ANY);
	xcb_key_symbols_free(keysyms);
	xcb_disconnect(conn);
}

int main(void) {
	conn = xcb_connect(NULL, NULL);
	scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

	uint32_t mask = XCB_CW_EVENT_MASK;
	uint32_t val = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

	xcb_change_window_attributes(conn, scr->root, mask, &val);
	
	atexit(die);

	ewmh = calloc(1, sizeof(xcb_ewmh_connection_t));
	if (!ewmh) {
		LOG("could not allocate ewmh connection");
		return 0;
	}
	xcb_ewmh_init_atoms_replies(ewmh, xcb_ewmh_init_atoms(conn, ewmh), (void *)0);

	const char *WM_ATOM_NAME[2]; 
	WM_ATOM_NAME[0] = "WM_PROTOCOLS";
	WM_ATOM_NAME[1] = "WM_DELETE_WINDOW";
	get_atoms(WM_ATOM_NAME, wm_atoms, WM_COUNT);
	
	const char *NET_ATOM_NAME[3];
	NET_ATOM_NAME[0] = "_NET_SUPPORTED";
	NET_ATOM_NAME[1] = "_NET_WM_STATE_FULLSCREEN";
	NET_ATOM_NAME[2] = "_NET_WM_STATE";
	get_atoms(NET_ATOM_NAME, net_atoms, NET_COUNT);
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root, net_atoms[NET_SUPPORTED],
			XCB_ATOM_ATOM, 32, NET_COUNT, net_atoms);

	mask = XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE;
	for (int i = 0; i < LEN(buttons); i++) {
		xcb_grab_button(conn, 0, scr->root, mask, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
				scr->root, XCB_NONE, buttons[i].button, buttons[i].mod);
	}

	grab_keys();
	
	void (*events[XCB_NO_OPERATION])(xcb_generic_event_t *event);

	for (int i = 0; i < XCB_NO_OPERATION; i++) events[i] = NULL;

	events[XCB_BUTTON_PRESS]      = button_press;
	events[XCB_BUTTON_RELEASE]    = button_release;
	events[XCB_MOTION_NOTIFY]     = motion_notify;
	events[XCB_CLIENT_MESSAGE]    = client_message;
	events[XCB_CONFIGURE_REQUEST] = configure_request;
	events[XCB_KEY_PRESS]         = key_press;
	events[XCB_KEY_RELEASE]       = key_release;
	events[XCB_MAP_REQUEST]       = map_request;
	events[XCB_UNMAP_NOTIFY]      = unmap_notify;
	events[XCB_DESTROY_NOTIFY]    = destroy_notify;
	events[XCB_ENTER_NOTIFY]      = enter_notify;
	events[XCB_MAPPING_NOTIFY]    = mapping_notify;

	keysyms = xcb_key_symbols_alloc(conn);

	xcb_generic_event_t *ev;
	for (; !xcb_connection_has_error(conn);) {
		xcb_flush(conn);
		ev = xcb_wait_for_event(conn);
		if (events[ev->response_type & ~0x80]) {
			events[ev->response_type & ~0x80](ev);
		}
		free(ev);
	}

	return 0;
}
