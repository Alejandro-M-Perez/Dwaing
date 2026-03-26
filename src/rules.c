#include "rules.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MIN_DEVICE_SPACING 0.25

static int push_error(ValidationError *errors, int max_errors, int count, const char *msg) {
    if (count < max_errors) {
        strncpy(errors[count].message, msg, sizeof(errors[count].message) - 1);
        errors[count].message[sizeof(errors[count].message) - 1] = '\0';
    }
    return count + 1;
}

static const Device *find_device_const(const Panel *panel, const char *id) {
    for (int i = 0; i < panel->device_count; ++i) {
        if (strcmp(panel->devices[i].id, id) == 0) {
            return &panel->devices[i];
        }
    }
    return 0;
}

static const Connector *find_connector_const(const BlockDef *block, const char *id) {
    if (!block || !id || id[0] == '\0') {
        return 0;
    }
    for (int i = 0; i < block->connector_count; ++i) {
        if (strcmp(block->connectors[i].id, id) == 0) {
            return &block->connectors[i];
        }
    }
    return 0;
}

static bool is_builtin_connector_id(const char *id) {
    return strcmp(id, "CENTER") == 0 ||
           strcmp(id, "LEFT") == 0 ||
           strcmp(id, "RIGHT") == 0 ||
           strcmp(id, "TOP") == 0 ||
           strcmp(id, "BOTTOM") == 0;
}

static bool object_world_origin(const Document *document, const DocumentObject *object, Vec2 *out) {
    Vec2 pos = object->position;
    const DocumentObject *cursor = object;

    while (cursor->parent_id[0] != '\0') {
        cursor = model_find_document_object_const(document, cursor->parent_id);
        if (!cursor) {
            return false;
        }
        pos.x += cursor->position.x;
        pos.y += cursor->position.y;
    }

    *out = pos;
    return true;
}

static bool connector_local_for_asset(const AssetDef *asset, const char *connector_id, Vec2 *out) {
    const double w = (asset && asset->width > 0.0) ? asset->width : 1.0;
    const double h = (asset && asset->height > 0.0) ? asset->height : 1.0;
    const Connector *connector = 0;

    if (!connector_id || connector_id[0] == '\0' || strcmp(connector_id, "CENTER") == 0) {
        out->x = 0.0;
        out->y = 0.0;
        return true;
    }
    if (strcmp(connector_id, "LEFT") == 0) {
        out->x = 0.0;
        out->y = h / 2.0;
        return true;
    }
    if (strcmp(connector_id, "RIGHT") == 0) {
        out->x = w;
        out->y = h / 2.0;
        return true;
    }
    if (strcmp(connector_id, "TOP") == 0) {
        out->x = w / 2.0;
        out->y = 0.0;
        return true;
    }
    if (strcmp(connector_id, "BOTTOM") == 0) {
        out->x = w / 2.0;
        out->y = h;
        return true;
    }

    connector = model_find_asset_connector_const(asset, connector_id);
    if (!connector) {
        return false;
    }
    *out = connector->position;
    return true;
}

static bool object_connection_point(const AssetLibrary *library,
                                    const Document *document,
                                    const DocumentObject *object,
                                    const char *port_id,
                                    Vec2 *out) {
    Vec2 origin;
    Vec2 local = {0.0, 0.0};
    const AssetDef *asset = 0;

    if (!object_world_origin(document, object, &origin)) {
        return false;
    }

    if (object->asset_id[0] != '\0') {
        asset = model_find_asset_const(library, object->asset_id);
    }
    if (!connector_local_for_asset(asset, port_id, &local)) {
        return false;
    }

    out->x = origin.x + local.x;
    out->y = origin.y + local.y;
    return true;
}

static bool document_object_bounds(const Document *document, const DocumentObject *object, Vec2 *min_out, Vec2 *max_out) {
    Vec2 origin;
    if (!object_world_origin(document, object, &origin)) {
        return false;
    }

    if (object->has_bounds) {
        min_out->x = origin.x;
        min_out->y = origin.y;
        max_out->x = origin.x + (object->max.x - object->min.x);
        max_out->y = origin.y + (object->max.y - object->min.y);
        return true;
    }

    min_out->x = origin.x;
    min_out->y = origin.y;
    max_out->x = origin.x + object->size.x;
    max_out->y = origin.y + object->size.y;
    return true;
}

static int orientation(Vec2 a, Vec2 b, Vec2 c) {
    const double v = (b.y - a.y) * (c.x - b.x) - (b.x - a.x) * (c.y - b.y);
    if (fabs(v) < 1e-9) return 0;
    return v > 0.0 ? 1 : 2;
}

