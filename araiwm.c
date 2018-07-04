#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "config.h"

enum { FOCUS, UNFOCUS };
enum { CENTER, CORNER };
enum { LEFT, ULEFT, DLEFT, RIGHT, URIGHT, DRIGHT, FULL };
enum { NET_SUPPORTED, NET_FULLSCREEN, NET_WM_STATE, NET_COUNT };

static xcb_connection_t		*connection;
static xcb_ewmh_connection_t 	*ewmh;
static xcb_screen_t		*screen;
static xcb_window_t		focuswindow;
static xcb_atom_t 		atoms[2], net_atoms[NET_COUNT];
static client			*wslist[NUM_WS] = { NULL };

//focuswindow modification state data
static int			curws = 0,
				mode = 0,
				xoff = 0,
				yoff = 0;

static void (*events[XCB_NO_OPERATION])(xcb_generic_event_t *event);

static void
arai_get_atoms(char **names, xcb_atom_t *atoms, unsigned int count)
{
	xcb_intern_atom_cookie_t cookies[count];
	xcb_intern_atom_reply_t *reply;
	for (int i = 0; i < count; i++) cookies[i] = xcb_intern_atom(connection,
			0,
			strlen(names[i]),
			names[i]);
	for (int i = 0; i < count; i++) {
		reply = xcb_intern_atom_reply(connection, cookies[i], NULL);
		if (reply) atoms[i] = reply->atom; free(reply);
	}
}

static void
arai_init(void)
{
	//setup xcb
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

	//setup ewmh	
	ewmh = calloc(1, sizeof(xcb_ewmh_connection_t));
	xcb_ewmh_init_atoms_replies(ewmh,
			xcb_ewmh_init_atoms(connection, ewmh),
			(void *)0);
	xcb_ewmh_set_wm_name(ewmh, screen->root, 6, "araiwm");
	xcb_atom_t atoms[] = {
		ewmh->_NET_SUPPORTED,
		ewmh->_NET_WM_NAME,
		ewmh->WM_PROTOCOLS
	};
	xcb_ewmh_set_supported(ewmh, 0, sizeof(atoms)/sizeof(*atoms), atoms);

	//setup icccm
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

	char *NET_ATOM_NAME[] = { "_NET_SUPPORTED",
		"_NET_WM_STATE_FULLSCREEN",
		"_NET_WM_STATE",
	};
	
	arai_get_atoms(NET_ATOM_NAME, net_atoms, NET_COUNT);

	xcb_change_property(connection,
			XCB_PROP_MODE_REPLACE,
			screen->root,
			net_atoms[NET_SUPPORTED],
			XCB_ATOM_ATOM,
			32,
			NET_COUNT,
			net_atoms);

	//grab mouse buttons
	for (int i = 0; i < sizeof(buttons)/sizeof(*buttons); i++)
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
	
	//grab keyboard keys
	xcb_key_symbols_t *keysyms = xcb_key_symbols_alloc(connection);
	for (int i = 0; i < sizeof(keys)/sizeof(*keys); i++)
		xcb_grab_key(connection,
			0,
		       	screen->root,
			keys[i].mod,
			*xcb_key_symbols_get_keycode(keysyms, keys[i].key),
		       	XCB_GRAB_MODE_ASYNC,
			XCB_GRAB_MODE_ASYNC);
	xcb_key_symbols_free(keysyms);

	

	xcb_flush(connection);
}

static void
arai_add_client(client* add, int ws)
{
	client *temp = wslist[ws];
	wslist[ws] = malloc(sizeof(client));
	*wslist[ws] = *add;
	wslist[ws]->next = temp;
}

static void
arai_remove_client(xcb_window_t window)
{
	client *current = wslist[curws], *prev = NULL;
	while (current && current->id != window) {
		prev = current;
		current = current->next;
	}
	if (!current) return;
	else if (current->next && prev) prev->next = current->next;
	else if (prev) prev->next = NULL;
	else if (current->next) wslist[curws] = current->next;
	else wslist[curws] = NULL;
	free(current);
}

static client*
arai_find_client(xcb_window_t win)
{
	client *current = wslist[curws];
	while (current) {
		if (current->id == win) return current;
		current = current->next;
	}
	return NULL;
}

