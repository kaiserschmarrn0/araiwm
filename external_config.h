#include <string.h>
#include <stdint.h>
//#include "types.h"

#define STR_MAX 255

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