static bool on_segment(Vec2 a, Vec2 b, Vec2 c) {
    return b.x <= fmax(a.x, c.x) + 1e-9 &&
           b.x + 1e-9 >= fmin(a.x, c.x) &&
           b.y <= fmax(a.y, c.y) + 1e-9 &&
           b.y + 1e-9 >= fmin(a.y, c.y);
}

static bool segments_intersect(Vec2 p1, Vec2 q1, Vec2 p2, Vec2 q2) {
    const int o1 = orientation(p1, q1, p2);
    const int o2 = orientation(p1, q1, q2);
    const int o3 = orientation(p2, q2, p1);
    const int o4 = orientation(p2, q2, q1);

    if (o1 != o2 && o3 != o4) return true;
    if (o1 == 0 && on_segment(p1, p2, q1)) return true;
    if (o2 == 0 && on_segment(p1, q2, q1)) return true;
    if (o3 == 0 && on_segment(p2, p1, q2)) return true;
    if (o4 == 0 && on_segment(p2, q1, q2)) return true;
    return false;
}

static int validate_voltage(const Device *d, ValidationError *errors, int max_errors, int count) {
    char msg[256];

    if (d->domain == DOMAIN_UNSPECIFIED) {
        snprintf(msg, sizeof(msg), "device '%s': domain must be AC or DC", d->id);
        return push_error(errors, max_errors, count, msg);
    }

    if (d->domain == DOMAIN_DC && d->ac_level != AC_LEVEL_NONE) {
        snprintf(msg, sizeof(msg), "device '%s': DC device cannot have ac_level", d->id);
        count = push_error(errors, max_errors, count, msg);
    }

    if (d->domain == DOMAIN_AC && d->ac_level != AC_LEVEL_120 && d->ac_level != AC_LEVEL_240 && d->ac_level != AC_LEVEL_480) {
        snprintf(msg, sizeof(msg), "device '%s': AC device must have ac_level 120/240/480", d->id);
        count = push_error(errors, max_errors, count, msg);
    }

    return count;
}

int collect_panel_warnings(const BlockLibrary *library, const Panel *panel, ValidationError *warnings, int max_warnings) {
    int count = 0;

    for (int i = 0; i < panel->device_count; ++i) {
        const Device *d = &panel->devices[i];
        const BlockDef *block = model_find_block_const(library, d->block_id);
        char msg[256];

        if (!d->has_amax) {
            snprintf(msg, sizeof(msg), "device '%s': missing Amax tag", d->id);
            count = push_error(warnings, max_warnings, count, msg);
        }

        if (block && block->connector_count == 0) {
            snprintf(msg, sizeof(msg), "device '%s': block '%s' has no V connectors", d->id, d->block_id);
            count = push_error(warnings, max_warnings, count, msg);
        }
    }

    return count;
}

