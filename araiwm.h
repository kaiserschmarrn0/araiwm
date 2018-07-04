#ifdef config_file
#include <string.h>
#include <stdint.h>

#define STR_MAX 255
#endif

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
	int max, x, y, w, h;
} client;

typedef struct {
	xcb_window_t id;
	int mode, xoff, yoff;
} pass;

#ifdef config_file
typedef struct {
	char *key;
	uint32_t *var;
} confitem;

static uint32_t BORDER = 5,
    	 GAP = 9,
   	 TOP = 33,
   	 BOT = 0,
   	 SNAP_X = 4,
   	 SNAP_Y = 200,
	 FOCUSCOLOR = 0x9baeb1,
	 UNFOCUSCOLOR = 0x12333b;

static const confitem items[] = {
	{ "border",	&BORDER       },
	{ "gap",	&GAP          },
	{ "top",	&TOP   	      },
	{ "bot",	&BOT          },
	{ "snap_x",	&SNAP_X       },
	{ "snap_y",	&SNAP_Y       },
	{ "focuscol",	&FOCUSCOLOR   },
	{ "unfocuscol",	&UNFOCUSCOLOR },
};
#endif

static void arai_kill();
static void arai_center();
static void arai_cycle();
static void arai_max();
static void arai_chws();
static void arai_sendws();
static void arai_snap();

#ifdef config_file
static void
parse(char *file)
{
	char scan[STR_MAX];
	uint32_t val;
	FILE *fconf;
	if (!(fconf = fopen(file, "r"))) return;
	while (!feof(fconf)) {
		fscanf(fconf, "%s ", &scan);
		if (strcmp(scan, "unfocuscol") == 0 || strcmp(scan, "focuscol") == 0) fscanf(fconf, "= %x", &val);
		else fscanf(fconf, "= %d", &val);
		printf("araiwm: %s = %d\n", scan, val);
		for (int i = 0; i < sizeof(items)/sizeof(*items); i++) {
			if (strcmp(scan, items[i].key) == 0) {
				*items[i].var = val;
			}
		}
	}
	fclose(fconf);
}
#endif
