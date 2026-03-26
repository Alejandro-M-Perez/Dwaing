#include "model.h"

#include <stdio.h>
#include <string.h>

void model_init_library(BlockLibrary *library) {
    memset(library, 0, sizeof(*library));
}

void model_init_panel(Panel *panel) {
    memset(panel, 0, sizeof(*panel));
}

void model_init_asset_library(AssetLibrary *library) {
    memset(library, 0, sizeof(*library));
}

void model_init_document(Document *document) {
    memset(document, 0, sizeof(*document));
}

const BlockDef *model_find_block_const(const BlockLibrary *library, const char *id) {
    for (int i = 0; i < library->block_count; ++i) {
        if (strcmp(library->blocks[i].id, id) == 0) {
            return &library->blocks[i];
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

const Rail *model_find_rail_const(const Panel *panel, const char *id) {
    for (int i = 0; i < panel->rail_count; ++i) {
        if (strcmp(panel->rails[i].id, id) == 0) {
            return &panel->rails[i];
        }
    }
    return 0;
}

const AssetDef *model_find_asset_const(const AssetLibrary *library, const char *id) {
    for (int i = 0; i < library->asset_count; ++i) {
        if (strcmp(library->assets[i].id, id) == 0) {
            return &library->assets[i];
        }
    }
    return 0;
}

const Connector *model_find_asset_connector_const(const AssetDef *asset, const char *id) {
    if (!asset || !id || id[0] == '\0') {
        return 0;
    }
    for (int i = 0; i < asset->connector_count; ++i) {
        if (strcmp(asset->connectors[i].id, id) == 0) {
            return &asset->connectors[i];
        }
    }
    return 0;
}

const DocumentObject *model_find_document_object_const(const Document *document, const char *id) {
    for (int i = 0; i < document->object_count; ++i) {
        if (strcmp(document->objects[i].id, id) == 0) {
            return &document->objects[i];
        }
    }
    return 0;
}

const char *document_kind_to_str(DocumentKind kind) {
    switch (kind) {
        case DOCUMENT_KIND_PANEL: return "panel";
        case DOCUMENT_KIND_ONE_LINE: return "one_line";
        case DOCUMENT_KIND_PID: return "pid";
        case DOCUMENT_KIND_GA: return "ga";
        default: return "unknown";
    }
}

DocumentKind document_kind_from_str(const char *raw) {
    if (!raw || raw[0] == '\0') {
        return DOCUMENT_KIND_UNKNOWN;
    }
    if (strcmp(raw, "panel") == 0) {
        return DOCUMENT_KIND_PANEL;
    }
    if (strcmp(raw, "one_line") == 0 || strcmp(raw, "one-line") == 0) {
        return DOCUMENT_KIND_ONE_LINE;
    }
    if (strcmp(raw, "pid") == 0 || strcmp(raw, "p&id") == 0) {
        return DOCUMENT_KIND_PID;
    }
    if (strcmp(raw, "ga") == 0 || strcmp(raw, "general_arrangement") == 0) {
        return DOCUMENT_KIND_GA;
    }
    return DOCUMENT_KIND_UNKNOWN;
}

void model_project_block_library_to_asset_library(const BlockLibrary *source, AssetLibrary *dest) {
    const BlockLibrary snapshot = *source;
    const BlockLibrary *src = &snapshot;

    model_init_asset_library(dest);
    dest->legacy = snapshot;
    for (int i = 0; i < src->block_count && i < MAX_BLOCKS; ++i) {
        AssetDef *asset = &dest->assets[dest->asset_count++];
        const BlockDef *block = &src->blocks[i];
        memset(asset, 0, sizeof(*asset));
        snprintf(asset->id, sizeof(asset->id), "%s", block->id);
        snprintf(asset->name, sizeof(asset->name), "%s", block->name);
        asset->kind = ASSET_KIND_SYMBOL;
        asset->width = block->width;
        asset->height = block->height;
        asset->connector_count = block->connector_count;
        memcpy(asset->connectors, block->connectors, sizeof(block->connectors));
    }
}

bool model_project_panel_to_document(const Panel *panel, Document *document, char *error, int error_len) {
    const Panel snapshot = *panel;
    const Panel *src = &snapshot;

    model_init_document(document);
    document->kind = DOCUMENT_KIND_PANEL;
    memcpy(document->name, src->name, sizeof(document->name));
    document->name[sizeof(document->name) - 1] = '\0';
    document->width = src->panel_width;
    document->height = src->panel_height;
    document->panel = snapshot;
    document->has_panel_model = true;

    {
        DocumentObject *root = &document->objects[document->object_count++];
        memset(root, 0, sizeof(*root));
        snprintf(root->id, sizeof(root->id), "%s", "panel");
        root->kind = DOC_OBJECT_PANEL;
        snprintf(root->role, sizeof(root->role), "%s", "panel");
        root->size.x = src->panel_width;
        root->size.y = src->panel_height;
        root->min.x = 0.0;
        root->min.y = 0.0;
        root->max.x = src->panel_width;
        root->max.y = src->panel_height;
        root->has_bounds = true;
    }

    for (int i = 0; i < src->region_count; ++i) {
        const Region *region = &src->regions[i];
        DocumentObject *object;
        if (document->object_count >= MAX_DOCUMENT_OBJECTS) {
            snprintf(error, error_len, "document object limit reached while adding region '%s'", region->id);
            return false;
        }
        object = &document->objects[document->object_count++];
        memset(object, 0, sizeof(*object));
        snprintf(object->id, sizeof(object->id), "%s", region->id);
        object->kind = DOC_OBJECT_GROUP;
        snprintf(object->parent_id, sizeof(object->parent_id), "%s", region->parent_region_id[0] ? region->parent_region_id : "panel");
        snprintf(object->role, sizeof(object->role), "%s", "region");
        object->position = region->min;
        object->size.x = region->max.x - region->min.x;
        object->size.y = region->max.y - region->min.y;
        object->min = region->min;
        object->max = region->max;
        object->has_bounds = true;
    }

    for (int i = 0; i < src->rail_count; ++i) {
        const Rail *rail = &src->rails[i];
        DocumentObject *object;
        if (document->object_count >= MAX_DOCUMENT_OBJECTS) {
            snprintf(error, error_len, "document object limit reached while adding rail '%s'", rail->id);
            return false;
        }
        object = &document->objects[document->object_count++];
        memset(object, 0, sizeof(*object));
        snprintf(object->id, sizeof(object->id), "%s", rail->id);
        object->kind = DOC_OBJECT_GUIDE;
        snprintf(object->parent_id, sizeof(object->parent_id), "%s", rail->region_id);
        snprintf(object->role, sizeof(object->role), "%s", "rail");
        object->position = rail->start;
        object->size.x = rail->end.x - rail->start.x;
        object->size.y = rail->end.y - rail->start.y;
        object->min.x = rail->start.x < rail->end.x ? rail->start.x : rail->end.x;
        object->min.y = rail->start.y < rail->end.y ? rail->start.y : rail->end.y;
        object->max.x = rail->start.x > rail->end.x ? rail->start.x : rail->end.x;
        object->max.y = rail->start.y > rail->end.y ? rail->start.y : rail->end.y;
        object->has_bounds = true;
    }

    for (int i = 0; i < src->duct_count; ++i) {
        const WireDuct *duct = &src->ducts[i];
        DocumentObject *object;
        if (document->object_count >= MAX_DOCUMENT_OBJECTS) {
            snprintf(error, error_len, "document object limit reached while adding wire duct '%s'", duct->id);
            return false;
        }
        object = &document->objects[document->object_count++];
        memset(object, 0, sizeof(*object));
        snprintf(object->id, sizeof(object->id), "%s", duct->id);
        object->kind = DOC_OBJECT_GUIDE;
        snprintf(object->parent_id, sizeof(object->parent_id), "%s", duct->region_id);
        snprintf(object->role, sizeof(object->role), "%s", "wire_duct");
        object->position = duct->origin;
        if (duct->orientation == 'V' || duct->orientation == 'v') {
            object->size.x = duct->width_in;
            object->size.y = duct->length;
        } else {
            object->size.x = duct->length;
            object->size.y = duct->width_in;
        }
        object->min = duct->origin;
        object->max.x = duct->origin.x + object->size.x;
        object->max.y = duct->origin.y + object->size.y;
        object->has_bounds = true;
    }

    for (int i = 0; i < src->device_count; ++i) {
        const Device *device = &src->devices[i];
        DocumentObject *object;
        if (document->object_count >= MAX_DOCUMENT_OBJECTS) {
            snprintf(error, error_len, "document object limit reached while adding device '%s'", device->id);
            return false;
        }
        object = &document->objects[document->object_count++];
        memset(object, 0, sizeof(*object));
        snprintf(object->id, sizeof(object->id), "%s", device->id);
        object->kind = DOC_OBJECT_SYMBOL;
        snprintf(object->parent_id, sizeof(object->parent_id), "%s", device->rail_id[0] ? device->rail_id : device->region_id);
        snprintf(object->asset_id, sizeof(object->asset_id), "%s", device->block_id);
        snprintf(object->role, sizeof(object->role), "%s", "device");
        object->position = device->position;
    }

    return true;
}
