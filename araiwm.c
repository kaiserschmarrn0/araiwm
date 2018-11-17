#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/*
the top left corner includes the border, but the width/height dont!!
*/

#define LEN(A) sizeof(A)/sizeof(*A)

#define NUM_WS 4

#define MOVE 0
#define RESZ 1

#define FOCUS 0
#define UNFOCUS 1

//state masks
#define SNAP 1
#define FULL 2

#define MOD XCB_MOD_MASK_4
#define SHIFT XCB_MOD_MASK_SHIFT

enum { WM_PROTOCOLS, WM_DELETE_WINDOW, WM_COUNT };
enum { NET_SUPPORTED, NET_FULLSCREEN, NET_WM_STATE, NET_COUNT };

typedef struct {
 uint16_t mod;
 xcb_keysym_t key;
 void (*function) (int arg);
 int arg;
} keybind_t;

typedef struct client_t {
 xcb_window_t id;
 struct client_t *next, *prev;
 xcb_rectangle_t snap_geom,
                 full_geom;
 bool ignore_unmap,
      snap,
      e_full,
      i_full;
} client_t;

//xcb boilerplate
static xcb_connection_t *conn;
static xcb_screen_t *scr;
static xcb_ewmh_connection_t *ewmh;
static xcb_atom_t wm_atoms[WM_COUNT], net_atoms[NET_COUNT];
static xcb_key_symbols_t *keysyms;

//window organization
static client_t *stack[NUM_WS] = { NULL },
                *fwin[NUM_WS] = { NULL };

//wm state
static int curws = 0;
static int state = 0,
           x,
           y;

bool tabbing = false;
client_t *marker = NULL;

//config values
uint32_t TOP = 0,
         BOT = 33,
         GAP = 9,
         BORDER = 6,
         FOCUSCOL = 0x9baeb1,
         UNFOCUSCOL = 0x12333b,
         MARGIN = 4,
         CORNER = 256;

static void close(int arg);
static void cycle(int arg);
static void snap_left(int arg);
static void snap_right(int arg);
static void snap_max(int arg);
static void int_fullscreen(int arg);
static void change_ws(int arg);
static void send_ws(int arg);

static const keybind_t keys[] = {
 { MOD, XK_q, close, 0 },
 { MOD, XK_Tab, cycle, 0 },
 { MOD, XK_Left, snap_left, 0 },
 { MOD, XK_Right, snap_right, 0 },
 { MOD, XK_f, snap_max, 0 },
 { MOD | SHIFT, XK_f, int_fullscreen, 0 },
 { MOD, XK_1, change_ws, 0 },
 { MOD, XK_2, change_ws, 1 },
 { MOD, XK_3, change_ws, 2 },
 { MOD, XK_4, change_ws, 3 },
 { MOD | SHIFT, XK_1, send_ws, 0 },
 { MOD | SHIFT, XK_2, send_ws, 1 },
 { MOD | SHIFT, XK_3, send_ws, 2 },
 { MOD | SHIFT, XK_4, send_ws, 3 },
};

static void insert(int ws, client_t *subject) {
 subject->next = stack[ws];
 subject->prev = NULL;
 if (stack[ws]) stack[ws]->prev = subject;
 stack[ws] = subject;
}

static client_t *excise(int ws, client_t *subject) {
 if (subject->next) subject->next->prev = subject->prev;
 if (subject->prev) subject->prev->next = subject->next;
 else stack[ws] = subject->next;
 return subject;
}

/*static void structure() {
 client_t *temp = stack[curws];
 printf("window stack: [ ");
 while (temp) {
  printf("%d, ", temp->id);
  temp = temp->next;
 }
 printf("]\n");
}*/

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

static xcb_keysym_t get_keysym(xcb_keycode_t keycode) {
 xcb_key_symbols_t *keysyms = xcb_key_symbols_alloc(conn);
 xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keysyms, keycode, 0);
 xcb_key_symbols_free(keysyms);
 return keysym;
}

static void focus(client_t *subj, int mode) {
 const uint32_t vals[] = { mode ? UNFOCUSCOL : FOCUSCOL };
 xcb_change_window_attributes(conn, subj->id, XCB_CW_BORDER_PIXEL, vals);
 if (mode == FOCUS) {
  xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, subj->id, XCB_CURRENT_TIME);
  if (subj != fwin[curws]) {
   if (fwin[curws] != NULL) focus(fwin[curws], UNFOCUS);
   fwin[curws] = subj;
  }
 }
}