int validate_panel(const BlockLibrary *library, const Panel *panel, ValidationError *errors, int max_errors) {
    int count = 0;

    if (panel->panel_width <= 0.0 || panel->panel_height <= 0.0) {
        count = push_error(errors, max_errors, count, "panel: width and height must be > 0");
    }
    if (panel->backplane_width <= 0.0 || panel->backplane_height <= 0.0) {
        count = push_error(errors, max_errors, count, "panel: back_width and back_height must be > 0");
    }

    for (int i = 0; i < panel->region_count; ++i) {
        const Region *r = &panel->regions[i];
        char msg[256];
        if (r->parent_region_id[0] != '\0') {
            const Region *parent = model_find_region_const(panel, r->parent_region_id);
            if (!parent) {
                snprintf(msg, sizeof(msg), "region '%s': unknown parent region '%s'", r->id, r->parent_region_id);
                count = push_error(errors, max_errors, count, msg);
            } else if (r->min.x < parent->min.x || r->max.x > parent->max.x || r->min.y < parent->min.y || r->max.y > parent->max.y) {
                snprintf(msg, sizeof(msg), "region '%s': does not fit inside parent region '%s'", r->id, r->parent_region_id);
                count = push_error(errors, max_errors, count, msg);
            }
        } else {
            if (r->min.x < 0.0 || r->min.y < 0.0 || r->max.x > panel->panel_width || r->max.y > panel->panel_height) {
                snprintf(msg, sizeof(msg), "region '%s': does not fit inside panel bounds", r->id);
                count = push_error(errors, max_errors, count, msg);
            }
        }
    }

    for (int i = 0; i < panel->duct_count; ++i) {
        const WireDuct *d = &panel->ducts[i];
        const Region *region = model_find_region_const(panel, d->region_id);
        char msg[256];
        if (!region) {
            snprintf(msg, sizeof(msg), "wire_duct '%s': unknown region '%s'", d->id, d->region_id);
            count = push_error(errors, max_errors, count, msg);
        }
        if (d->width_in < 1.0 || d->width_in > 8.0) {
            snprintf(msg, sizeof(msg), "wire_duct '%s': width_in must be between 1 and 8", d->id);
            count = push_error(errors, max_errors, count, msg);
        }
        if (!(d->orientation == 'H' || d->orientation == 'h' || d->orientation == 'V' || d->orientation == 'v')) {
            snprintf(msg, sizeof(msg), "wire_duct '%s': orientation must be H or V", d->id);
            count = push_error(errors, max_errors, count, msg);
        }
    }

    for (int bi = 0; bi < library->block_count; ++bi) {
        const BlockDef *block = &library->blocks[bi];
        for (int ci = 0; ci < block->connector_count; ++ci) {
            const Connector *c = &block->connectors[ci];
            char msg[256];

            if (c->direction.x == 0.0 && c->direction.y == 0.0) {
                snprintf(msg, sizeof(msg), "block '%s' connector '%s': direction dx/dy cannot both be 0", block->id, c->id);
                count = push_error(errors, max_errors, count, msg);
            }

            for (int cj = ci + 1; cj < block->connector_count; ++cj) {
                if (strcmp(c->id, block->connectors[cj].id) == 0) {
                    snprintf(msg, sizeof(msg), "block '%s': duplicate connector id '%s'", block->id, c->id);
                    count = push_error(errors, max_errors, count, msg);
                    break;
                }
            }
        }
    }

    for (int i = 0; i < panel->device_count; ++i) {
        const Device *d = &panel->devices[i];
        const Region *region = model_find_region_const(panel, d->region_id);
        const Rail *rail = model_find_rail_const(panel, d->rail_id);
        const BlockDef *block = model_find_block_const(library, d->block_id);
        char msg[256];

        if (!block) {
            snprintf(msg, sizeof(msg), "device '%s': unknown block '%s'", d->id, d->block_id);
            count = push_error(errors, max_errors, count, msg);
        }

        if (!region) {
            snprintf(msg, sizeof(msg), "device '%s': unknown region '%s'", d->id, d->region_id);
            count = push_error(errors, max_errors, count, msg);
        } else {
            if (block) {
                double w = block->width > 0.0 ? block->width : 1.25;
                double h = block->height > 0.0 ? block->height : 0.75;
                const int rot = d->rotation_deg % 180;
                double min_x;
                double max_x;
                double min_y;
                double max_y;

                if (rot != 0) {
                    const double t = w;
                    w = h;
                    h = t;
                }

                min_x = d->position.x - (w / 2.0);
                max_x = d->position.x + (w / 2.0);
                min_y = d->position.y - (h / 2.0);
                max_y = d->position.y + (h / 2.0);

                if (min_x < region->min.x || max_x > region->max.x || min_y < region->min.y || max_y > region->max.y) {
                    snprintf(msg, sizeof(msg), "device '%s': geometry does not fit inside region '%s'", d->id, d->region_id);
                    count = push_error(errors, max_errors, count, msg);
                }
            } else {
                if (d->position.x < region->min.x || d->position.x > region->max.x || d->position.y < region->min.y || d->position.y > region->max.y) {
                    snprintf(msg, sizeof(msg), "device '%s': position is outside region '%s'", d->id, d->region_id);
                    count = push_error(errors, max_errors, count, msg);
                }
            }
        }

        if (!rail) {
            snprintf(msg, sizeof(msg), "device '%s': unknown rail '%s'", d->id, d->rail_id);
            count = push_error(errors, max_errors, count, msg);
        } else if (strcmp(rail->region_id, d->region_id) != 0) {
            snprintf(msg, sizeof(msg), "device '%s': rail '%s' is not in region '%s'", d->id, d->rail_id, d->region_id);
            count = push_error(errors, max_errors, count, msg);
        }

        count = validate_voltage(d, errors, max_errors, count);

        if (d->relation != DEVICE_REL_NONE && block) {
            const Connector *self_conn = 0;
            const Connector *target_conn = 0;

            if (d->self_connector_id[0] != '\0' && !is_builtin_connector_id(d->self_connector_id)) {
                self_conn = find_connector_const(block, d->self_connector_id);
                if (!self_conn) {
                    snprintf(msg, sizeof(msg), "device '%s': unknown self_connector '%s' on block '%s'", d->id, d->self_connector_id, block->id);
                    count = push_error(errors, max_errors, count, msg);
                }
            }

            if (d->target_connector_id[0] != '\0' && !is_builtin_connector_id(d->target_connector_id)) {
                const Device *anchor = find_device_const(panel, d->of_device_id);
                const BlockDef *anchor_block = anchor ? model_find_block_const(library, anchor->block_id) : 0;
                if (!anchor) {
                    snprintf(msg, sizeof(msg), "device '%s': relation target '%s' not found", d->id, d->of_device_id);
                    count = push_error(errors, max_errors, count, msg);
                } else if (!anchor_block) {
                    snprintf(msg, sizeof(msg), "device '%s': relation target block '%s' not found", d->id, anchor->block_id);
                    count = push_error(errors, max_errors, count, msg);
                } else {
                    target_conn = find_connector_const(anchor_block, d->target_connector_id);
                    if (!target_conn) {
                        snprintf(msg, sizeof(msg), "device '%s': unknown target_connector '%s' on block '%s'", d->id, d->target_connector_id, anchor_block->id);
                        count = push_error(errors, max_errors, count, msg);
                    }
                }
            }

            if (self_conn && target_conn && strcmp(self_conn->voltage_tag, target_conn->voltage_tag) != 0) {
                snprintf(msg, sizeof(msg), "device '%s': connector voltage mismatch '%s' vs '%s'", d->id, self_conn->voltage_tag, target_conn->voltage_tag);
                count = push_error(errors, max_errors, count, msg);
            }
        }

        if (!d->has_amax || d->amax <= 0.0) {
            snprintf(msg, sizeof(msg), "device '%s': Amax tag required and must be > 0", d->id);
            count = push_error(errors, max_errors, count, msg);
        }
    }

    for (int i = 0; i < panel->rail_count; ++i) {
        const Rail *r = &panel->rails[i];
        char msg[256];
        if (strcmp(r->layout, "horizontal") != 0 && strcmp(r->layout, "vertical") != 0) {
            snprintf(msg, sizeof(msg), "rail '%s': layout must be horizontal or vertical", r->id);
            count = push_error(errors, max_errors, count, msg);
        }
    }

    for (int i = 0; i < panel->rail_count; ++i) {
        for (int j = i + 1; j < panel->rail_count; ++j) {
            const Rail *a = &panel->rails[i];
            const Rail *b = &panel->rails[j];
            char msg[256];

            if (strcmp(a->region_id, b->region_id) != 0) {
                continue;
            }
            if (!segments_intersect(a->start, a->end, b->start, b->end)) {
                continue;
            }
            snprintf(msg, sizeof(msg), "rails '%s' and '%s': intersect in region '%s'", a->id, b->id, a->region_id);
            count = push_error(errors, max_errors, count, msg);
        }
    }

    for (int i = 0; i < panel->device_count; ++i) {
        for (int j = i + 1; j < panel->device_count; ++j) {
            const Device *a = &panel->devices[i];
            const Device *b = &panel->devices[j];
            char msg[256];

            if (strcmp(a->rail_id, b->rail_id) != 0) {
                continue;
            }

            if (vec2_distance(a->position, b->position) < MIN_DEVICE_SPACING) {
                snprintf(msg, sizeof(msg), "devices '%s' and '%s': spacing violation on rail '%s'", a->id, b->id, a->rail_id);
                count = push_error(errors, max_errors, count, msg);
            }

            if (a->domain != DOMAIN_UNSPECIFIED && b->domain != DOMAIN_UNSPECIFIED && a->domain != b->domain) {
                snprintf(msg, sizeof(msg), "devices '%s' and '%s': AC/DC mixed on rail '%s'", a->id, b->id, a->rail_id);
                count = push_error(errors, max_errors, count, msg);
            }

            if (a->domain == DOMAIN_AC && b->domain == DOMAIN_AC && a->ac_level != b->ac_level) {
                snprintf(msg, sizeof(msg), "devices '%s' and '%s': mixed AC levels (%d vs %d) on rail '%s'", a->id, b->id, (int)a->ac_level, (int)b->ac_level, a->rail_id);
                count = push_error(errors, max_errors, count, msg);
            }
        }
    }

    return count;
}

