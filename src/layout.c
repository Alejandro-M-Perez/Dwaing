#include "layout.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const Device *find_device_const(const Panel *panel, const char *id, int *index_out) {
    for (int i = 0; i < panel->device_count; ++i) {
        if (strcmp(panel->devices[i].id, id) == 0) {
            if (index_out) {
                *index_out = i;
            }
            return &panel->devices[i];
        }
    }
    return 0;
}

static void rotate_local_by_deg(Vec2 *v, int deg) {
    if (deg == 90) {
        const double x = v->x;
        const double y = v->y;
        v->x = -y;
        v->y = x;
    } else if (deg == 180) {
        v->x = -v->x;
        v->y = -v->y;
    } else if (deg == 270) {
        const double x = v->x;
        const double y = v->y;
        v->x = y;
        v->y = -x;
    }
}

static void assign_device_rotation_from_rails(Panel *panel) {
    for (int i = 0; i < panel->device_count; ++i) {
        Device *d = &panel->devices[i];
        const Rail *rail = model_find_rail_const(panel, d->rail_id);
        if (rail && (rail->orientation == 'V' || rail->orientation == 'v')) {
            d->rotation_deg = 90;
        } else {
            d->rotation_deg = 0;
        }
    }
}

static bool connector_local(const BlockDef *block, const char *connector_id, Vec2 *out) {
    const double w = (block && block->width > 0.0) ? block->width : 1.25;
    const double h = (block && block->height > 0.0) ? block->height : 0.75;

    if (!connector_id || connector_id[0] == '\0' || strcmp(connector_id, "CENTER") == 0) {
        out->x = 0.0;
        out->y = 0.0;
        return true;
    }
    if (strcmp(connector_id, "LEFT") == 0) {
        out->x = -w / 2.0;
        out->y = 0.0;
        return true;
    }
    if (strcmp(connector_id, "RIGHT") == 0) {
        out->x = w / 2.0;
        out->y = 0.0;
        return true;
    }
    if (strcmp(connector_id, "TOP") == 0) {
        out->x = 0.0;
        out->y = -h / 2.0;
        return true;
    }
    if (strcmp(connector_id, "BOTTOM") == 0) {
        out->x = 0.0;
        out->y = h / 2.0;
        return true;
    }

    if (block) {
        for (int i = 0; i < block->connector_count; ++i) {
            if (strcmp(block->connectors[i].id, connector_id) == 0) {
                *out = block->connectors[i].position;
                return true;
            }
        }
    }

    return false;
}