static void raise(client_t *subj) {
 insert(curws, excise(curws, subj));
 xcb_configure_window(conn, subj->id, XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]){ XCB_STACK_MODE_ABOVE });
 xcb_flush(conn);
 xcb_map_window(conn, subj->id);
 xcb_flush(conn);
 focus(subj, FOCUS);
}

static void close(int arg) {
 if (!fwin[curws]) return;
 xcb_icccm_get_wm_protocols_reply_t pro;
 if (xcb_icccm_get_wm_protocols_reply(conn, xcb_icccm_get_wm_protocols_unchecked(conn, fwin[curws]->id, ewmh->WM_PROTOCOLS), &pro, NULL))
  for (int i = 0; i < pro.atoms_len; i++) {
   xcb_client_message_event_t ev = { XCB_CLIENT_MESSAGE, 32, 0, fwin[curws]->id, wm_atoms[0] };
   ev.data.data32[0] = wm_atoms[WM_DELETE_WINDOW];
   ev.data.data32[1] = XCB_CURRENT_TIME;
   xcb_send_event(conn, 0, fwin[curws]->id, XCB_EVENT_MASK_NO_EVENT, (char *)&ev);
   goto a;
  }
 xcb_kill_client(conn, fwin[curws]->id);
a:
 xcb_icccm_get_wm_protocols_reply_wipe(&pro);
}

static void cycle(int arg) {
 if (!stack[curws] || !stack[curws]->next) return;

 if (!tabbing) {
  xcb_keycode_t *keycode = xcb_key_symbols_get_keycode(keysyms, XK_Super_L);
  xcb_grab_key(conn, 0, scr->root, XCB_MOD_MASK_ANY, *keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

  xcb_key_press_event_t *e = calloc(1, sizeof(xcb_key_press_event_t));
  e->response_type = XCB_KEY_PRESS;
  e->detail = *keycode;
  e->time = XCB_CURRENT_TIME;
  e->root = scr->root;
  e->event = scr->root;
  e->child = scr->root;
  e->state = XCB_MOD_MASK_ANY;
  e->same_screen = 1;
  
  xcb_send_event(conn, false, scr->root, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char*)e);
  xcb_flush(conn);
  free(e);

  free(keycode);

  marker = fwin[curws];
  tabbing = true; 
 }
 
 if (marker->next) raise(marker->next);
 else { 
  marker = stack[curws];
  cycle(0);
 }
}

static void change_ws(int arg) {
 if (arg == curws) return;

 traverse(stack[arg], w_map);
 traverse(stack[curws], w_unmap_ignore); 
 curws = arg;
 if (fwin[arg]) focus(fwin[curws], FOCUS); 
 
 xcb_ewmh_set_current_desktop(ewmh, 0, arg);
}

static void send_ws(int arg) {
 if (!fwin[curws] || arg == curws) return;
 fwin[curws]->ignore_unmap = true;
 insert(arg, excise(curws, fwin[curws]));
 xcb_unmap_window(conn, fwin[curws]->id);
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
 xcb_key_symbols_free(keysyms);
 xcb_disconnect(conn);
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

static void full_save_state(client_t *subj) {
 xcb_get_geometry_reply_t *temp = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, subj->id), NULL);
 subj->full_geom.x = temp->x;
 subj->full_geom.y = temp->y;
 subj->full_geom.width = temp->width;
 subj->full_geom.height = temp->height;
 free(temp);
 //don't determine value here
}

static void snap_restore_state(client_t *subj) {
 subj->snap = false;
 xcb_configure_window(conn, subj->id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (uint32_t[]){ subj->snap_geom.x, subj->snap_geom.y, subj->snap_geom.width, subj->snap_geom.height });
}

static void full_restore_state(client_t *subj) {
 //don't determine value here
 xcb_configure_window(conn, subj->id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (uint32_t[]){ subj->full_geom.x, subj->full_geom.y, subj->full_geom.width, subj->full_geom.height });
}

