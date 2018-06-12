#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <unistd.h>
#include "config.h"
#include <stdio.h>

enum { FOCUS, UNFOCUS };
enum { CENTER, CORNER };

static xcb_connection_t		*connection;
static xcb_ewmh_connection_t 	*ewmh;
static xcb_screen_t		*screen;
static xcb_window_t		focuswindow;
static xcb_atom_t 		atoms[2];

static void
arai_init(void)
{
	const uint32_t values[] = {
		XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_KEY_PRESS
	};
	connection = xcb_connect(NULL, NULL);
	screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
	focuswindow = screen->root;
	xcb_change_window_attributes_checked(connection,
			screen->root,
			XCB_CW_EVENT_MASK,
			values);
}

static void
arai_setup_ewmh(void)
{
	ewmh = calloc(1, sizeof(xcb_ewmh_connection_t));
	xcb_ewmh_init_atoms_replies(ewmh,
			xcb_ewmh_init_atoms(connection, ewmh),
			(void *)0);
}

static void
arai_setup_icccm(void)
{
	xcb_intern_atom_cookie_t cookies[] = {
		xcb_intern_atom(connection, 0, 12, "WM_PROTOCOLS"),
		xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW")
	};
	xcb_intern_atom_reply_t *reply;
	for (unsigned int i = 0; i < 2; i++) {
		reply = xcb_intern_atom_reply(connection, cookies[i], NULL);
		atoms[i] = reply->atom;
		free(reply);
	}
}

static void
arai_buttongrab(void)
{
	for (int i = 0; i < 2; i++) {
		xcb_grab_button(connection,
			0,
			screen->root,
			XCB_EVENT_MASK_BUTTON_PRESS |
			XCB_EVENT_MASK_BUTTON_RELEASE,
			XCB_GRAB_MODE_ASYNC,
			XCB_GRAB_MODE_ASYNC,
			screen->root,
			XCB_NONE,
			buttons[i].button,
			buttons[i].mod);
	}
}

static void
arai_keygrab(void)
{
	xcb_key_symbols_t *keysyms = xcb_key_symbols_alloc(connection);
	for (int i = 0; i < sizeof(keys)/sizeof(*keys); i++) {
		xcb_grab_key(connection,
			0,
		       	screen->root,
			keys[i].mod,
			*xcb_key_symbols_get_keycode(keysyms, keys[i].key),
		       	XCB_GRAB_MODE_ASYNC,
			XCB_GRAB_MODE_ASYNC);
	}
	xcb_key_symbols_free(keysyms);
}

static xcb_keysym_t
arai_get_keysym(xcb_keycode_t keycode)
{
	xcb_key_symbols_t *keysyms = xcb_key_symbols_alloc(connection);
	xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keysyms, keycode, 0);
	xcb_key_symbols_free(keysyms);
	return keysym;
}

static int
arai_check_managed(xcb_window_t window)
{
	xcb_ewmh_get_atoms_reply_t type;
	if (!xcb_ewmh_get_wm_window_type_reply(ewmh,
				xcb_ewmh_get_wm_window_type(ewmh, window),
				&type, NULL)) return 1;
	for (unsigned int i = 0; i < type.atoms_len; i++)
		if (type.atoms[i] == ewmh->_NET_WM_WINDOW_TYPE_DOCK ||
					type.atoms[i] == ewmh->_NET_WM_WINDOW_TYPE_TOOLBAR ||
					type.atoms[i] == ewmh->_NET_WM_WINDOW_TYPE_DESKTOP) {
				xcb_ewmh_get_atoms_reply_wipe(&type);
				return 0;
		}
	xcb_ewmh_get_atoms_reply_wipe(&type);
	return 1;
}

static void
arai_focus(xcb_window_t window, int mode)
{
	const uint32_t values[] = { mode ? UNFOCUSCOLOR : FOCUSCOLOR };
	xcb_change_window_attributes(connection,
			window,
			XCB_CW_BORDER_PIXEL,
			values);
	if (mode == FOCUS) {
		xcb_set_input_focus(connection,
				XCB_INPUT_FOCUS_POINTER_ROOT,
				window,
				XCB_CURRENT_TIME);
		if (window != focuswindow) {
			arai_focus(focuswindow, UNFOCUS);
			focuswindow = window;
		}
	}
}