static void
arai_restack(client *subject)
{
	client *current = wslist[curws], *prev = NULL;
	while (current && current != subject) {
		prev = current;
		current = current->next;
	}
	if (!current) return;
	else if (current->next && prev) prev->next = current->next;
	else if (prev) prev->next = NULL;
	else if (current->next) wslist[curws] = current->next;
	else wslist[curws] = NULL;
	current->next = wslist[curws];
	wslist[curws] = current;
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
	xcb_change_window_attributes(connection, window, XCB_CW_EVENT_MASK, values);
	values[0] = BORDER;
	xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_BORDER_WIDTH, values);
	arai_focus(window, FOCUS);
	arai_center(window);
	client new;
	new.id = window;
	new.max = 0;
	arai_add_client(&new, curws);
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
arai_snap(int arg)
{
	if (focuswindow == screen->root) return;
	client *temp = arai_find_client(focuswindow);
	if (temp && temp->max == 1) arai_max(focuswindow);
	uint32_t values[] = {
		0,
		GAP + TOP,
		screen->width_in_pixels / 2 - GAP * 1.5 - BORDER * 2,
		screen->height_in_pixels - GAP * 2 - BORDER * 2 - BOT - TOP,
		XCB_STACK_MODE_ABOVE
	};
	if (arg < 3) {
		values[0] = GAP;
		if (arg != 0) {
			values[3] = (screen->height_in_pixels - TOP - BOT) / 2 - GAP * 1.5 - BORDER * 2;
			if (arg == ULEFT) values[1] = GAP + TOP;
			else values[1] = (screen->height_in_pixels - TOP - BOT) / 2 + GAP / 2 + TOP;
		}
	} else if (arg < 6) {
		values[0] = screen->width_in_pixels / 2 + GAP / 2;
		if (arg != 3) {
			values[3] = (screen->height_in_pixels - TOP - BOT) / 2 - GAP * 1.5 - BORDER * 2;
			if (arg == URIGHT) values[1] = GAP + TOP;
			else values[1] = (screen->height_in_pixels - TOP - BOT) / 2 + GAP / 2 + TOP;
		}
	} else {
		values[0] = GAP;
		values[1] = GAP + TOP;
		values[2] = screen->width_in_pixels - GAP * 2 - BORDER * 2;
	}	
	xcb_configure_window(connection,
			focuswindow,
			XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
			XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
			XCB_CONFIG_WINDOW_STACK_MODE,
			values);
	xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
	arai_restack(arai_find_client(focuswindow));
}

static void
arai_move(xcb_query_pointer_reply_t *pointer, int xoff, int yoff)
{
	const uint32_t values[] = {
		pointer->root_x - xoff - BORDER,
		pointer->root_y - yoff - BORDER
	};
	if (pointer->root_x < SNAP_X) {
		if (pointer->root_y < SNAP_Y + TOP) arai_snap(ULEFT);
		else if (pointer->root_y > screen->height_in_pixels - SNAP_Y - BOT) arai_snap(DLEFT);
		else arai_snap(LEFT);
	} else if (pointer->root_x > screen->width_in_pixels - SNAP_X) {
		if (pointer->root_y < SNAP_Y + TOP) arai_snap(URIGHT);
		else if (pointer->root_y > screen->height_in_pixels - SNAP_Y - BOT) arai_snap(DRIGHT);
		else arai_snap(RIGHT);
	} else if (pointer->root_y < SNAP_X) arai_snap(FULL);
	else xcb_configure_window(connection,
			focuswindow,
			XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
			values);
}

static void
arai_resize(xcb_query_pointer_reply_t *pointer, int xoff, int yoff)
{
	const uint32_t values[2] = {
		(pointer->root_x < xoff + 64) ? 64 - 2 * BORDER : (pointer->root_x - xoff - 2 * BORDER + 1),
		(pointer->root_y < yoff + 64) ? 64 - 2 * BORDER : (pointer->root_y - yoff - 2 * BORDER + 1)
	};
	xcb_configure_window(connection,
			focuswindow,
			XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
			values);
}

static void
arai_center(int arg)
{
	if (focuswindow == screen->root) return;
	client *temp = arai_find_client(focuswindow);
	if (temp && temp->max == 1) arai_max(focuswindow);
	xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(connection,
			xcb_get_geometry(connection, focuswindow),
			NULL);
	const uint32_t values[] = {
		(screen->width_in_pixels - geometry->width) / 2 - BORDER,	
		(screen->height_in_pixels - geometry->height) / 2 - BORDER + TOP / 2 - BOT / 2,
		XCB_STACK_MODE_ABOVE
	};
	xcb_configure_window(connection,
			focuswindow,
			XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_STACK_MODE,
			values);
	arai_restack(temp);
}

static void
arai_cycle(int arg)
{
	if (focuswindow == screen->root || !wslist[curws] || !wslist[curws]->next) return;
	const uint32_t values[] = { XCB_STACK_MODE_ABOVE };
	client *current = wslist[curws];
	while (current->next) current = current->next;
	if (!current) return;
	arai_focus(current->id, FOCUS);
	xcb_configure_window(connection, focuswindow, XCB_CONFIG_WINDOW_STACK_MODE, values);
	arai_restack(arai_find_client(focuswindow));
	arai_warp_pointer(focuswindow, CENTER);
}