static void int_fullscreen(int arg) {
 if (!fwin[curws]) return;
 if (!fwin[curws]->e_full) {
  if (fwin[curws]->i_full) full_restore_state(fwin[curws]); 
  else {
   full_save_state(fwin[curws]);
   xcb_configure_window(conn, fwin[curws]->id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (uint32_t[]){ - BORDER, - BORDER, scr->width_in_pixels, scr->height_in_pixels });
  }
 }
 fwin[curws]->i_full = !fwin[curws]->i_full;
}

static void ext_fullscreen(client_t *subj) {
 if (subj->e_full) {
  if (!subj->i_full) full_restore_state(subj);
 } else {
  if (!subj->i_full) full_save_state(subj);
  xcb_configure_window(conn, subj->id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (uint32_t[]){ - BORDER, - BORDER, scr->width_in_pixels, scr->height_in_pixels });
 }
 subj->e_full = !subj->e_full;
}

#define SNAP_TEMPLATE(A, B, C, D, E) static void A(int arg) { \
 if (!fwin[curws] || fwin[curws]->e_full || fwin[curws]->i_full) return; \
 if (!fwin[curws]->snap) snap_save_state(fwin[curws]); \
 xcb_configure_window(conn, fwin[curws]->id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (uint32_t[]){ B, C, D, E}); \
}

SNAP_TEMPLATE(snap_max, GAP, GAP + TOP, scr->width_in_pixels - 2 * GAP - 2 * BORDER, scr->height_in_pixels - 2 * GAP - 2 * BORDER - TOP - BOT)
SNAP_TEMPLATE(snap_left, GAP, GAP + TOP, scr->width_in_pixels / 2 - 1.5 * GAP - BORDER * 2, scr->height_in_pixels - 2 * GAP - 2 * BORDER - TOP - BOT)
SNAP_TEMPLATE(snap_uleft, GAP, GAP + TOP, scr->width_in_pixels / 2 - 1.5 * GAP - BORDER * 2, (scr->height_in_pixels - TOP - BOT) / 2 - 1.5 * GAP - 2 * BORDER)
SNAP_TEMPLATE(snap_dleft, GAP, (scr->height_in_pixels - TOP - BOT) / 2 + GAP / 2 + TOP, scr->width_in_pixels / 2 - 1.5 * GAP - BORDER * 2, (scr->height_in_pixels - TOP - BOT) / 2 - 1.5 * GAP - 2 * BORDER)
SNAP_TEMPLATE(snap_right, scr->width_in_pixels / 2 + GAP / 2, GAP + TOP, scr->width_in_pixels / 2 - 1.5 * GAP - BORDER * 2, scr->height_in_pixels - 2 * GAP - 2 * BORDER - TOP - BOT)
SNAP_TEMPLATE(snap_uright, scr->width_in_pixels / 2 + GAP / 2, GAP + TOP, scr->width_in_pixels / 2 - 1.5 * GAP - BORDER * 2, (scr->height_in_pixels - TOP - BOT) / 2 - 1.5 * GAP - 2 * BORDER)
SNAP_TEMPLATE(snap_dright, scr->width_in_pixels / 2 + GAP / 2, (scr->height_in_pixels - TOP - BOT) / 2 + GAP / 2 + TOP, scr->width_in_pixels / 2 - 1.5 * GAP - BORDER * 2, (scr->height_in_pixels - TOP - BOT) / 2 - 1.5 * GAP - 2 * BORDER)