static void
arai_wrap(xcb_window_t window)
{
	uint32_t values[] = { XCB_EVENT_MASK_ENTER_WINDOW, XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY };
	xcb_change_window_attributes(connection,
			window,
			XCB_CW_EVENT_MASK,
			values);
	values[0] = BORDER;
	xcb_configure_window(connection,
			window,
			XCB_CONFIG_WINDOW_BORDER_WIDTH,
			values);
	arai_center(window);
	arai_focus(window, FOCUS);
}

static void
arai_move(xcb_query_pointer_reply_t *pointer, xcb_get_geometry_reply_t *geometry, xcb_window_t window)
{
	uint32_t values[] = {
		(pointer->root_x + geometry->width / 2 + BORDER * 2 > screen->width_in_pixels) ?
		(screen->width_in_pixels - geometry->width - BORDER * 2) :
		(pointer->root_x - geometry->width / 2 - BORDER),
		(pointer->root_y + geometry->height / 2 + BORDER * 2 + BOT > screen->height_in_pixels) ?
		(screen->height_in_pixels - geometry->height - BORDER * 2 - BOT) :
		(pointer->root_y - geometry->height / 2 - BORDER)
	};
	if (pointer->root_x < geometry->width / 2 + BORDER) values[0] = 0;
	if (pointer->root_y < geometry->height / 2 + TOP + BORDER) values[1] = TOP;
	xcb_configure_window(connection,
			window,
			XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
			values);
}

static void
arai_resize(xcb_query_pointer_reply_t *pointer, xcb_get_geometry_reply_t *geometry, xcb_window_t window)
{
	const uint32_t values[2] = {
		(pointer->root_x < geometry->x + 64) ?
		64 - 2 * BORDER :
		(pointer->root_x - geometry->x - 2 * BORDER + 1),
		(pointer->root_y < geometry->y + 64) ?
		64 - 2 * BORDER :
		(pointer->root_y - geometry->y - 2 * BORDER + 1)
	};
	xcb_configure_window(connection,
			window,
			XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
			values);
}

static void
arai_warp_pointer(xcb_window_t window, int mode)
{
	xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(connection,
			xcb_get_geometry(connection, window),
			NULL);
	xcb_warp_pointer(connection,
			XCB_NONE,
			window,
			0, 0, 0, 0,
			mode ? geometry->width + BORDER : geometry->width/2,
			mode ? geometry->height + BORDER : geometry->height/2);
	free(geometry);
}

static void
arai_center(xcb_window_t window)
{
	xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(connection,
			xcb_get_geometry(connection, window),
			NULL);
	const uint32_t values[] = {
		(screen->width_in_pixels - geometry->width) / 2 - BORDER,	
		(screen->height_in_pixels - geometry->height) / 2 - BORDER + TOP / 2 - BOT / 2,
		XCB_STACK_MODE_ABOVE
	};
	xcb_configure_window(connection,
			window,
			XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
			XCB_CONFIG_WINDOW_STACK_MODE,
			values);
}

static void
arai_delete(xcb_window_t window)
{
	xcb_client_message_event_t event;
	event.response_type = XCB_CLIENT_MESSAGE;
	event.window = window;
	event.format = 32;
	event.sequence = 0;
	event.type = atoms[0];
	event.data.data32[0] = atoms[1];
	event.data.data32[1] = XCB_CURRENT_TIME;
	xcb_send_event(connection,
			0,
			window,
			XCB_EVENT_MASK_NO_EVENT,
			(char *)&event);
}

static void
arai_kill(xcb_window_t window)
{
	focuswindow = screen->root;
	xcb_icccm_get_wm_protocols_reply_t protocols;
	if (!xcb_icccm_get_wm_protocols_reply(connection,
				xcb_icccm_get_wm_protocols_unchecked(connection,
					window,
					ewmh->WM_PROTOCOLS),
				&protocols,
				NULL)) {
		xcb_kill_client(connection, window);
		return;
	}
	for (int i = 0; i < protocols.atoms_len; i++)
		if (protocols.atoms[i] == atoms[1]) {
			arai_delete(window);
			xcb_icccm_get_wm_protocols_reply_wipe(&protocols);
		}
}

