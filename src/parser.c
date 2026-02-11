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
    const char *start;
    const char *end;
    size_t len;

    snprintf(pattern, sizeof(pattern), "%s=\"", key);
    start = strstr(line, pattern);
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
            continue;
        }

        if (starts_with(p, "</block")) {
            active_block = 0;
            continue;
        }

        if (starts_with(p, "<connector ")) {
            Connector *c;
            char sx[64], sy[64], dx[64], dy[64];

            if (!active_block) {
                snprintf(error, error_len, "line %d: <connector> must be inside <block>", line_no);
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
                !extract_attr(p, "type", c->type, (int)sizeof(c->type)) ||
                !extract_attr(p, "x", sx, (int)sizeof(sx)) ||
                !extract_attr(p, "y", sy, (int)sizeof(sy)) ||
                !extract_attr(p, "dx", dx, (int)sizeof(dx)) ||
                !extract_attr(p, "dy", dy, (int)sizeof(dy))) {
                snprintf(error, error_len, "line %d: connector requires id,type,x,y,dx,dy", line_no);
                fclose(f);
                return false;
            }

            c->position.x = strtod(sx, 0);
            c->position.y = strtod(sy, 0);
            c->direction.x = strtod(dx, 0);
            c->direction.y = strtod(dy, 0);
            continue;
        }
    }

    fclose(f);
    return true;
}

bool parse_panel(const char *path, Panel *panel, char *error, int error_len) {
    FILE *f = fopen(path, "r");
    char line[1024];
    int line_no = 0;

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
            continue;
        }

        if (starts_with(p, "<region ")) {
            Region *r;
            if (panel->region_count >= MAX_REGIONS) {
                snprintf(error, error_len, "line %d: too many regions", line_no);
                fclose(f);
                return false;
            }
            r = &panel->regions[panel->region_count++];
            memset(r, 0, sizeof(*r));

            if (!extract_attr(p, "id", r->id, (int)sizeof(r->id)) ||
                !parse_double_attr(p, "min_x", &r->min.x) ||
                !parse_double_attr(p, "min_y", &r->min.y) ||
                !parse_double_attr(p, "max_x", &r->max.x) ||
                !parse_double_attr(p, "max_y", &r->max.y)) {
                snprintf(error, error_len, "line %d: region requires id,min_x,min_y,max_x,max_y", line_no);
                fclose(f);
                return false;
            }
            continue;
        }

        if (starts_with(p, "<rail ")) {
            Rail *r;
            if (panel->rail_count >= MAX_RAILS) {
                snprintf(error, error_len, "line %d: too many rails", line_no);
                fclose(f);
                return false;
            }
            r = &panel->rails[panel->rail_count++];
            memset(r, 0, sizeof(*r));

            if (!extract_attr(p, "id", r->id, (int)sizeof(r->id)) ||
                !extract_attr(p, "region", r->region_id, (int)sizeof(r->region_id)) ||
                !parse_double_attr(p, "x1", &r->start.x) ||
                !parse_double_attr(p, "y1", &r->start.y) ||
                !parse_double_attr(p, "x2", &r->end.x) ||
                !parse_double_attr(p, "y2", &r->end.y)) {
                snprintf(error, error_len, "line %d: rail requires id,region,x1,y1,x2,y2", line_no);
                fclose(f);
                return false;
            }
            continue;
        }

        if (starts_with(p, "<device ")) {
            Device *d;
            char domain_buf[16] = {0};
            char ac_buf[16] = {0};

            if (panel->device_count >= MAX_DEVICES) {
                snprintf(error, error_len, "line %d: too many devices", line_no);
                fclose(f);
                return false;
            }
            d = &panel->devices[panel->device_count++];
            memset(d, 0, sizeof(*d));

            if (!extract_attr(p, "id", d->id, (int)sizeof(d->id)) ||
                !extract_attr(p, "block", d->block_id, (int)sizeof(d->block_id)) ||
                !extract_attr(p, "region", d->region_id, (int)sizeof(d->region_id)) ||
                !extract_attr(p, "rail", d->rail_id, (int)sizeof(d->rail_id)) ||
                !parse_double_attr(p, "x", &d->position.x) ||
                !parse_double_attr(p, "y", &d->position.y) ||
                !extract_attr(p, "domain", domain_buf, (int)sizeof(domain_buf))) {
                snprintf(error, error_len, "line %d: device requires id,block,region,rail,x,y,domain", line_no);
                fclose(f);
                return false;
            }

            d->domain = parse_domain(domain_buf);
            if (extract_attr(p, "ac_level", ac_buf, (int)sizeof(ac_buf))) {
                d->ac_level = (AcLevel)atoi(ac_buf);
            } else {
                d->ac_level = AC_LEVEL_NONE;
            }
            continue;
        }
    }

    fclose(f);
    return true;
}