static void map_notify(xcb_generic_event_t *ev) {
 xcb_map_notify_event_t *e = (xcb_map_notify_event_t *)ev;
 if (e->override_redirect || wtf(e->window, NULL)) return;

 xcb_get_geometry_reply_t *init_geom = xcb_get_geometry_reply(conn, xcb_get_geometry_unchecked(conn, e->window), NULL);
 if (!init_geom) return;

 xcb_ewmh_get_atoms_reply_t type;
 if (xcb_ewmh_get_wm_window_type_reply(ewmh, xcb_ewmh_get_wm_window_type(ewmh, e->window), &type, NULL)) {
  for (unsigned int i = 0; i < type.atoms_len; i++)
   if (type.atoms[i] == ewmh->_NET_WM_WINDOW_TYPE_DOCK || type.atoms[i] == ewmh->_NET_WM_WINDOW_TYPE_TOOLBAR || type.atoms[i] == ewmh->_NET_WM_WINDOW_TYPE_DESKTOP) {
    xcb_ewmh_get_atoms_reply_wipe(&type);
    return;
   }
  xcb_ewmh_get_atoms_reply_wipe(&type);
 }
 
 uint32_t vals[] = { (scr->width_in_pixels - init_geom->width) / 2 - BORDER, (scr->height_in_pixels - TOP - BOT - init_geom->height) / 2 - BORDER, BORDER };
 xcb_configure_window(conn, e->window, XCB_CONFIG_WINDOW_BORDER_WIDTH | XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_BORDER_WIDTH, vals);
 vals[0] = XCB_EVENT_MASK_ENTER_WINDOW;
 xcb_change_window_attributes(conn, e->window, XCB_CW_EVENT_MASK, vals);

 free(init_geom);

 xcb_map_window(conn, e->window);
 xcb_change_save_set(conn, XCB_SET_MODE_INSERT, e->window);

 client_t *new = malloc(sizeof(client_t));
 new->id = e->window;
 new->ignore_unmap = false;
 new->snap = false;
 new->e_full = false;
 new->i_full = false;
 insert(curws, new);

 focus(new, FOCUS);
}

static void enter_notify(xcb_generic_event_t *ev) {
 xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;
 client_t *found = wtf(e->event, NULL);
 if (!found || found == fwin[curws]) return;
 focus(found, FOCUS);
}

static void button_press(xcb_generic_event_t *ev) {
 xcb_button_press_event_t *e = (xcb_button_press_event_t *)ev;
 client_t *found = wtf(e->child, NULL);
 if (!found) return;
 
 if (found != fwin[curws]) focus(found, FOCUS);
 if (found != stack[curws]) raise(found);
 if (found->e_full || found->i_full) return;

 xcb_get_geometry_reply_t *geom = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, fwin[curws]->id), NULL);
 if (e->detail == XCB_BUTTON_INDEX_1) {
  if (fwin[curws]->snap) {
   x = fwin[curws]->snap_geom.width * (e->event_x - geom->x) / geom->width;
   y = fwin[curws]->snap_geom.height * (e->event_y - geom->y) / geom->height; 
  } else  {
   x = e->event_x - geom->x;
   y = e->event_y - geom->y;
  }
  state = MOVE; 
 } else {
  state = RESZ;
  fwin[curws]->snap = false;
  x = geom->width - e->event_x;
  y = geom->height - e->event_y;
 }

 free(geom);
 xcb_grab_pointer(conn, 0, scr->root, XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION_HINT, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, scr->root, XCB_NONE, XCB_CURRENT_TIME);
}

static void motion_notify(xcb_generic_event_t *ev) {
 xcb_motion_notify_event_t *e = (xcb_motion_notify_event_t *)ev;
 xcb_query_pointer_reply_t *p = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, scr->root), 0);
 if (!p) return;
 
 if (state == MOVE) {
  if (fwin[curws]->snap) snap_restore_state(fwin[curws]);

  if (p->root_x < MARGIN) {
   if (p->root_y < CORNER) snap_uleft(0);
   else if (p->root_y > scr->height_in_pixels - CORNER) snap_dleft(0);
   else snap_left(0);
  } else if (p->root_y < MARGIN) {
   if (p->root_x < CORNER) snap_uleft(0);
   else if (p->root_x > scr->width_in_pixels - CORNER) snap_uright(0);
   else snap_max(0);
  } else if (p->root_x > scr->width_in_pixels - MARGIN) {
   if (p->root_y < CORNER) snap_uright(0);
   else if (p->root_y > scr->height_in_pixels - CORNER) snap_dright(0);
   else snap_right(0);
  } else if (p->root_y > scr->height_in_pixels - MARGIN) {
   if (p->root_x < CORNER) snap_dleft(0);
   else if (p->root_x > scr->width_in_pixels - CORNER) snap_dright(0);
   else goto a;
  } else {
   xcb_configure_window(conn, fwin[curws]->id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (uint32_t[]){ p->root_x - x, p->root_y - y });
   goto a;
  }
  xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
 } else if (state == RESZ) {
  xcb_configure_window(conn, fwin[curws]->id, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, (uint32_t[]){ p->root_x + x, p->root_y + y });
 }
