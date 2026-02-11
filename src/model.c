#include "model.h"

#include <string.h>

void model_init_library(BlockLibrary *library) {
    memset(library, 0, sizeof(*library));
}

void model_init_panel(Panel *panel) {
    memset(panel, 0, sizeof(*panel));
}

BlockDef *model_find_block(BlockLibrary *library, const char *id) {
    for (int i = 0; i < library->block_count; ++i) {
        if (strcmp(library->blocks[i].id, id) == 0) {
            return &library->blocks[i];
        }
    }
    return 0;
}

const BlockDef *model_find_block_const(const BlockLibrary *library, const char *id) {
    for (int i = 0; i < library->block_count; ++i) {
        if (strcmp(library->blocks[i].id, id) == 0) {
            return &library->blocks[i];
        }
    }
    return 0;
}

Region *model_find_region(Panel *panel, const char *id) {
    for (int i = 0; i < panel->region_count; ++i) {
        if (strcmp(panel->regions[i].id, id) == 0) {
            return &panel->regions[i];
        }
    }
    return 0;
}

const Region *model_find_region_const(const Panel *panel, const char *id) {
    for (int i = 0; i < panel->region_count; ++i) {
        if (strcmp(panel->regions[i].id, id) == 0) {
            return &panel->regions[i];
        }
    }
    return 0;
}

Rail *model_find_rail(Panel *panel, const char *id) {
    for (int i = 0; i < panel->rail_count; ++i) {
        if (strcmp(panel->rails[i].id, id) == 0) {
            return &panel->rails[i];
        }
    }
    return 0;
}

const Rail *model_find_rail_const(const Panel *panel, const char *id) {
    for (int i = 0; i < panel->rail_count; ++i) {
        if (strcmp(panel->rails[i].id, id) == 0) {
            return &panel->rails[i];
        }
    }
    return 0;
}