static bool document_object_world_origin(const Document *document, int object_index, Vec2 *out) {
    Vec2 pos = document->objects[object_index].position;
    const DocumentObject *cursor = &document->objects[object_index];

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

static bool asset_connector_local(const AssetDef *asset, const char *connector_id, Vec2 *out) {
    const double w = (asset && asset->width > 0.0) ? asset->width : 1.0;
    const double h = (asset && asset->height > 0.0) ? asset->height : 1.0;
    const Connector *connector = 0;

    if (!connector_id || connector_id[0] == '\0' || strcmp(connector_id, "CENTER") == 0) {
        out->x = w / 2.0;
        out->y = h / 2.0;
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

static bool document_connection_endpoint(const AssetLibrary *library,
                                         const Document *document,
                                         const DocumentObject *object,
                                         const char *connector_id,
                                         Vec2 *out) {
    const AssetDef *asset = 0;
    Vec2 local;

    if (!object) {
        return false;
    }
    if (object->asset_id[0] != '\0') {
        asset = model_find_asset_const(library, object->asset_id);
    }
    if (!asset_connector_local(asset, connector_id, &local)) {
        return false;
    }

    for (int i = 0; i < document->object_count; ++i) {
        if (&document->objects[i] == object) {
            if (!document_object_world_origin(document, i, out)) {
                return false;
            }
            out->x += local.x;
            out->y += local.y;
            return true;
        }
    }

    return false;
}

static bool region_size_ready(const Region *r) {
    return r->has_explicit_bounds || (r->width > 0.0 && r->height > 0.0);
}

static bool try_compute_auto_region_size(Panel *panel, int region_index, char *error, int error_len) {
    Region *r = &panel->regions[region_index];
    double width_acc = r->margin;
    double height_acc = r->margin;
    double max_child_w = 0.0;
    double max_child_h = 0.0;
    int child_count = 0;

    if (!r->has_auto_size || r->has_explicit_bounds) {
        return true;
    }

    for (int i = 0; i < panel->region_count; ++i) {
        const Region *child = &panel->regions[i];
        if (strcmp(child->parent_region_id, r->id) != 0) {
            continue;
        }
        if (!region_size_ready(child)) {
            return false;
        }
        ++child_count;
        if (strcmp(r->flow, "horizontal") == 0) {
            width_acc += child->width + child->margin;
            if ((child->height + (2.0 * child->margin)) > max_child_h) {
                max_child_h = child->height + (2.0 * child->margin);
            }
        } else {
            height_acc += child->height + child->margin;
            if ((child->width + (2.0 * child->margin)) > max_child_w) {
                max_child_w = child->width + (2.0 * child->margin);
            }
        }
    }

    if (child_count == 0) {
        snprintf(error, error_len, "region '%s': size='auto' requires child regions", r->id);
        return false;
    }

    if (strcmp(r->flow, "horizontal") == 0) {
        r->width = width_acc + r->margin;
        r->height = max_child_h;
    } else {
        r->width = max_child_w;
        r->height = height_acc + r->margin;
    }
    return true;
}

static bool auto_layout_regions(Panel *panel, char *error, int error_len) {
    bool resolved[MAX_REGIONS];
    int unresolved = 0;

    for (int i = 0; i < panel->region_count; ++i) {
        Region *r = &panel->regions[i];
        resolved[i] = r->has_explicit_bounds;
        if (r->has_explicit_bounds) {
            r->width = r->max.x - r->min.x;
            r->height = r->max.y - r->min.y;
        } else {
            ++unresolved;
        }
    }

    for (int pass = 0; pass < panel->region_count + 2 && unresolved > 0; ++pass) {
        bool progress = false;

        for (int i = 0; i < panel->region_count; ++i) {
            Region *r = &panel->regions[i];
            double parent_min_x = 0.0;
            double parent_min_y = 0.0;
            double parent_max_x = panel->panel_width;
            double parent_max_y = panel->panel_height;
            const Region *parent = 0;

            if (resolved[i]) {
                continue;
            }

            if (r->parent_region_id[0] != '\0') {
                parent = model_find_region_const(panel, r->parent_region_id);
                if (!parent || !(parent->max.x > parent->min.x && parent->max.y > parent->min.y)) {
                    continue;
                }
                parent_min_x = parent->min.x;
                parent_min_y = parent->min.y;
                parent_max_x = parent->max.x;
                parent_max_y = parent->max.y;
            }

            if (r->has_auto_size) {
                if (!try_compute_auto_region_size(panel, i, error, error_len)) {
                    if (strstr(error, "requires child regions")) {
                        return false;
                    }
                    continue;
                }
            }
            if (r->width <= 0.0 || r->height <= 0.0) {
                continue;
            }

            {
                const bool right_corner = (strcmp(r->corner, "top_right") == 0 || strcmp(r->corner, "bottom_right") == 0);
                const bool bottom_corner = (strcmp(r->corner, "bottom_left") == 0 || strcmp(r->corner, "bottom_right") == 0);
                const char *parent_flow = (parent && parent->flow[0] != '\0') ? parent->flow : "vertical";
                double cursor = 0.0;

                if (!(strcmp(r->corner, "top_left") == 0 ||
                      strcmp(r->corner, "top_right") == 0 ||
                      strcmp(r->corner, "bottom_left") == 0 ||
                      strcmp(r->corner, "bottom_right") == 0)) {
                    snprintf(error, error_len, "region '%s': unknown corner '%s'", r->id, r->corner);
                    return false;
                }

                for (int j = 0; j < i; ++j) {
                    const Region *sibling = &panel->regions[j];
                    const bool same_parent = strcmp(sibling->parent_region_id, r->parent_region_id) == 0;
                    const bool sibling_right = (strcmp(sibling->corner, "top_right") == 0 || strcmp(sibling->corner, "bottom_right") == 0);
                    const bool sibling_bottom = (strcmp(sibling->corner, "bottom_left") == 0 || strcmp(sibling->corner, "bottom_right") == 0);
                    if (!same_parent || !resolved[j]) {
                        continue;
                    }
                    if (sibling_right != right_corner || sibling_bottom != bottom_corner) {
                        continue;
                    }
                    if (strcmp(parent_flow, "horizontal") == 0) {
                        cursor += sibling->width + sibling->margin;
                    } else {
                        cursor += sibling->height + sibling->margin;
                    }
                }

                if (strcmp(parent_flow, "horizontal") == 0) {
                    if (right_corner) {
                        r->min.x = parent_max_x - r->margin - r->width - cursor;
                    } else {
                        r->min.x = parent_min_x + r->margin + cursor;
                    }
                    if (bottom_corner) {
                        r->min.y = parent_max_y - r->margin - r->height;
                    } else {
                        r->min.y = parent_min_y + r->margin;
                    }
                } else {
                    if (right_corner) {
                        r->min.x = parent_max_x - r->margin - r->width;
                    } else {
                        r->min.x = parent_min_x + r->margin;
                    }
                    if (bottom_corner) {
                        r->min.y = parent_max_y - r->margin - r->height - cursor;
                    } else {
                        r->min.y = parent_min_y + r->margin + cursor;
                    }
                }
            }

            r->max.x = r->min.x + r->width;
            r->max.y = r->min.y + r->height;

            resolved[i] = true;
            --unresolved;
            progress = true;
        }

        if (!progress) {
            break;
        }
    }

    if (unresolved > 0) {
        snprintf(error, error_len, "Could not resolve hierarchical regions (check parent chain or auto size dependencies)");
        return false;
    }
    return true;
}

static bool auto_layout_rails(Panel *panel, char *error, int error_len) {
    for (int i = 0; i < panel->rail_count; ++i) {
        Rail *r = &panel->rails[i];
        const Region *region;
        double inner_min_x;
        double inner_min_y;
        double inner_max_x;
        double inner_max_y;
        double usable_w;
        double usable_h;
        double len;

        if (r->has_explicit_line) {
            continue;
        }

        region = model_find_region_const(panel, r->region_id);
        if (!region) {
            snprintf(error, error_len, "rail '%s': unknown region '%s'", r->id, r->region_id);
            return false;
        }

        inner_min_x = region->min.x;
        inner_min_y = region->min.y;
        inner_max_x = region->max.x;
        inner_max_y = region->max.y;

        /* Wire ducts are treated as region borders; shrink usable bounds by edge-aligned ducts. */
        for (int di = 0; di < panel->duct_count; ++di) {
            const WireDuct *d = &panel->ducts[di];
            const double thickness = d->width_in;
            const double edge_take = thickness + d->gap;
            if (strcmp(d->region_id, region->id) != 0) {
                continue;
            }
            if (strcmp(d->align, "top") == 0) {
                inner_min_y += edge_take;
            } else if (strcmp(d->align, "bottom") == 0) {
                inner_max_y -= edge_take;
            } else if (strcmp(d->align, "left") == 0) {
                inner_min_x += edge_take;
            } else if (strcmp(d->align, "right") == 0) {
                inner_max_x -= edge_take;
            }
        }

        usable_w = (inner_max_x - inner_min_x) - (2.0 * r->margin);
        usable_h = (inner_max_y - inner_min_y) - (2.0 * r->margin);
        len = r->length > 0.0 ? r->length : ((r->orientation == 'V' || r->orientation == 'v') ? usable_h : usable_w);

        if (len <= 0.0) {
            snprintf(error, error_len, "rail '%s': computed length <= 0", r->id);
            return false;
        }

        if (r->orientation == 'V' || r->orientation == 'v') {
            double x = inner_min_x + r->margin;
            double y1 = inner_min_y + r->margin;
            if (strcmp(r->align, "right") == 0) {
                x = inner_max_x - r->margin;
            } else if (strcmp(r->align, "center") == 0) {
                x = (inner_min_x + inner_max_x) / 2.0;
            }
            x += r->offset;

            r->start.x = x;
            r->start.y = y1;
            r->end.x = x;
            r->end.y = y1 + len;
        } else {
            double y = inner_min_y + r->margin;
            double x1 = inner_min_x + r->margin;
            if (strcmp(r->align, "bottom") == 0) {
                y = inner_max_y - r->margin;
            } else if (strcmp(r->align, "center") == 0) {
                y = (inner_min_y + inner_max_y) / 2.0;
            }
            y += r->offset;

            r->start.x = x1;
            r->start.y = y;
            r->end.x = x1 + len;
            r->end.y = y;
        }
    }

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

static bool resolve_auto_rail_intersections(Panel *panel, char *error, int error_len) {
    (void)error;
    (void)error_len;
    for (int i = 0; i < panel->rail_count; ++i) {
        Rail *r = &panel->rails[i];
        const Region *region = model_find_region_const(panel, r->region_id);
        const bool vertical = (r->orientation == 'V' || r->orientation == 'v');
        const double step = r->margin > 0.0 ? r->margin : 0.25;
        const Vec2 orig_start = r->start;
        const Vec2 orig_end = r->end;
        int attempts = 0;
        bool resolved = false;

        if (r->has_explicit_line) {
            continue;
        }
        if (!region) {
            continue;
        }

        while (attempts < 64) {
            bool intersects = false;

            for (int j = 0; j < i; ++j) {
                const Rail *other = &panel->rails[j];
                if (strcmp(r->region_id, other->region_id) != 0) {
                    continue;
                }
                if (!segments_intersect(r->start, r->end, other->start, other->end)) {
                    continue;
                }
                intersects = true;
                break;
            }

            if (!intersects) {
                resolved = true;
                break;
            }

            if (vertical) {
                r->start.x += step;
                r->end.x += step;
            } else {
                r->start.y += step;
                r->end.y += step;
            }

            if (r->start.x < region->min.x || r->start.x > region->max.x ||
                r->end.x < region->min.x || r->end.x > region->max.x ||
                r->start.y < region->min.y || r->start.y > region->max.y ||
                r->end.y < region->min.y || r->end.y > region->max.y) {
                break;
            }
            ++attempts;
        }

        if (!resolved) {
            r->start = orig_start;
            r->end = orig_end;
        }
    }
    return true;
}

static void auto_layout_ducts(Panel *panel) {
    for (int ri = 0; ri < panel->region_count; ++ri) {
        const Region *region = &panel->regions[ri];
        double top_cursor = 0.0;
        double bottom_cursor = 0.0;
        double left_cursor = 0.0;
        double right_cursor = 0.0;

        for (int di = 0; di < panel->duct_count; ++di) {
            WireDuct *d = &panel->ducts[di];
            const bool vertical = (d->orientation == 'V' || d->orientation == 'v');
            double width_units;

            if (d->has_explicit_origin) {
                continue;
            }
            if (strcmp(d->region_id, region->id) != 0) {
                continue;
            }

            width_units = d->width_in;
            if (vertical) {
                if (d->length <= 0.0) {
                    d->length = (region->max.y - region->min.y) - (2.0 * d->margin);
                }
                if (strcmp(d->align, "right") == 0) {
                    d->origin.x = region->max.x - d->margin - width_units - right_cursor;
                    d->origin.y = region->min.y + d->margin;
                    right_cursor += width_units + d->gap;
                } else if (strcmp(d->align, "center") == 0) {
                    d->origin.x = ((region->min.x + region->max.x) - width_units) / 2.0;
                    d->origin.y = region->min.y + d->margin;
                } else {
                    d->origin.x = region->min.x + d->margin + left_cursor;
                    d->origin.y = region->min.y + d->margin;
                    left_cursor += width_units + d->gap;
                }
            } else {
                if (d->length <= 0.0) {
                    d->length = (region->max.x - region->min.x) - (2.0 * d->margin);
                }
                if (strcmp(d->align, "bottom") == 0) {
                    d->origin.x = region->min.x + d->margin;
                    d->origin.y = region->max.y - d->margin - width_units - bottom_cursor;
                    bottom_cursor += width_units + d->gap;
                } else if (strcmp(d->align, "center") == 0) {
                    d->origin.x = region->min.x + d->margin;
                    d->origin.y = ((region->min.y + region->max.y) - width_units) / 2.0;
                } else {
                    d->origin.x = region->min.x + d->margin;
                    d->origin.y = region->min.y + d->margin + top_cursor;
                    top_cursor += width_units + d->gap;
                }
            }
            d->has_explicit_origin = true;
        }
    }
}

static void auto_layout_devices_on_rails(const BlockLibrary *library, Panel *panel) {
    for (int ri = 0; ri < panel->rail_count; ++ri) {
        const Rail *rail = &panel->rails[ri];
        const bool vertical = (strcmp(rail->layout, "vertical") == 0);
        double cursor = 0.0;
        bool first = true;

        for (int di = 0; di < panel->device_count; ++di) {
            Device *d = &panel->devices[di];
            const BlockDef *block;
            double w;
            double h;
            double local_gap;
            double cross = 0.0;

            if (d->has_explicit_position || d->relation != DEVICE_REL_NONE) {
                continue;
            }
            if (strcmp(d->rail_id, rail->id) != 0) {
                continue;
            }

            block = model_find_block_const(library, d->block_id);
            w = (block && block->width > 0.0) ? block->width : 1.25;
            h = (block && block->height > 0.0) ? block->height : 0.75;
            if (rail->orientation == 'V' || rail->orientation == 'v') {
                const double t = w;
                w = h;
                h = t;
                d->rotation_deg = 90;
            } else {
                d->rotation_deg = 0;
            }
            local_gap = d->gap > 0.0 ? d->gap : 0.3;

            if (vertical) {
                if (strcmp(d->align, "left") == 0) {
                    cross = -(w / 2.0) - 0.3;
                } else if (strcmp(d->align, "right") == 0) {
                    cross = (w / 2.0) + 0.3;
                }

                if (!first) {
                    cursor += local_gap;
                }
                d->position.x = rail->start.x + cross;
                d->position.y = rail->start.y + cursor + (h / 2.0);
                cursor += h;
            } else {
                if (strcmp(d->align, "above") == 0) {
                    cross = -(h / 2.0) - 0.3;
                } else if (strcmp(d->align, "below") == 0) {
                    cross = (h / 2.0) + 0.3;
                }

                if (!first) {
                    cursor += local_gap;
                }
                d->position.x = rail->start.x + cursor + (w / 2.0);
                d->position.y = rail->start.y + cross;
                cursor += w;
            }

            d->has_explicit_position = true;
            first = false;
        }
    }
}

bool resolve_relative_devices(const BlockLibrary *library, Panel *panel, char *error, int error_len) {
    bool resolved[MAX_DEVICES];
    int unresolved_count = 0;

    if (!auto_layout_regions(panel, error, error_len)) {
        return false;
    }
    if (!auto_layout_rails(panel, error, error_len)) {
        return false;
    }
    if (!resolve_auto_rail_intersections(panel, error, error_len)) {
        return false;
    }
    auto_layout_ducts(panel);
    auto_layout_devices_on_rails(library, panel);
    assign_device_rotation_from_rails(panel);

    for (int i = 0; i < panel->device_count; ++i) {
        const Device *d = &panel->devices[i];
        resolved[i] = d->has_explicit_position || d->relation == DEVICE_REL_NONE;
        if (!resolved[i]) {
            ++unresolved_count;
        }
        if (d->relation == DEVICE_REL_NONE && !d->has_explicit_position) {
            snprintf(error, error_len, "device '%s': missing absolute x/y or resolvable rail placement", d->id);
            return false;
        }
    }

    for (int pass = 0; pass < panel->device_count && unresolved_count > 0; ++pass) {
        bool progress = false;

        for (int i = 0; i < panel->device_count; ++i) {
            Device *d = &panel->devices[i];
            const Device *anchor;
            int anchor_index = -1;
            const BlockDef *self_block;
            const BlockDef *anchor_block;
            Vec2 self_local;
            Vec2 target_local;
            Vec2 target_world;
            const char *self_connector = d->self_connector_id;
            const char *target_connector = d->target_connector_id;

            if (resolved[i]) {
                continue;
            }

            anchor = find_device_const(panel, d->of_device_id, &anchor_index);
            if (!anchor) {
                snprintf(error, error_len, "device '%s': relation target '%s' not found", d->id, d->of_device_id);
                return false;
            }
            if (!resolved[anchor_index]) {
                continue;
            }

            self_block = model_find_block_const(library, d->block_id);
            anchor_block = model_find_block_const(library, anchor->block_id);

            if (d->relation == DEVICE_REL_NEXT_TO) {
                if (self_connector[0] == '\0') self_connector = "LEFT";
                if (target_connector[0] == '\0') target_connector = "RIGHT";
            } else if (d->relation == DEVICE_REL_ABOVE) {
                if (self_connector[0] == '\0') self_connector = "BOTTOM";
                if (target_connector[0] == '\0') target_connector = "TOP";
            } else if (d->relation == DEVICE_REL_BELOW) {
                if (self_connector[0] == '\0') self_connector = "TOP";
                if (target_connector[0] == '\0') target_connector = "BOTTOM";
            }

            if (!connector_local(self_block, self_connector, &self_local)) {
                snprintf(error, error_len, "device '%s': unknown self_connector '%s'", d->id, self_connector);
                return false;
            }
            if (!connector_local(anchor_block, target_connector, &target_local)) {
                snprintf(error, error_len, "device '%s': unknown target_connector '%s'", d->id, target_connector);
                return false;
            }
            rotate_local_by_deg(&self_local, d->rotation_deg);
            rotate_local_by_deg(&target_local, anchor->rotation_deg);

            target_world.x = anchor->position.x + target_local.x;
            target_world.y = anchor->position.y + target_local.y;

            d->position.x = target_world.x - self_local.x;
            d->position.y = target_world.y - self_local.y;

            if (d->relation == DEVICE_REL_NEXT_TO) {
                d->position.x += d->gap;
            } else if (d->relation == DEVICE_REL_ABOVE) {
                d->position.y -= d->gap;
            } else if (d->relation == DEVICE_REL_BELOW) {
                d->position.y += d->gap;
            }

            d->has_explicit_position = true;
            resolved[i] = true;
            progress = true;
            --unresolved_count;
        }

        if (!progress) {
            break;
        }
    }

    if (unresolved_count > 0) {
        snprintf(error, error_len, "Could not resolve all relative device placements (cyclic or missing references)");
        return false;
    }

    return true;
}

bool layout_document(const AssetLibrary *library, Document *document, char *error, int error_len) {
    if (document->kind == DOCUMENT_KIND_PANEL) {
        if (!document->has_panel_model) {
            snprintf(error, error_len, "panel document is missing the legacy panel model");
            return false;
        }
        if (!resolve_relative_devices(&library->legacy, &document->panel, error, error_len)) {
            return false;
        }
        return model_project_panel_to_document(&document->panel, document, error, error_len);
    }

    for (int i = 0; i < document->object_count; ++i) {
        DocumentObject *object = &document->objects[i];
        const AssetDef *asset = 0;

        if (object->asset_id[0] != '\0') {
            asset = model_find_asset_const(library, object->asset_id);
        }
        if (object->size.x <= 0.0 && asset && asset->width > 0.0) {
            object->size.x = asset->width;
        }
        if (object->size.y <= 0.0 && asset && asset->height > 0.0) {
            object->size.y = asset->height;
        }
        if (!object->has_bounds && (object->size.x > 0.0 || object->size.y > 0.0)) {
            object->min.x = object->position.x;
            object->min.y = object->position.y;
            object->max.x = object->position.x + object->size.x;
            object->max.y = object->position.y + object->size.y;
            object->has_bounds = true;
        }
    }

    for (int i = 0; i < document->connection_count; ++i) {
        DocumentConnection *connection = &document->connections[i];
        const DocumentObject *from_object = model_find_document_object_const(document, connection->from_object_id);
        const DocumentObject *to_object = model_find_document_object_const(document, connection->to_object_id);
        Vec2 from_point;
        Vec2 to_point;
        if (!from_object || !to_object) {
            connection->point_count = 0;
            continue;
        }
        if (!document_connection_endpoint(library, document, from_object, connection->from_port_id, &from_point) ||
            !document_connection_endpoint(library, document, to_object, connection->to_port_id, &to_point)) {
            connection->point_count = 0;
            continue;
        }

        if (connection->kind == DOC_CONNECTION_LINK) {
            connection->point_count = 2;
            connection->points[0] = from_point;
            connection->points[1] = to_point;
        } else {
            const double mid_x = (from_point.x + to_point.x) / 2.0;
            connection->point_count = 4;
            connection->points[0] = from_point;
            connection->points[1].x = mid_x;
            connection->points[1].y = from_point.y;
            connection->points[2].x = mid_x;
            connection->points[2].y = to_point.y;
            connection->points[3] = to_point;
        }
    }

    return true;
}