static void
arai_kill(int arg)
{
	if (focuswindow == screen->root) return;
	xcb_window_t temp = focuswindow;
	arai_focus(screen->root, FOCUS);
	xcb_icccm_get_wm_protocols_reply_t protocols;
	if (xcb_icccm_get_wm_protocols_reply(connection,
				xcb_icccm_get_wm_protocols_unchecked(connection,
					temp,
					ewmh->WM_PROTOCOLS),
				&protocols,
				NULL))
		for (int i = 0; i < protocols.atoms_len; i++)
			if (protocols.atoms[i] == atoms[1]) {
				xcb_client_message_event_t event;
				event.response_type = XCB_CLIENT_MESSAGE;
				event.window = temp;
				event.format = 32;
				event.sequence = 0;
				event.type = atoms[0];
				event.data.data32[0] = atoms[1];
				event.data.data32[1] = XCB_CURRENT_TIME;
				xcb_send_event(connection,
						0,
						temp,
						XCB_EVENT_MASK_NO_EVENT,
						(char *)&event);
				xcb_icccm_get_wm_protocols_reply_wipe(&protocols);
				return;
			}
	xcb_icccm_get_wm_protocols_reply_wipe(&protocols);
	xcb_kill_client(connection, temp);
}

static void
arai_chws(int ws)
{
	if (ws == curws) return;
	client *current = wslist[ws];
	while (current) {
		xcb_map_window(connection, current->id);
		current = current->next;
	}
	current = wslist[curws];
	while (current) {
		xcb_unmap_window(connection, current->id);
		current = current->next;
	}
	curws = ws;
	if (wslist[curws]) arai_focus(wslist[curws]->id, FOCUS);
	else arai_focus(screen->root, FOCUS);
	xcb_ewmh_set_current_desktop(ewmh, 0, ws);
}

static void
arai_sendws(int ws)
{
	if (focuswindow == screen->root) return;
	client *oldclient = arai_find_client(focuswindow);
	arai_add_client(oldclient, ws);
	xcb_unmap_window(connection, focuswindow);
	arai_focus(screen->root, FOCUS);
}

static void
arai_max(int arg)
{
	client* found;
	if (focuswindow == screen->root || !(found = arai_find_client(focuswindow))) return;
	if (found->max == 0) {
		xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(connection,
				xcb_get_geometry(connection, focuswindow),
				NULL);
		const uint32_t values[] = {
			0,
			0,
			screen->width_in_pixels,
			screen->height_in_pixels,
			XCB_STACK_MODE_ABOVE,
			0
		};
		xcb_configure_window(connection,
				focuswindow,
				XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
				XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
				XCB_CONFIG_WINDOW_STACK_MODE | XCB_CONFIG_WINDOW_BORDER_WIDTH,
				values);
		found->x = geometry->x;
		found->y = geometry->y;
		found->w = geometry->width;
		found->h = geometry->height;
		found->max = 1;
		arai_restack(found);
	} else {
		const uint32_t values[] = {
			found->x,
			found->y,
			found->w,
			found->h,
			BORDER
		};
		xcb_configure_window(connection,
				focuswindow,
				XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
				XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
				XCB_CONFIG_WINDOW_BORDER_WIDTH,
				values);
		found->max = 0;
	}
}

static void
arai_key_press(xcb_generic_event_t *event)
{
	xcb_key_press_event_t *ev = (xcb_key_press_event_t *)event;
	xcb_keysym_t keysym = arai_get_keysym(ev->detail);
	for (int i = 0; i < sizeof(keys)/sizeof(*keys); i++)
		if (keysym == keys[i].key &&
				keys[i].mod == ev->state) {
			keys[i].function(keys[i].arg);
			break;
		}
}

static void
arai_button(xcb_button_press_event_t *e)
{
	if (!(e->child && arai_check_managed(e->child) && e->child != screen->root)) return;
	uint32_t values[] = { XCB_STACK_MODE_ABOVE };
	xcb_configure_window(connection, e->child, XCB_CONFIG_WINDOW_STACK_MODE, values);
	arai_restack(arai_find_client(focuswindow));
	if (arai_find_client(focuswindow)->max == 1) return;
	if (e->detail == 3) arai_warp_pointer(e->child, CORNER);
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
arai_motion_notify(xcb_generic_event_t *event)
{
	xcb_query_pointer_reply_t *pointer = xcb_query_pointer_reply(connection,
			xcb_query_pointer(connection, screen->root),
			0);
	if (mode == 1) arai_move(pointer, xoff, yoff);	
	else arai_resize(pointer, xoff, yoff);
	free(pointer);
}

static void
arai_client_message(xcb_generic_event_t *event)
{
	xcb_client_message_event_t *e = (xcb_client_message_event_t *)event;
	if (e->type == net_atoms[NET_WM_STATE] && 
			((unsigned)e->data.data32[1] == net_atoms[NET_FULLSCREEN] ||
			 (unsigned)e->data.data32[2] == net_atoms[NET_FULLSCREEN]))
		arai_max(e->window);
}

static void
arai_enter_notify(xcb_generic_event_t *event)
{
	xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)event;
	arai_focus(e->event, FOCUS);
}