static void
arai_key_press(xcb_generic_event_t *event)
{
	if (focuswindow == screen->root || !arai_check_managed(focuswindow)) return;
	xcb_keysym_t keysym = arai_get_keysym(((xcb_key_press_event_t *)event)->detail);
	for (int i = 0; i < sizeof(keys)/sizeof(*keys); i++)
		if (keysym == keys[i].key)
			keys[i].function(focuswindow);
}

static void
arai_button_press(xcb_button_press_event_t *e)
{
	if (!(e->child && arai_check_managed(e->child) && e->child != screen->root)) return;
	uint32_t values[] = { XCB_STACK_MODE_ABOVE };
	xcb_configure_window(connection,
			e->child,
			XCB_CONFIG_WINDOW_STACK_MODE,
			values);
	if (e->detail == 1) arai_warp_pointer(e->child, CENTER);
	else arai_warp_pointer(e->child, CORNER);
	xcb_grab_pointer(connection,
			0,
			screen->root,
			XCB_EVENT_MASK_BUTTON_RELEASE |
			XCB_EVENT_MASK_BUTTON_MOTION |
			XCB_EVENT_MASK_POINTER_MOTION_HINT,
			XCB_GRAB_MODE_ASYNC,
			XCB_GRAB_MODE_ASYNC,
			screen->root,
			XCB_NONE,
			XCB_CURRENT_TIME);
}

static void
arai_motion_notify(xcb_generic_event_t *event, int mode, xcb_window_t carry)
{
	xcb_query_pointer_reply_t *pointer = xcb_query_pointer_reply(connection,
			xcb_query_pointer(connection, screen->root),
			0);
	xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(connection,
			xcb_get_geometry(connection, carry),
			NULL);
	if (mode == 1) arai_move(pointer, geometry, focuswindow);	
	else if (mode == 3) arai_resize(pointer, geometry, focuswindow);
	free(geometry);
	free(pointer);
}

static void
arai_dive(void)
{
	xcb_generic_event_t *event;
	xcb_window_t carry;
	int mode;
	event = xcb_wait_for_event(connection);
	switch (event->response_type & ~0x80) {
		case XCB_ENTER_NOTIFY: {
			xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)event;
			arai_focus(e->event, FOCUS);
		} break;
		case XCB_MAP_NOTIFY: {
			xcb_map_notify_event_t *e = (xcb_map_notify_event_t *)event;
			if (!e->override_redirect && arai_check_managed(e->window))
				arai_wrap(e->window);
			xcb_map_window(connection, e->window);
		} break;
		case XCB_DESTROY_NOTIFY: {
			xcb_destroy_notify_event_t *e = (xcb_destroy_notify_event_t *)event;
			xcb_kill_client(connection, e->window);
		} break;
		case XCB_CONFIGURE_NOTIFY: {
			xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)event;
			if (e->window != focuswindow) arai_focus(e->window, UNFOCUS);
			arai_focus(focuswindow, FOCUS);
		} break;
		case XCB_BUTTON_PRESS: {
			xcb_button_press_event_t *e = (xcb_button_press_event_t *)event;
			carry = e->child;
			mode = e->detail;
			arai_button_press(e);
		} break;
		case XCB_MOTION_NOTIFY: {
			arai_motion_notify(event, mode, carry);
		} break;
		case XCB_BUTTON_RELEASE: {
			xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
		} break;
		case XCB_KEY_PRESS: {
			arai_key_press(event);
		} break;
	}
	xcb_flush(connection);
	free(event);
}

static void
arai_cleanup(void)
{
	xcb_ungrab_button(connection,
			XCB_BUTTON_INDEX_ANY,
			screen->root,
			XCB_MOD_MASK_ANY);
	xcb_ungrab_key(connection,
			XCB_GRAB_ANY,
			screen->root,
			XCB_MOD_MASK_ANY);
	xcb_ewmh_connection_wipe(ewmh);
	xcb_flush(connection);
	free(ewmh);
	xcb_disconnect(connection);
}

int
main(void)
{
	arai_init();
	arai_setup_ewmh();
	arai_setup_icccm();
	arai_buttongrab();
	arai_keygrab();
	for (;;) arai_dive();
	atexit(arai_cleanup);
	return 0;
}