a:
 free(p);
}

static void button_release(xcb_generic_event_t *ev) {
 xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
 state = 0;
}

static void key_press(xcb_generic_event_t *ev) {
 xcb_key_press_event_t *e = (xcb_key_press_event_t *)ev;
 xcb_keysym_t keysym = get_keysym(e->detail);
 for (int i = 0; i < LEN(keys); i++) {
  if (keysym == keys[i].key && keys[i].mod == e->state) {
   keys[i].function(keys[i].arg);
   break;
  }
 }
}

static void key_release(xcb_generic_event_t *ev) {
 xcb_key_release_event_t *e = (xcb_key_release_event_t *)ev;
 xcb_keysym_t keysym = get_keysym(e->detail);
 if (keysym == XK_Super_L && tabbing) {
  xcb_ungrab_key(conn, e->detail, scr->root, XCB_MOD_MASK_ANY);
  tabbing = false;
 }
}

static void forget_client(client_t *subj, int ws) {
 free(excise(ws, subj));
 
 if (ws == curws) {
  if (fwin[curws] == subj) {
   if (!stack[curws]) fwin[curws] = NULL;
   else focus(stack[curws], FOCUS);
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
    if (action == XCB_EWMH_WM_STATE_ADD) {
     if (!found->e_full) ext_fullscreen(found);
    } else if (action == XCB_EWMH_WM_STATE_REMOVE) {
     if (found->e_full) ext_fullscreen(found);
    } else if (action == XCB_EWMH_WM_STATE_TOGGLE) {
     ext_fullscreen(found);
    }
   }
  }
 }
}

int main(void) {
 conn = xcb_connect(NULL, NULL);
 scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

 xcb_change_window_attributes_checked(conn, scr->root, XCB_CW_EVENT_MASK, (uint32_t[]){ XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY });

 ewmh = calloc(1, sizeof(xcb_ewmh_connection_t));
 xcb_ewmh_init_atoms_replies(ewmh, xcb_ewmh_init_atoms(conn, ewmh), (void *)0);

 const char *WM_ATOM_NAME[] = { "WM_PROTOCOLS", "WM_DELETE_WINDOW", };
 get_atoms(WM_ATOM_NAME, wm_atoms, WM_COUNT);
 const char *NET_ATOM_NAME[] = { "_NET_SUPPORTED", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE" };
 get_atoms(NET_ATOM_NAME, net_atoms, NET_COUNT);
 xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root, net_atoms[NET_SUPPORTED], XCB_ATOM_ATOM, 32, NET_COUNT, net_atoms);
 
 keysyms = xcb_key_symbols_alloc(conn);
 xcb_keycode_t *keycode;
 for (int i = 0; i < LEN(keys); i++) {
  keycode = xcb_key_symbols_get_keycode(keysyms, keys[i].key);
  xcb_grab_key(conn, 0, scr->root, keys[i].mod, *keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  free(keycode);
 }

 xcb_grab_button(conn, 0, scr->root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, scr->root, XCB_NONE, XCB_BUTTON_INDEX_1, XCB_MOD_MASK_4);
 xcb_grab_button(conn, 0, scr->root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, scr->root, XCB_NONE, XCB_BUTTON_INDEX_3, XCB_MOD_MASK_4);
 
 static void (*events[XCB_NO_OPERATION])(xcb_generic_event_t *event);
 for (int i = 0; i < XCB_NO_OPERATION; i++) events[i] = NULL;
 events[XCB_BUTTON_PRESS]	    = button_press;
 events[XCB_BUTTON_RELEASE]	  = button_release;
 events[XCB_MOTION_NOTIFY]	   = motion_notify;
 events[XCB_CLIENT_MESSAGE]	  = client_message;
 //events[XCB_CONFIGURE_NOTIFY] = configure_notify;
 events[XCB_KEY_PRESS]	       = key_press;
 events[XCB_KEY_RELEASE]	     = key_release;
 events[XCB_MAP_NOTIFY]	      = map_notify;
 events[XCB_UNMAP_NOTIFY]	    = unmap_notify;
 events[XCB_DESTROY_NOTIFY]	  = destroy_notify;
 events[XCB_ENTER_NOTIFY]	    = enter_notify;
 
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