int collect_document_warnings(const AssetLibrary *library, const Document *document, ValidationError *warnings, int max_warnings) {
    if (document->kind == DOCUMENT_KIND_PANEL && document->has_panel_model) {
        return collect_panel_warnings(&library->legacy, &document->panel, warnings, max_warnings);
    }
    (void)library;
    (void)document;
    (void)warnings;
    (void)max_warnings;
    return 0;
}

int validate_document(const AssetLibrary *library, const Document *document, ValidationError *errors, int max_errors) {
    int count = 0;

    if (document->kind == DOCUMENT_KIND_PANEL && document->has_panel_model) {
        return validate_panel(&library->legacy, &document->panel, errors, max_errors);
    }

    if (document->kind == DOCUMENT_KIND_UNKNOWN) {
        count = push_error(errors, max_errors, count, "document: kind must be specified");
    }
    if (document->width <= 0.0 || document->height <= 0.0) {
        count = push_error(errors, max_errors, count, "document: width and height must be > 0");
    }

    for (int i = 0; i < document->object_count; ++i) {
        const DocumentObject *object = &document->objects[i];
        char msg[256];
        Vec2 object_min;
        Vec2 object_max;

        if (object->parent_id[0] != '\0' && !model_find_document_object_const(document, object->parent_id)) {
            snprintf(msg, sizeof(msg), "object '%s': unknown parent '%s'", object->id, object->parent_id);
            count = push_error(errors, max_errors, count, msg);
        }

        if (object->kind == DOC_OBJECT_SYMBOL && object->asset_id[0] != '\0' && !model_find_asset_const(library, object->asset_id)) {
            snprintf(msg, sizeof(msg), "object '%s': unknown asset '%s'", object->id, object->asset_id);
            count = push_error(errors, max_errors, count, msg);
        }

        if (!document_object_bounds(document, object, &object_min, &object_max)) {
            snprintf(msg, sizeof(msg), "object '%s': parent chain is invalid", object->id);
            count = push_error(errors, max_errors, count, msg);
            continue;
        }

        if (object->parent_id[0] != '\0') {
            const DocumentObject *parent = model_find_document_object_const(document, object->parent_id);
            if (parent) {
                Vec2 parent_min;
                Vec2 parent_max;
                if (document_object_bounds(document, parent, &parent_min, &parent_max) &&
                    (object_min.x < parent_min.x || object_max.x > parent_max.x ||
                     object_min.y < parent_min.y || object_max.y > parent_max.y)) {
                    snprintf(msg, sizeof(msg), "object '%s': does not fit inside parent '%s'", object->id, object->parent_id);
                    count = push_error(errors, max_errors, count, msg);
                }
            }
        } else if (object->kind == DOC_OBJECT_GROUP || object->kind == DOC_OBJECT_PANEL) {
            if (object_min.x < 0.0 || object_min.y < 0.0 ||
                object_max.x > document->width || object_max.y > document->height) {
                snprintf(msg, sizeof(msg), "object '%s': does not fit inside document bounds", object->id);
                count = push_error(errors, max_errors, count, msg);
            }
        }
    }

    for (int i = 0; i < document->connection_count; ++i) {
        const DocumentConnection *connection = &document->connections[i];
        char msg[256];
        const DocumentObject *from_object = model_find_document_object_const(document, connection->from_object_id);
        const DocumentObject *to_object = model_find_document_object_const(document, connection->to_object_id);
        Vec2 point;

        if (!from_object) {
            snprintf(msg, sizeof(msg), "connection '%s': unknown from object '%s'", connection->id, connection->from_object_id);
            count = push_error(errors, max_errors, count, msg);
        }
        if (!to_object) {
            snprintf(msg, sizeof(msg), "connection '%s': unknown to object '%s'", connection->id, connection->to_object_id);
            count = push_error(errors, max_errors, count, msg);
        }
        if (from_object && !object_connection_point(library, document, from_object, connection->from_port_id, &point)) {
            snprintf(msg, sizeof(msg), "connection '%s': unknown from port '%s' on '%s'",
                     connection->id,
                     connection->from_port_id[0] ? connection->from_port_id : "(default)",
                     connection->from_object_id);
            count = push_error(errors, max_errors, count, msg);
        }
        if (to_object && !object_connection_point(library, document, to_object, connection->to_port_id, &point)) {
            snprintf(msg, sizeof(msg), "connection '%s': unknown to port '%s' on '%s'",
                     connection->id,
                     connection->to_port_id[0] ? connection->to_port_id : "(default)",
                     connection->to_object_id);
            count = push_error(errors, max_errors, count, msg);
        }
    }

    return count;
}