static void
arai_map_notify(xcb_generic_event_t *event)
{
	xcb_map_notify_event_t *e = (xcb_map_notify_event_t *)event;
	if (!e->override_redirect && arai_check_managed(e->window) &&
			arai_find_client(e->window) == NULL) arai_wrap(e->window);
	xcb_map_window(connection, e->window);
}

static void
arai_unmap_notify(xcb_generic_event_t *event)
{
	xcb_map_notify_event_t *e = (xcb_map_notify_event_t *)event;
	arai_remove_client(e->window);
}

static void
arai_configure_notify(xcb_generic_event_t *event)
{
	xcb_configure_notify_event_t *e = (xcb_configure_notify_event_t *)event;
	if (e->window != focuswindow) arai_focus(e->window, UNFOCUS);
	arai_focus(focuswindow, FOCUS);
}

static void
arai_button_press(xcb_generic_event_t *event)
{
	xcb_button_press_event_t *e = (xcb_button_press_event_t *)event;
	if (!e->child || e->child == screen->root) return;
	//carry = e->child;
	mode = e->detail;
	xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(connection,
			xcb_get_geometry(connection, focuswindow),
			NULL);
	if (mode == 1) {
		xcb_query_pointer_reply_t *pointer = xcb_query_pointer_reply(connection,
				xcb_query_pointer(connection, screen->root),
				0);
		xoff = pointer->root_x - geometry->x - BORDER;
		yoff = pointer->root_y - geometry->y - BORDER;
		free(pointer);
	} else {
		xoff = geometry->x;
		yoff = geometry->y;
	}
	free(geometry);
	arai_button(e);
}

static void
arai_button_release(xcb_generic_event_t *event)
{
	printf("hello what the fuck\n");
	xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
}

static void
arai_set_events(void)
{
	for (int i = 0; i < XCB_NO_OPERATION; i++) events[i] = NULL;
	events[XCB_BUTTON_PRESS]	= arai_button_press;
	events[XCB_BUTTON_RELEASE]	= arai_button_release;
	events[XCB_MOTION_NOTIFY]	= arai_motion_notify;
	events[XCB_CLIENT_MESSAGE]	= arai_client_message;
	events[XCB_CONFIGURE_NOTIFY]	= arai_configure_notify;
	events[XCB_KEY_PRESS]		= arai_key_press;
	events[XCB_MAP_NOTIFY]		= arai_map_notify;
	events[XCB_UNMAP_NOTIFY]	= arai_unmap_notify;
	events[XCB_ENTER_NOTIFY]	= arai_enter_notify;
}

static void
arai_dive(void)
{
	xcb_generic_event_t *event;
	for (;;) {
		event = xcb_wait_for_event(connection);
		if (events[event->response_type & ~0x80]) events[event->response_type & ~0x80](event);
		xcb_flush(connection);
		free(event);
	}
}

static void
arai_cleanup(void)
{
	//free client list
	client *current, *temp;
	if (current = wslist[curws]) {
		wslist[curws] = NULL;
		while (current) {
			temp = current->next;
			free(current);
			current = temp;
		}
	}

	//ungrap inputs
	xcb_ungrab_button(connection,
			XCB_BUTTON_INDEX_ANY,
			screen->root,
			XCB_MOD_MASK_ANY);
	xcb_ungrab_key(connection,
			XCB_GRAB_ANY,
			screen->root,
			XCB_MOD_MASK_ANY);

	//kill connections
	xcb_ewmh_connection_wipe(ewmh);
	xcb_flush(connection);
	free(ewmh);
	xcb_disconnect(connection);
}

int
main(int argc, char **argv)
{
#ifdef config_file
	if (argc > 2) {
		printf("araiwm: too many arguments.\n");
		return 0;
	} else if (argc == 2) {
		printf("araiwm: arg read.\n");
		parse(argv[1]);
	} else printf("araiwm: no config specified, using default.\n");
#endif
	arai_init();
	arai_set_events();
	arai_dive();
	atexit(arai_cleanup);
	return 0;
}
