#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool starts_with(const char *line, const char *prefix) {
    return strncmp(line, prefix, strlen(prefix)) == 0;
}

static char *trim_left(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        ++s;
    }
    return s;
}

static void trim_right(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        --n;
    }
}

static bool extract_attr(const char *line, const char *key, char *out, int out_len) {
    char pattern[128];
    const char *start = line;
    const char *end;
    size_t len;

    snprintf(pattern, sizeof(pattern), "%s=\"", key);
    while ((start = strstr(start, pattern)) != 0) {
        const bool boundary_ok = (start == line) ||
                                 start[-1] == ' ' ||
                                 start[-1] == '\t' ||
                                 start[-1] == '<';
        if (boundary_ok) {
            break;
        }
        ++start;
    }
    if (!start) {
        return false;
    }

    start += strlen(pattern);
    end = strchr(start, '"');
    if (!end) {
        return false;
    }

    len = (size_t)(end - start);
    if ((int)len >= out_len) {
        len = (size_t)(out_len - 1);
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

static bool parse_double_attr(const char *line, const char *key, double *out) {
    char buf[64];
    if (!extract_attr(line, key, buf, (int)sizeof(buf))) {
        return false;
    }
    *out = strtod(buf, 0);
    return true;
}

static VoltageDomain parse_domain(const char *raw) {
    if (strcmp(raw, "AC") == 0) {
        return DOMAIN_AC;
    }
    if (strcmp(raw, "DC") == 0) {
        return DOMAIN_DC;
    }
    return DOMAIN_UNSPECIFIED;
}

static bool is_valid_voltage_tag(const char *tag) {
    return strcmp(tag, "AC480") == 0 ||
           strcmp(tag, "AC240") == 0 ||
           strcmp(tag, "AC120") == 0 ||
           strcmp(tag, "DC24") == 0 ||
           strcmp(tag, "DC12") == 0 ||
           strcmp(tag, "DC5") == 0 ||
           strcmp(tag, "DC3_3") == 0;
}

static bool parse_library_rect(
    const char *line,
    const char *tag_name,
    LibraryRect *out_rect,
    char *error,
    int error_len,
    int line_no
) {
    if (!extract_attr(line, "id", out_rect->id, (int)sizeof(out_rect->id)) ||
        !parse_double_attr(line, "min_x", &out_rect->min.x) ||
        !parse_double_attr(line, "min_y", &out_rect->min.y) ||
        !parse_double_attr(line, "max_x", &out_rect->max.x) ||
        !parse_double_attr(line, "max_y", &out_rect->max.y)) {
        snprintf(
            error,
            error_len,
            "line %d: <%s> requires id,min_x,min_y,max_x,max_y",
            line_no,
            tag_name
        );
        return false;
    }
    return true;
}

static bool panel_has_region_id(const Panel *panel, const char *id) {
    for (int i = 0; i < panel->region_count; ++i) {
        if (strcmp(panel->regions[i].id, id) == 0) {
            return true;
        }
    }
    return false;
}

static bool panel_has_rail_id(const Panel *panel, const char *id) {
    for (int i = 0; i < panel->rail_count; ++i) {
        if (strcmp(panel->rails[i].id, id) == 0) {
            return true;
        }
    }
    return false;
}

static bool panel_has_device_id(const Panel *panel, const char *id) {
    for (int i = 0; i < panel->device_count; ++i) {
        if (strcmp(panel->devices[i].id, id) == 0) {
            return true;
        }
    }
    return false;
}

bool parse_block_library(const char *path, BlockLibrary *library, char *error, int error_len) {
    FILE *f = fopen(path, "r");
    char line[1024];
    BlockDef *active_block = 0;
    int line_no = 0;

    if (!f) {
        snprintf(error, error_len, "Failed to open library file: %s", path);
        return false;
    }

    model_init_library(library);

    while (fgets(line, sizeof(line), f)) {
        char *p;
        trim_right(line);
        p = trim_left(line);
        ++line_no;

        if (*p == '\0' || *p == '#') {
            continue;
        }

        if (starts_with(p, "<block ")) {
            char id[MAX_ID_LEN + 1];
            char name[MAX_ID_LEN + 1];
            const bool self_closing = strstr(p, "/>") != 0;
            if (!extract_attr(p, "id", id, (int)sizeof(id))) {
                snprintf(error, error_len, "line %d: <block> missing id", line_no);
                fclose(f);
                return false;
            }
            if (library->block_count >= MAX_BLOCKS) {
                snprintf(error, error_len, "Too many blocks (max=%d)", MAX_BLOCKS);
                fclose(f);
                return false;
            }
            active_block = &library->blocks[library->block_count++];
            memset(active_block, 0, sizeof(*active_block));
            snprintf(active_block->id, sizeof(active_block->id), "%s", id);
            if (extract_attr(p, "name", name, (int)sizeof(name))) {
                snprintf(active_block->name, sizeof(active_block->name), "%s", name);
            } else {
                snprintf(active_block->name, sizeof(active_block->name), "%s", id);
            }
            if (!parse_double_attr(p, "width", &active_block->width) || active_block->width <= 0.0) {
                active_block->width = 1.25;
            }
            if (!parse_double_attr(p, "height", &active_block->height) || active_block->height <= 0.0) {
                active_block->height = 0.75;
            }
            if (self_closing) {
                active_block = 0;
            }
            continue;
        }

        if (starts_with(p, "<panel_vector ")) {
            LibraryRect *r;
            if (active_block) {
                snprintf(error, error_len, "line %d: <panel_vector> must be outside <block>", line_no);
                fclose(f);
                return false;
            }
            if (library->panel_vector_count >= MAX_REGIONS) {
                snprintf(error, error_len, "line %d: too many panel vectors", line_no);
                fclose(f);
                return false;
            }
            r = &library->panel_vectors[library->panel_vector_count++];
            memset(r, 0, sizeof(*r));
            if (!parse_library_rect(p, "panel_vector", r, error, error_len, line_no)) {
                fclose(f);
                return false;
            }
            continue;
        }

        if (starts_with(p, "<backplane_vector ")) {
            LibraryRect *r;
            if (active_block) {
                snprintf(error, error_len, "line %d: <backplane_vector> must be outside <block>", line_no);
                fclose(f);
                return false;
            }
            if (library->backplane_vector_count >= MAX_REGIONS) {
                snprintf(error, error_len, "line %d: too many backplane vectors", line_no);
                fclose(f);
                return false;
            }
            r = &library->backplane_vectors[library->backplane_vector_count++];
            memset(r, 0, sizeof(*r));
            if (!parse_library_rect(p, "backplane_vector", r, error, error_len, line_no)) {
                fclose(f);
                return false;
            }
            continue;
        }

        if (starts_with(p, "</block")) {
            active_block = 0;
            continue;
        }

        if (starts_with(p, "<V ")) {
            Connector *c;
            char sx[64], sy[64], dx[64], dy[64], voltage[32];

            if (!active_block) {
                snprintf(error, error_len, "line %d: <V> must be inside <block>", line_no);
                fclose(f);
                return false;
            }
            if (active_block->connector_count >= MAX_CONNECTORS) {
                snprintf(error, error_len, "line %d: too many connectors on block '%s'", line_no, active_block->id);
                fclose(f);
                return false;
            }

            c = &active_block->connectors[active_block->connector_count++];
            memset(c, 0, sizeof(*c));

            if (!extract_attr(p, "id", c->id, (int)sizeof(c->id)) ||
                !extract_attr(p, "V", voltage, (int)sizeof(voltage)) ||
                !extract_attr(p, "x", sx, (int)sizeof(sx)) ||
                !extract_attr(p, "y", sy, (int)sizeof(sy)) ||
                !extract_attr(p, "dx", dx, (int)sizeof(dx)) ||
                !extract_attr(p, "dy", dy, (int)sizeof(dy))) {
                snprintf(error, error_len, "line %d: V requires id,V,x,y,dx,dy", line_no);
                fclose(f);
                return false;
            }

            if (!is_valid_voltage_tag(voltage)) {
                snprintf(error, error_len, "line %d: invalid V tag '%s'", line_no, voltage);
                fclose(f);
                return false;
            }

            snprintf(c->voltage_tag, sizeof(c->voltage_tag), "%s", voltage);
            c->position.x = strtod(sx, 0);
            c->position.y = strtod(sy, 0);
            c->direction.x = strtod(dx, 0);
            c->direction.y = strtod(dy, 0);
            continue;
        }
    }

    if (active_block) {
        snprintf(error, error_len, "line %d: missing </block> before EOF", line_no);
        fclose(f);
        return false;
    }

    fclose(f);
    return true;
}

static bool library_has_rect_id(const LibraryRect *rects, int count, const char *id) {
    for (int i = 0; i < count; ++i) {
        if (strcmp(rects[i].id, id) == 0) {
            return true;
        }
    }
    return false;
}

static bool merge_library(const BlockLibrary *src, BlockLibrary *dst, char *error, int error_len);

static bool asset_library_has_id(const AssetLibrary *library, const char *id) {
    for (int i = 0; i < library->asset_count; ++i) {
        if (strcmp(library->assets[i].id, id) == 0) {
            return true;
        }
    }
    return false;
}

static bool merge_asset_library(const AssetLibrary *src, AssetLibrary *dst, char *error, int error_len) {
    for (int i = 0; i < src->asset_count; ++i) {
        if (asset_library_has_id(dst, src->assets[i].id)) {
            snprintf(error, error_len, "Duplicate asset id '%s' across library files", src->assets[i].id);
            return false;
        }
        if (dst->asset_count >= MAX_BLOCKS) {
            snprintf(error, error_len, "Too many assets after merge (max=%d)", MAX_BLOCKS);
            return false;
        }
        dst->assets[dst->asset_count++] = src->assets[i];
    }

    if (!merge_library(&src->legacy, &dst->legacy, error, error_len)) {
        return false;
    }

    return true;
}

static char *trim_token(char *s) {
    char *end;
    while (*s && isspace((unsigned char)*s)) {
        ++s;
    }
    if (*s == '\0') {
        return s;
    }
    end = s + strlen(s) - 1;
    while (end >= s && isspace((unsigned char)*end)) {
        *end = '\0';
        --end;
    }
    return s;
}

static bool merge_library(const BlockLibrary *src, BlockLibrary *dst, char *error, int error_len) {
    for (int i = 0; i < src->block_count; ++i) {
        const BlockDef *b = &src->blocks[i];
        if (model_find_block_const(dst, b->id)) {
            snprintf(error, error_len, "Duplicate block id '%s' across library files", b->id);
            return false;
        }
        if (dst->block_count >= MAX_BLOCKS) {
            snprintf(error, error_len, "Too many blocks after merge (max=%d)", MAX_BLOCKS);
            return false;
        }
        dst->blocks[dst->block_count++] = *b;
    }

    for (int i = 0; i < src->panel_vector_count; ++i) {
        const LibraryRect *r = &src->panel_vectors[i];
        if (library_has_rect_id(dst->panel_vectors, dst->panel_vector_count, r->id)) {
            snprintf(error, error_len, "Duplicate panel_vector id '%s' across library files", r->id);
            return false;
        }
        if (dst->panel_vector_count >= MAX_REGIONS) {
            snprintf(error, error_len, "Too many panel vectors after merge (max=%d)", MAX_REGIONS);
            return false;
        }
        dst->panel_vectors[dst->panel_vector_count++] = *r;
    }

    for (int i = 0; i < src->backplane_vector_count; ++i) {
        const LibraryRect *r = &src->backplane_vectors[i];
        if (library_has_rect_id(dst->backplane_vectors, dst->backplane_vector_count, r->id)) {
            snprintf(error, error_len, "Duplicate backplane_vector id '%s' across library files", r->id);
            return false;
        }
        if (dst->backplane_vector_count >= MAX_REGIONS) {
            snprintf(error, error_len, "Too many backplane vectors after merge (max=%d)", MAX_REGIONS);
            return false;
        }
        dst->backplane_vectors[dst->backplane_vector_count++] = *r;
    }

    return true;
}

static bool parse_generic_asset_library(const char *path, AssetLibrary *library, char *error, int error_len) {
    FILE *f = fopen(path, "r");
    char line[1024];
    int line_no = 0;
    AssetDef *active_asset = 0;

    if (!f) {
        snprintf(error, error_len, "Failed to open library file: %s", path);
        return false;
    }

    model_init_asset_library(library);

    while (fgets(line, sizeof(line), f)) {
        char *p;
        trim_right(line);
        p = trim_left(line);
        ++line_no;

        if (*p == '\0' || *p == '#') {
            continue;
        }

        if (starts_with(p, "<asset ") || starts_with(p, "<symbol ") || starts_with(p, "<assembly ")) {
            const bool self_closing = strstr(p, "/>") != 0;
            char kind_buf[32] = {0};
            char name_buf[MAX_ID_LEN + 1] = {0};

            if (library->asset_count >= MAX_BLOCKS) {
                snprintf(error, error_len, "line %d: too many assets", line_no);
                fclose(f);
                return false;
            }

            active_asset = &library->assets[library->asset_count];
            memset(active_asset, 0, sizeof(*active_asset));

            if (!extract_attr(p, "id", active_asset->id, (int)sizeof(active_asset->id))) {
                snprintf(error, error_len, "line %d: asset requires id", line_no);
                fclose(f);
                return false;
            }
            if (asset_library_has_id(library, active_asset->id)) {
                snprintf(error, error_len, "line %d: duplicate asset id '%s'", line_no, active_asset->id);
                fclose(f);
                return false;
            }

            if (starts_with(p, "<symbol ")) {
                snprintf(kind_buf, sizeof(kind_buf), "%s", "symbol");
            } else if (starts_with(p, "<assembly ")) {
                snprintf(kind_buf, sizeof(kind_buf), "%s", "assembly");
            } else {
                extract_attr(p, "kind", kind_buf, (int)sizeof(kind_buf));
            }

            if (strcmp(kind_buf, "assembly") == 0) {
                active_asset->kind = ASSET_KIND_ASSEMBLY;
            } else {
                active_asset->kind = ASSET_KIND_SYMBOL;
            }

            if (extract_attr(p, "name", name_buf, (int)sizeof(name_buf))) {
                snprintf(active_asset->name, sizeof(active_asset->name), "%s", name_buf);
            } else {
                snprintf(active_asset->name, sizeof(active_asset->name), "%s", active_asset->id);
            }

            parse_double_attr(p, "width", &active_asset->width);
            parse_double_attr(p, "height", &active_asset->height);

            ++library->asset_count;
            if (self_closing) {
                active_asset = 0;
            }
            continue;
        }

        if (starts_with(p, "</asset") || starts_with(p, "</symbol") || starts_with(p, "</assembly")) {
            active_asset = 0;
            continue;
        }

        if ((starts_with(p, "<port ") || starts_with(p, "<V ")) && active_asset) {
            Connector *c;
            char sx[64] = {0};
            char sy[64] = {0};
            char dx[64] = {0};
            char dy[64] = {0};
            char voltage[32] = {0};

            if (active_asset->connector_count >= MAX_CONNECTORS) {
                snprintf(error, error_len, "line %d: too many ports on asset '%s'", line_no, active_asset->id);
                fclose(f);
                return false;
            }

            c = &active_asset->connectors[active_asset->connector_count++];
            memset(c, 0, sizeof(*c));

            if (!extract_attr(p, "id", c->id, (int)sizeof(c->id))) {
                snprintf(error, error_len, "line %d: port requires id", line_no);
                fclose(f);
                return false;
            }
            if (!extract_attr(p, "x", sx, (int)sizeof(sx)) || !extract_attr(p, "y", sy, (int)sizeof(sy))) {
                snprintf(error, error_len, "line %d: port requires x,y", line_no);
                fclose(f);
                return false;
            }
            if (extract_attr(p, "dx", dx, (int)sizeof(dx))) {
                c->direction.x = strtod(dx, 0);
            }
            if (extract_attr(p, "dy", dy, (int)sizeof(dy))) {
                c->direction.y = strtod(dy, 0);
            }
            if (extract_attr(p, "voltage", voltage, (int)sizeof(voltage)) ||
                extract_attr(p, "V", voltage, (int)sizeof(voltage))) {
                snprintf(c->voltage_tag, sizeof(c->voltage_tag), "%s", voltage);
            }
            c->position.x = strtod(sx, 0);
            c->position.y = strtod(sy, 0);
            continue;
        }

        if (starts_with(p, "<member ") && active_asset) {
            char asset_id[MAX_ID_LEN + 1] = {0};
            if (active_asset->child_count >= MAX_CONNECTORS) {
                snprintf(error, error_len, "line %d: too many members on asset '%s'", line_no, active_asset->id);
                fclose(f);
                return false;
            }
            if (!extract_attr(p, "asset", asset_id, (int)sizeof(asset_id))) {
                snprintf(error, error_len, "line %d: member requires asset", line_no);
                fclose(f);
                return false;
            }
            snprintf(active_asset->child_asset_ids[active_asset->child_count],
                     sizeof(active_asset->child_asset_ids[active_asset->child_count]),
                     "%s",
                     asset_id);
            parse_double_attr(p, "x", &active_asset->child_positions[active_asset->child_count].x);
            parse_double_attr(p, "y", &active_asset->child_positions[active_asset->child_count].y);
            ++active_asset->child_count;
            continue;
        }
    }

    fclose(f);
    return true;
}

static bool file_looks_like_legacy_library(const char *path, char *error, int error_len) {
    FILE *f = fopen(path, "r");
    char line[1024];
    bool result = true;

    if (!f) {
        snprintf(error, error_len, "Failed to open library file: %s", path);
        return false;
    }

    while (fgets(line, sizeof(line), f)) {
        char *p;
        trim_right(line);
        p = trim_left(line);
        if (*p == '\0' || *p == '#') {
            continue;
        }
        if (starts_with(p, "<asset ") || starts_with(p, "<symbol ") || starts_with(p, "<assembly ")) {
            result = false;
            break;
        }
    }

    fclose(f);
    return result;
}

bool parse_block_library_list(const char *paths_csv, BlockLibrary *library, char *error, int error_len) {
    char paths[2048];
    char *cursor;
    bool saw_any = false;
    BlockLibrary *temp = 0;

    if (!paths_csv || paths_csv[0] == '\0') {
        snprintf(error, error_len, "No library path provided");
        return false;
    }
    if ((int)strlen(paths_csv) >= (int)sizeof(paths)) {
        snprintf(error, error_len, "Library path list is too long");
        return false;
    }

    temp = (BlockLibrary *)malloc(sizeof(*temp));
    if (!temp) {
        snprintf(error, error_len, "Out of memory while loading library list");
        return false;
    }

    model_init_library(library);
    snprintf(paths, sizeof(paths), "%s", paths_csv);
    cursor = paths;

    while (cursor) {
        char *next = strchr(cursor, ',');
        char local_err[256];
        char *token;

        if (next) {
            *next = '\0';
        }
        token = trim_token(cursor);
        if (token[0] != '\0') {
            saw_any = true;
            if (!parse_block_library(token, temp, local_err, (int)sizeof(local_err))) {
                snprintf(error, error_len, "%s (%s)", local_err, token);
                free(temp);
                return false;
            }
            if (!merge_library(temp, library, error, error_len)) {
                free(temp);
                return false;
            }
        }

        cursor = next ? next + 1 : 0;
    }

    free(temp);
    if (!saw_any) {
        snprintf(error, error_len, "No library path provided");
        return false;
    }
    return true;
}

bool parse_panel(const char *path, Panel *panel, char *error, int error_len) {
    FILE *f = fopen(path, "r");
    char line[1024];
    int line_no = 0;
    int region_stack[MAX_REGION_DEPTH];
    int region_sp = 0;
    char active_rail_id[MAX_ID_LEN + 1] = {0};
    bool in_rail = false;

    if (!f) {
        snprintf(error, error_len, "Failed to open panel file: %s", path);
        return false;
    }

    model_init_panel(panel);

    while (fgets(line, sizeof(line), f)) {
        char *p;
        trim_right(line);
        p = trim_left(line);
        ++line_no;

        if (*p == '\0' || *p == '#') {
            continue;
        }

        if (starts_with(p, "<panel ")) {
            extract_attr(p, "name", panel->name, (int)sizeof(panel->name));
            parse_double_attr(p, "width", &panel->panel_width);
            parse_double_attr(p, "height", &panel->panel_height);
            if (!parse_double_attr(p, "back_width", &panel->backplane_width)) {
                panel->backplane_width = panel->panel_width;
            }
            if (!parse_double_attr(p, "back_height", &panel->backplane_height)) {
                panel->backplane_height = panel->panel_height;
            }
            continue;
        }

        if (starts_with(p, "<region ")) {
            Region *r;
            const bool self_closing = strstr(p, "/>") != 0;
            char width_buf[32] = {0};
            char height_buf[32] = {0};
            char size_buf[16] = {0};

            if (panel->region_count >= MAX_REGIONS) {
                snprintf(error, error_len, "line %d: too many regions", line_no);
                fclose(f);
                return false;
            }
            if (!self_closing && region_sp >= MAX_REGION_DEPTH) {
                snprintf(error, error_len, "line %d: region nesting too deep", line_no);
                fclose(f);
                return false;
            }

            r = &panel->regions[panel->region_count];
            memset(r, 0, sizeof(*r));
            r->margin = 0.0;
            snprintf(r->corner, sizeof(r->corner), "%s", "top_left");
            snprintf(r->flow, sizeof(r->flow), "%s", "vertical");

            if (!extract_attr(p, "id", r->id, (int)sizeof(r->id))) {
                snprintf(error, error_len, "line %d: region requires id", line_no);
                fclose(f);
                return false;
            }
            if (panel_has_region_id(panel, r->id)) {
                snprintf(error, error_len, "line %d: duplicate region id '%s'", line_no, r->id);
                fclose(f);
                return false;
            }
            if (region_sp > 0) {
                snprintf(r->parent_region_id, sizeof(r->parent_region_id), "%s", panel->regions[region_stack[region_sp - 1]].id);
            }

            extract_attr(p, "corner", r->corner, (int)sizeof(r->corner));
            extract_attr(p, "flow", r->flow, (int)sizeof(r->flow));
            parse_double_attr(p, "margin", &r->margin);

            if (parse_double_attr(p, "min_x", &r->min.x) &&
                parse_double_attr(p, "min_y", &r->min.y) &&
                parse_double_attr(p, "max_x", &r->max.x) &&
                parse_double_attr(p, "max_y", &r->max.y)) {
                r->has_explicit_bounds = true;
                r->width = r->max.x - r->min.x;
                r->height = r->max.y - r->min.y;
            } else {
                extract_attr(p, "size", size_buf, (int)sizeof(size_buf));
                extract_attr(p, "width", width_buf, (int)sizeof(width_buf));
                extract_attr(p, "height", height_buf, (int)sizeof(height_buf));

                if (strcmp(size_buf, "auto") == 0 || strcmp(width_buf, "auto") == 0 || strcmp(height_buf, "auto") == 0) {
                    r->has_auto_size = true;
                } else {
                    if (!parse_double_attr(p, "width", &r->width) || !parse_double_attr(p, "height", &r->height)) {
                        snprintf(error, error_len, "line %d: region requires bounds or width/height (or size=\"auto\")", line_no);
                        fclose(f);
                        return false;
                    }
                }
            }

            if (!self_closing) {
                region_stack[region_sp++] = panel->region_count;
            }
            ++panel->region_count;
            continue;
        }

        if (starts_with(p, "</region")) {
            if (region_sp == 0) {
                snprintf(error, error_len, "line %d: unexpected </region>", line_no);
                fclose(f);
                return false;
            }
            --region_sp;
            continue;
        }

        if (starts_with(p, "</rail")) {
            if (!in_rail) {
                snprintf(error, error_len, "line %d: unexpected </rail>", line_no);
                fclose(f);
                return false;
            }
            in_rail = false;
            active_rail_id[0] = '\0';
            continue;
        }

        if (starts_with(p, "<wire_duct ")) {
            WireDuct *d;
            char region_id[MAX_ID_LEN + 1] = {0};
            char orientation[16] = {0};

            if (panel->duct_count >= MAX_DUCTS) {
                snprintf(error, error_len, "line %d: too many wire ducts", line_no);
                fclose(f);
                return false;
            }
            d = &panel->ducts[panel->duct_count++];
            memset(d, 0, sizeof(*d));
            d->margin = 0.25;
            d->gap = 0.25;
            snprintf(d->align, sizeof(d->align), "%s", "top");

            if (!extract_attr(p, "id", d->id, (int)sizeof(d->id)) ||
                !extract_attr(p, "orientation", orientation, (int)sizeof(orientation)) ||
                !parse_double_attr(p, "length", &d->length) ||
                !parse_double_attr(p, "width_in", &d->width_in)) {
                snprintf(error, error_len, "line %d: wire_duct requires id,orientation,length,width_in", line_no);
                fclose(f);
                return false;
            }
            if (parse_double_attr(p, "x", &d->origin.x) && parse_double_attr(p, "y", &d->origin.y)) {
                d->has_explicit_origin = true;
            }
            extract_attr(p, "align", d->align, (int)sizeof(d->align));
            parse_double_attr(p, "margin", &d->margin);
            parse_double_attr(p, "gap", &d->gap);

            d->orientation = orientation[0];
            if (region_sp > 0) {
                snprintf(d->region_id, sizeof(d->region_id), "%s", panel->regions[region_stack[region_sp - 1]].id);
            } else if (extract_attr(p, "region", region_id, (int)sizeof(region_id))) {
                snprintf(d->region_id, sizeof(d->region_id), "%s", region_id);
            } else {
                snprintf(error, error_len, "line %d: wire_duct must be nested in <region> or include region attribute", line_no);
                fclose(f);
                return false;
            }
            continue;
        }

        if (starts_with(p, "<rail ")) {
            Rail *r;
            char region_id[MAX_ID_LEN + 1] = {0};
            const bool self_closing = strstr(p, "/>") != 0;
            if (panel->rail_count >= MAX_RAILS) {
                snprintf(error, error_len, "line %d: too many rails", line_no);
                fclose(f);
                return false;
            }
            r = &panel->rails[panel->rail_count];
            memset(r, 0, sizeof(*r));
            r->orientation = 'H';
            r->offset = 0.0;
            r->length = 0.0;
            r->margin = 0.4;
            snprintf(r->align, sizeof(r->align), "%s", "top");
            snprintf(r->layout, sizeof(r->layout), "%s", "horizontal");

            if (!extract_attr(p, "id", r->id, (int)sizeof(r->id))) {
                snprintf(error, error_len, "line %d: rail requires id", line_no);
                fclose(f);
                return false;
            }
            if (panel_has_rail_id(panel, r->id)) {
                snprintf(error, error_len, "line %d: duplicate rail id '%s'", line_no, r->id);
                fclose(f);
                return false;
            }
            {
                char orient_buf[8] = {0};
                if (extract_attr(p, "orientation", orient_buf, (int)sizeof(orient_buf))) {
                    r->orientation = orient_buf[0];
                }
            }
            if (parse_double_attr(p, "x1", &r->start.x) &&
                parse_double_attr(p, "y1", &r->start.y) &&
                parse_double_attr(p, "x2", &r->end.x) &&
                parse_double_attr(p, "y2", &r->end.y)) {
                r->has_explicit_line = true;
                if (!(r->orientation == 'H' || r->orientation == 'h' || r->orientation == 'V' || r->orientation == 'v')) {
                    const double dx = r->end.x - r->start.x;
                    const double dy = r->end.y - r->start.y;
                    r->orientation = (dy > dx || dy < -dx) ? 'V' : 'H';
                }
                if (!extract_attr(p, "layout", r->layout, (int)sizeof(r->layout))) {
                    if (r->orientation == 'V' || r->orientation == 'v') {
                        snprintf(r->layout, sizeof(r->layout), "%s", "vertical");
                    } else {
                        snprintf(r->layout, sizeof(r->layout), "%s", "horizontal");
                    }
                }
            } else {
                extract_attr(p, "align", r->align, (int)sizeof(r->align));
                if (!extract_attr(p, "layout", r->layout, (int)sizeof(r->layout))) {
                    if (r->orientation == 'V' || r->orientation == 'v') {
                        snprintf(r->layout, sizeof(r->layout), "%s", "vertical");
                    } else {
                        snprintf(r->layout, sizeof(r->layout), "%s", "horizontal");
                    }
                }
                parse_double_attr(p, "offset", &r->offset);
                parse_double_attr(p, "length", &r->length);
                parse_double_attr(p, "margin", &r->margin);
            }

            if (region_sp > 0) {
                snprintf(r->region_id, sizeof(r->region_id), "%s", panel->regions[region_stack[region_sp - 1]].id);
            } else if (extract_attr(p, "region", region_id, (int)sizeof(region_id))) {
                snprintf(r->region_id, sizeof(r->region_id), "%s", region_id);
            } else {
                snprintf(error, error_len, "line %d: rail must be nested in <region> or include region attribute", line_no);
                fclose(f);
                return false;
            }
            if (!self_closing) {
                in_rail = true;
                snprintf(active_rail_id, sizeof(active_rail_id), "%s", r->id);
            }
            ++panel->rail_count;
            continue;
        }

        if (starts_with(p, "<device ")) {
            Device *d;
            char domain_buf[16] = {0};
            char ac_buf[16] = {0};
            char relation_buf[32] = {0};
            char gap_buf[16] = {0};
            char region_id[MAX_ID_LEN + 1] = {0};

            if (panel->device_count >= MAX_DEVICES) {
                snprintf(error, error_len, "line %d: too many devices", line_no);
                fclose(f);
                return false;
            }
            d = &panel->devices[panel->device_count];
            memset(d, 0, sizeof(*d));
            d->gap = 0.4;
            snprintf(d->align, sizeof(d->align), "%s", "center");

            if (!extract_attr(p, "id", d->id, (int)sizeof(d->id)) ||
                !extract_attr(p, "block", d->block_id, (int)sizeof(d->block_id)) ||
                !extract_attr(p, "domain", domain_buf, (int)sizeof(domain_buf))) {
                snprintf(error, error_len, "line %d: device requires id,block,domain", line_no);
                fclose(f);
                return false;
            }
            if (parse_double_attr(p, "Amax", &d->amax)) {
                d->has_amax = true;
            } else {
                d->amax = 0.0;
                d->has_amax = false;
            }
            if (panel_has_device_id(panel, d->id)) {
                snprintf(error, error_len, "line %d: duplicate device id '%s'", line_no, d->id);
                fclose(f);
                return false;
            }
            extract_attr(p, "align", d->align, (int)sizeof(d->align));

            if (in_rail) {
                snprintf(d->rail_id, sizeof(d->rail_id), "%s", active_rail_id);
            } else if (!extract_attr(p, "rail", d->rail_id, (int)sizeof(d->rail_id))) {
                d->rail_id[0] = '\0';
            }

            if (region_sp > 0) {
                snprintf(d->region_id, sizeof(d->region_id), "%s", panel->regions[region_stack[region_sp - 1]].id);
            } else if (extract_attr(p, "region", region_id, (int)sizeof(region_id))) {
                snprintf(d->region_id, sizeof(d->region_id), "%s", region_id);
            } else {
                snprintf(error, error_len, "line %d: device must be nested in <region> or include region attribute", line_no);
                fclose(f);
                return false;
            }

            if (parse_double_attr(p, "x", &d->position.x) && parse_double_attr(p, "y", &d->position.y)) {
                d->has_explicit_position = true;
            }

            if (extract_attr(p, "relation", relation_buf, (int)sizeof(relation_buf))) {
                if (strcmp(relation_buf, "next_to") == 0) {
                    d->relation = DEVICE_REL_NEXT_TO;
                } else if (strcmp(relation_buf, "above") == 0) {
                    d->relation = DEVICE_REL_ABOVE;
                } else if (strcmp(relation_buf, "below") == 0) {
                    d->relation = DEVICE_REL_BELOW;
                } else {
                    snprintf(error, error_len, "line %d: unknown relation '%s'", line_no, relation_buf);
                    fclose(f);
                    return false;
                }

                if (!extract_attr(p, "of", d->of_device_id, (int)sizeof(d->of_device_id))) {
                    snprintf(error, error_len, "line %d: relative device requires 'of' attribute", line_no);
                    fclose(f);
                    return false;
                }
                extract_attr(p, "self_connector", d->self_connector_id, (int)sizeof(d->self_connector_id));
                extract_attr(p, "target_connector", d->target_connector_id, (int)sizeof(d->target_connector_id));
                if (extract_attr(p, "gap", gap_buf, (int)sizeof(gap_buf))) {
                    d->gap = strtod(gap_buf, 0);
                }
            }

            d->domain = parse_domain(domain_buf);
            if (extract_attr(p, "ac_level", ac_buf, (int)sizeof(ac_buf))) {
                d->ac_level = (AcLevel)atoi(ac_buf);
            } else {
                d->ac_level = AC_LEVEL_NONE;
            }
            ++panel->device_count;
            continue;
        }
    }

    if (in_rail) {
        snprintf(error, error_len, "line %d: missing </rail> before EOF", line_no);
        fclose(f);
        return false;
    }
    if (region_sp != 0) {
        snprintf(error, error_len, "line %d: missing </region> before EOF", line_no);
        fclose(f);
        return false;
    }

    fclose(f);
    return true;
}

static DocumentObjectKind parse_document_object_kind(const char *raw) {
    if (!raw || raw[0] == '\0') {
        return DOC_OBJECT_SYMBOL;
    }
    if (strcmp(raw, "group") == 0) {
        return DOC_OBJECT_GROUP;
    }
    if (strcmp(raw, "guide") == 0) {
        return DOC_OBJECT_GUIDE;
    }
    if (strcmp(raw, "panel") == 0) {
        return DOC_OBJECT_PANEL;
    }
    return DOC_OBJECT_SYMBOL;
}

static DocumentConnectionKind parse_document_connection_kind(const char *raw) {
    if (!raw || raw[0] == '\0' || strcmp(raw, "wire") == 0) {
        return DOC_CONNECTION_WIRE;
    }
    if (strcmp(raw, "pipe") == 0) {
        return DOC_CONNECTION_PIPE;
    }
    if (strcmp(raw, "link") == 0) {
        return DOC_CONNECTION_LINK;
    }
    return DOC_CONNECTION_UNKNOWN;
}

static void split_endpoint_ref(const char *raw, char *object_id, int object_len, char *port_id, int port_len) {
    const char *dot;
    if (!raw) {
        object_id[0] = '\0';
        port_id[0] = '\0';
        return;
    }
    dot = strchr(raw, '.');
    if (!dot) {
        snprintf(object_id, object_len, "%s", raw);
        port_id[0] = '\0';
        return;
    }
    {
        const size_t object_n = (size_t)(dot - raw);
        const size_t clamped = object_n < (size_t)(object_len - 1) ? object_n : (size_t)(object_len - 1);
        memcpy(object_id, raw, clamped);
        object_id[clamped] = '\0';
    }
    snprintf(port_id, port_len, "%s", dot + 1);
}

static bool parse_generic_document(const char *path, Document *document, char *error, int error_len) {
    FILE *f = fopen(path, "r");
    char line[1024];
    int line_no = 0;
    char parent_stack[MAX_REGION_DEPTH][MAX_ID_LEN + 1];
    int parent_sp = 0;

    if (!f) {
        snprintf(error, error_len, "Failed to open document file: %s", path);
        return false;
    }

    model_init_document(document);

    while (fgets(line, sizeof(line), f)) {
        char *p;
        trim_right(line);
        p = trim_left(line);
        ++line_no;

        if (*p == '\0' || *p == '#') {
            continue;
        }

        if (starts_with(p, "<document ")) {
            char kind_buf[32] = {0};
            extract_attr(p, "name", document->name, (int)sizeof(document->name));
            extract_attr(p, "kind", kind_buf, (int)sizeof(kind_buf));
            document->kind = document_kind_from_str(kind_buf);
            parse_double_attr(p, "width", &document->width);
            parse_double_attr(p, "height", &document->height);
            if (document->kind == DOCUMENT_KIND_UNKNOWN) {
                snprintf(error, error_len, "line %d: document kind must be panel, one_line, pid, or ga", line_no);
                fclose(f);
                return false;
            }
            continue;
        }

        if (starts_with(p, "<group ") || starts_with(p, "<object ")) {
            const bool self_closing = strstr(p, "/>") != 0;
            const bool is_group_tag = starts_with(p, "<group ");
            DocumentObject *object;
            char kind_buf[32] = {0};
            char parent_buf[MAX_ID_LEN + 1] = {0};

            if (document->object_count >= MAX_DOCUMENT_OBJECTS) {
                snprintf(error, error_len, "line %d: too many document objects", line_no);
                fclose(f);
                return false;
            }

            object = &document->objects[document->object_count];
            memset(object, 0, sizeof(*object));
            if (!extract_attr(p, "id", object->id, (int)sizeof(object->id))) {
                snprintf(error, error_len, "line %d: object requires id", line_no);
                fclose(f);
                return false;
            }
            if (model_find_document_object_const(document, object->id)) {
                snprintf(error, error_len, "line %d: duplicate object id '%s'", line_no, object->id);
                fclose(f);
                return false;
            }
            ++document->object_count;

            if (is_group_tag) {
                object->kind = DOC_OBJECT_GROUP;
            } else {
                extract_attr(p, "kind", kind_buf, (int)sizeof(kind_buf));
                object->kind = parse_document_object_kind(kind_buf);
            }
            extract_attr(p, "asset", object->asset_id, (int)sizeof(object->asset_id));
            extract_attr(p, "role", object->role, (int)sizeof(object->role));
            if (extract_attr(p, "parent", parent_buf, (int)sizeof(parent_buf))) {
                snprintf(object->parent_id, sizeof(object->parent_id), "%s", parent_buf);
            } else if (parent_sp > 0) {
                snprintf(object->parent_id, sizeof(object->parent_id), "%s", parent_stack[parent_sp - 1]);
            }

            parse_double_attr(p, "x", &object->position.x);
            parse_double_attr(p, "y", &object->position.y);
            parse_double_attr(p, "width", &object->size.x);
            parse_double_attr(p, "height", &object->size.y);
            if (parse_double_attr(p, "min_x", &object->min.x) &&
                parse_double_attr(p, "min_y", &object->min.y) &&
                parse_double_attr(p, "max_x", &object->max.x) &&
                parse_double_attr(p, "max_y", &object->max.y)) {
                object->has_bounds = true;
            } else if (object->size.x > 0.0 || object->size.y > 0.0) {
                object->min = object->position;
                object->max.x = object->position.x + object->size.x;
                object->max.y = object->position.y + object->size.y;
                object->has_bounds = true;
            }

            if (!self_closing && object->kind == DOC_OBJECT_GROUP) {
                if (parent_sp >= MAX_REGION_DEPTH) {
                    snprintf(error, error_len, "line %d: object nesting too deep", line_no);
                    fclose(f);
                    return false;
                }
                snprintf(parent_stack[parent_sp++], sizeof(parent_stack[0]), "%s", object->id);
            }
            continue;
        }

        if (starts_with(p, "</group") || starts_with(p, "</object")) {
            if (parent_sp > 0) {
                --parent_sp;
            }
            continue;
        }

        if (starts_with(p, "<connection ")) {
            DocumentConnection *connection;
            char kind_buf[32] = {0};
            char from_buf[128] = {0};
            char to_buf[128] = {0};

            if (document->connection_count >= MAX_DOCUMENT_CONNECTIONS) {
                snprintf(error, error_len, "line %d: too many document connections", line_no);
                fclose(f);
                return false;
            }
            connection = &document->connections[document->connection_count];
            memset(connection, 0, sizeof(*connection));
            if (!extract_attr(p, "id", connection->id, (int)sizeof(connection->id)) ||
                !extract_attr(p, "from", from_buf, (int)sizeof(from_buf)) ||
                !extract_attr(p, "to", to_buf, (int)sizeof(to_buf))) {
                snprintf(error, error_len, "line %d: connection requires id,from,to", line_no);
                fclose(f);
                return false;
            }
            ++document->connection_count;
            extract_attr(p, "kind", kind_buf, (int)sizeof(kind_buf));
            connection->kind = parse_document_connection_kind(kind_buf);
            split_endpoint_ref(from_buf, connection->from_object_id, (int)sizeof(connection->from_object_id), connection->from_port_id, (int)sizeof(connection->from_port_id));
            split_endpoint_ref(to_buf, connection->to_object_id, (int)sizeof(connection->to_object_id), connection->to_port_id, (int)sizeof(connection->to_port_id));
            continue;
        }
    }

    if (parent_sp != 0) {
        snprintf(error, error_len, "line %d: missing </group> before EOF", line_no);
        fclose(f);
        return false;
    }

    fclose(f);
    return true;
}

bool parse_asset_library_list(const char *paths_csv, AssetLibrary *library, char *error, int error_len) {
    char paths[2048];
    char *cursor;
    bool saw_any = false;

    if (!paths_csv || paths_csv[0] == '\0') {
        snprintf(error, error_len, "No library path provided");
        return false;
    }
    if ((int)strlen(paths_csv) >= (int)sizeof(paths)) {
        snprintf(error, error_len, "Library path list is too long");
        return false;
    }

    model_init_asset_library(library);
    snprintf(paths, sizeof(paths), "%s", paths_csv);
    cursor = paths;

    while (cursor) {
        char *next = strchr(cursor, ',');
        char *token;
        bool use_legacy = true;

        if (next) {
            *next = '\0';
        }
        token = trim_token(cursor);
        if (token[0] != '\0') {
            AssetLibrary *temp_assets = 0;
            saw_any = true;
            error[0] = '\0';
            use_legacy = file_looks_like_legacy_library(token, error, error_len);
            if (error[0] != '\0') {
                return false;
            }
            temp_assets = (AssetLibrary *)malloc(sizeof(*temp_assets));
            if (!temp_assets) {
                snprintf(error, error_len, "Out of memory while loading asset library list");
                return false;
            }
            if (use_legacy) {
                BlockLibrary *temp_legacy = (BlockLibrary *)malloc(sizeof(*temp_legacy));
                if (!temp_legacy) {
                    free(temp_assets);
                    snprintf(error, error_len, "Out of memory while loading legacy library");
                    return false;
                }
                model_init_library(temp_legacy);
                if (!parse_block_library(token, temp_legacy, error, error_len)) {
                    free(temp_legacy);
                    free(temp_assets);
                    return false;
                }
                model_init_asset_library(temp_assets);
                model_project_block_library_to_asset_library(temp_legacy, temp_assets);
                free(temp_legacy);
            } else {
                if (!parse_generic_asset_library(token, temp_assets, error, error_len)) {
                    free(temp_assets);
                    return false;
                }
            }
            if (!merge_asset_library(temp_assets, library, error, error_len)) {
                free(temp_assets);
                return false;
            }
            free(temp_assets);
        }
        cursor = next ? next + 1 : 0;
    }

    if (!saw_any) {
        snprintf(error, error_len, "No library path provided");
        return false;
    }

    return true;
}

bool parse_document(const char *path, Document *document, char *error, int error_len) {
    FILE *f = fopen(path, "r");
    char line[1024];
    bool is_legacy_panel = false;

    if (!f) {
        snprintf(error, error_len, "Failed to open document file: %s", path);
        return false;
    }

    while (fgets(line, sizeof(line), f)) {
        char *p;
        trim_right(line);
        p = trim_left(line);
        if (*p == '\0' || *p == '#') {
            continue;
        }
        if (starts_with(p, "<panel ")) {
            is_legacy_panel = true;
        }
        break;
    }
    fclose(f);

    if (is_legacy_panel) {
        Panel *panel = (Panel *)malloc(sizeof(*panel));
        bool ok;
        if (!panel) {
            snprintf(error, error_len, "Out of memory while loading panel document");
            return false;
        }
        ok = parse_panel(path, panel, error, error_len);
        if (!ok) {
            free(panel);
            return false;
        }
        ok = model_project_panel_to_document(panel, document, error, error_len);
        free(panel);
        return ok;
    }

    return parse_generic_document(path, document, error, error_len);
}
