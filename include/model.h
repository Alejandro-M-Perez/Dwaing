#ifndef MODEL_H
#define MODEL_H

#include "types.h"
#include "geom.h"

typedef struct {
    char id[MAX_ID_LEN + 1];
    Vec2 position;
    Vec2 direction;
    char type[MAX_ID_LEN + 1];
} Connector;

typedef struct {
    char id[MAX_ID_LEN + 1];
    Connector connectors[MAX_CONNECTORS];
    int connector_count;
} BlockDef;

typedef struct {
    char id[MAX_ID_LEN + 1];
    Vec2 min;
    Vec2 max;
} Region;

typedef struct {
    char id[MAX_ID_LEN + 1];
    char region_id[MAX_ID_LEN + 1];
    Vec2 start;
    Vec2 end;
} Rail;

typedef struct {
    char id[MAX_ID_LEN + 1];
    char block_id[MAX_ID_LEN + 1];
    char region_id[MAX_ID_LEN + 1];
    char rail_id[MAX_ID_LEN + 1];
    Vec2 position;
    VoltageDomain domain;
    AcLevel ac_level;
} Device;

typedef struct {
    BlockDef blocks[MAX_BLOCKS];
    int block_count;
} BlockLibrary;

typedef struct {
    char name[MAX_ID_LEN + 1];
    Region regions[MAX_REGIONS];
    int region_count;
    Rail rails[MAX_RAILS];
    int rail_count;
    Device devices[MAX_DEVICES];
    int device_count;
} Panel;

void model_init_library(BlockLibrary *library);
void model_init_panel(Panel *panel);

BlockDef *model_find_block(BlockLibrary *library, const char *id);
const BlockDef *model_find_block_const(const BlockLibrary *library, const char *id);
Region *model_find_region(Panel *panel, const char *id);
const Region *model_find_region_const(const Panel *panel, const char *id);
Rail *model_find_rail(Panel *panel, const char *id);
const Rail *model_find_rail_const(const Panel *panel, const char *id);

#endif
