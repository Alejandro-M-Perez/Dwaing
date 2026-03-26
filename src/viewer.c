#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <GL/gl.h>

#include "layout.h"
#include "parser.h"
#include "rules.h"

typedef struct {
    double x;
    double y;
    double w;
    double h;
} Box;

typedef struct {
    float r;
    float g;
    float b;
} Color3;

typedef enum {
    LAYER_PANEL = 0,
    LAYER_BACK_PLANE,
    LAYER_DIN_RAIL,
    LAYER_DEVICES,
    LAYER_WIRES,
    LAYER_COUNT
} LayerId;

typedef struct {
    const char *name;
    Color3 color;
    bool visible;
    Box button;
    bool button_pressed;
} LayerState;

typedef enum {
    SEL_NONE = 0,
    SEL_PANEL,
    SEL_REGION,
    SEL_RAIL,
    SEL_DEVICE,
    SEL_DUCT,
    SEL_DOCUMENT,
    SEL_OBJECT,
    SEL_CONNECTION
} SelectionType;

typedef struct {
    SelectionType type;
    int index;
    char id[MAX_ID_LEN + 1];
} Selection;

typedef struct {
    char key[32];
    char value[128];
    bool editable;
    Box box;
} PropertyRow;

static Document g_document;
static AssetLibrary g_library;
static bool g_have_document = false;
static bool g_have_library = false;
static int g_win_w = 1200;
static int g_win_h = 800;
static double g_zoom = 18.0;
static double g_pan_x = 90.0;
static double g_pan_y = 70.0;
static bool g_reset_btn_pressed = false;

static char g_panel_path[512] = "examples/panel.xml";
static char g_library_path[512] = "libraries/default/library.xml";

static Selection g_selection = {SEL_NONE, -1, {0}};

#define g_panel (g_document.panel)

#define MAX_PROP_ROWS 64
static PropertyRow g_props[MAX_PROP_ROWS];
static int g_prop_count = 0;
static int g_active_prop = -1;
static bool g_editing = false;
static char g_edit_buf[128];

static bool g_prop_error_active = false;
static char g_prop_error_uid[128];
static char g_prop_error_key[32];
static char g_prop_error_msg[256];

static const Box g_reset_button = {12.0, 12.0, 108.0, 28.0};

static LayerState g_layers[LAYER_COUNT] = {
    {"Panel",      {0.08f, 0.50f, 0.22f}, true, {132.0, 12.0, 94.0, 28.0}, false},
    {"BackPlane",  {0.72f, 0.72f, 0.76f}, true, {232.0, 12.0, 110.0, 28.0}, false},
    {"DIN Rail",   {0.62f, 0.40f, 0.10f}, true, {348.0, 12.0, 94.0, 28.0}, false},
    {"Devices",    {0.12f, 0.36f, 0.80f}, true, {448.0, 12.0, 94.0, 28.0}, false},
    {"Wires",      {0.80f, 0.22f, 0.14f}, true, {548.0, 12.0, 84.0, 28.0}, false}
};

static const unsigned char GLYPH_BLANK[7] = {0, 0, 0, 0, 0, 0, 0};
static const unsigned char GLYPH_UNKNOWN[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};

static Box properties_pane_box(void) {
    const double pane_w = 370.0;
    Box b;
    b.w = pane_w;
    b.x = (double)g_win_w - pane_w;
    if (b.x < 680.0) {
        b.x = 680.0;
    }
    b.y = 0.0;
    b.h = (double)g_win_h;
    b.w = (double)g_win_w - b.x;
    return b;
}

static bool starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
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
        const bool boundary_ok = (start == line) || start[-1] == ' ' || start[-1] == '\t' || start[-1] == '<';
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

static bool point_in_box(int px, int py, const Box *b) {
    const double x = (double)px;
    const double y = (double)py;
    return x >= b->x && x <= (b->x + b->w) && y >= b->y && y <= (b->y + b->h);
}

static void world_from_screen(int sx, int sy, double *wx, double *wy) {
    *wx = ((double)sx - g_pan_x) / g_zoom;
    *wy = ((double)sy - g_pan_y) / g_zoom;
}

static double point_segment_distance(double px, double py, Vec2 a, Vec2 b) {
    const double vx = b.x - a.x;
    const double vy = b.y - a.y;
    const double wx = px - a.x;
    const double wy = py - a.y;
    const double vv = vx * vx + vy * vy;
    double t;
    double cx;
    double cy;

    if (vv <= 1e-9) {
        const double dx = px - a.x;
        const double dy = py - a.y;
        return sqrt(dx * dx + dy * dy);
    }

    t = (wx * vx + wy * vy) / vv;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    cx = a.x + t * vx;
    cy = a.y + t * vy;
    {
        const double dx = px - cx;
        const double dy = py - cy;
        return sqrt(dx * dx + dy * dy);
    }
}

static void selection_uid(const Selection *sel, char *out, int out_len) {
    const char *type = "none";
    switch (sel->type) {
        case SEL_PANEL: type = "panel"; break;
        case SEL_REGION: type = "region"; break;
        case SEL_RAIL: type = "rail"; break;
        case SEL_DEVICE: type = "device"; break;
        case SEL_DUCT: type = "duct"; break;
        case SEL_DOCUMENT: type = "document"; break;
        case SEL_OBJECT: type = "object"; break;
        case SEL_CONNECTION: type = "connection"; break;
        default: break;
    }
    snprintf(out, out_len, "%s:%s", type, sel->id[0] ? sel->id : "(none)");
}

static bool panel_document_active(void) {
    return g_have_document && g_document.kind == DOCUMENT_KIND_PANEL && g_document.has_panel_model;
}

static bool document_object_world_origin(const DocumentObject *object, Vec2 *out) {
    Vec2 pos = object->position;
    const DocumentObject *cursor = object;

    while (cursor->parent_id[0] != '\0') {
        cursor = model_find_document_object_const(&g_document, cursor->parent_id);
        if (!cursor) {
            return false;
        }
        pos.x += cursor->position.x;
        pos.y += cursor->position.y;
    }

    *out = pos;
    return true;
}

static void document_object_size(const DocumentObject *object, double *w_out, double *h_out) {
    const AssetDef *asset = 0;
    double w = object->size.x;
    double h = object->size.y;

    if (object->asset_id[0] != '\0') {
        asset = model_find_asset_const(&g_library, object->asset_id);
    }
    if (w <= 0.0 && asset && asset->width > 0.0) {
        w = asset->width;
    }
    if (h <= 0.0 && asset && asset->height > 0.0) {
        h = asset->height;
    }
    if (w <= 0.0) {
        w = 1.0;
    }
    if (h <= 0.0) {
        h = 1.0;
    }

    *w_out = w;
    *h_out = h;
}

static bool document_object_world_box(const DocumentObject *object, Box *out) {
    Vec2 origin;
    double w;
    double h;

    if (!document_object_world_origin(object, &origin)) {
        return false;
    }
    document_object_size(object, &w, &h);
    out->x = origin.x;
    out->y = origin.y;
    out->w = w;
    out->h = h;
    return true;
}

static bool current_prop_has_error(const char *key) {
    char uid[128];
    if (!g_prop_error_active) {
        return false;
    }
    selection_uid(&g_selection, uid, (int)sizeof(uid));
    return strcmp(uid, g_prop_error_uid) == 0 && strcmp(key, g_prop_error_key) == 0;
}

static void clear_current_prop_error_if_match(const char *key) {
    char uid[128];
    if (!g_prop_error_active) {
        return;
    }
    selection_uid(&g_selection, uid, (int)sizeof(uid));
    if (strcmp(uid, g_prop_error_uid) == 0 && strcmp(key, g_prop_error_key) == 0) {
        g_prop_error_active = false;
        g_prop_error_uid[0] = '\0';
        g_prop_error_key[0] = '\0';
        g_prop_error_msg[0] = '\0';
    }
}

static void set_prop_error_for_current(const char *key, const char *msg) {
    selection_uid(&g_selection, g_prop_error_uid, (int)sizeof(g_prop_error_uid));
    snprintf(g_prop_error_key, sizeof(g_prop_error_key), "%s", key);
    snprintf(g_prop_error_msg, sizeof(g_prop_error_msg), "%.*s", (int)sizeof(g_prop_error_msg) - 1, msg);
    g_prop_error_active = true;
}

static void glyph_for_char(char c, unsigned char out[7]) {
    const char uc = (char)toupper((unsigned char)c);
    const unsigned char *rows = GLYPH_UNKNOWN;

    switch (uc) {
        case ' ': rows = GLYPH_BLANK; break;
        case '-': { static const unsigned char r[7] = {0,0,0,0x1F,0,0,0}; rows = r; } break;
        case '_': { static const unsigned char r[7] = {0,0,0,0,0,0,0x1F}; rows = r; } break;
        case '.': { static const unsigned char r[7] = {0,0,0,0,0,0x06,0x06}; rows = r; } break;
        case ':': { static const unsigned char r[7] = {0,0x06,0x06,0,0x06,0x06,0}; rows = r; } break;

        case '0': { static const unsigned char r[7] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}; rows = r; } break;
        case '1': { static const unsigned char r[7] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}; rows = r; } break;
        case '2': { static const unsigned char r[7] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}; rows = r; } break;
        case '3': { static const unsigned char r[7] = {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}; rows = r; } break;
        case '4': { static const unsigned char r[7] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}; rows = r; } break;
        case '5': { static const unsigned char r[7] = {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}; rows = r; } break;
        case '6': { static const unsigned char r[7] = {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}; rows = r; } break;
        case '7': { static const unsigned char r[7] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}; rows = r; } break;
        case '8': { static const unsigned char r[7] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}; rows = r; } break;
        case '9': { static const unsigned char r[7] = {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}; rows = r; } break;

        case 'A': { static const unsigned char r[7] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; rows = r; } break;
        case 'B': { static const unsigned char r[7] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; rows = r; } break;
        case 'C': { static const unsigned char r[7] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; rows = r; } break;
        case 'D': { static const unsigned char r[7] = {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}; rows = r; } break;
        case 'E': { static const unsigned char r[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; rows = r; } break;
        case 'F': { static const unsigned char r[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; rows = r; } break;
        case 'G': { static const unsigned char r[7] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}; rows = r; } break;
        case 'H': { static const unsigned char r[7] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; rows = r; } break;
        case 'I': { static const unsigned char r[7] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}; rows = r; } break;
        case 'J': { static const unsigned char r[7] = {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}; rows = r; } break;
        case 'K': { static const unsigned char r[7] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11}; rows = r; } break;
        case 'L': { static const unsigned char r[7] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; rows = r; } break;
        case 'M': { static const unsigned char r[7] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; rows = r; } break;
        case 'N': { static const unsigned char r[7] = {0x11,0x11,0x19,0x15,0x13,0x11,0x11}; rows = r; } break;
        case 'O': { static const unsigned char r[7] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; rows = r; } break;
        case 'P': { static const unsigned char r[7] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; rows = r; } break;
        case 'Q': { static const unsigned char r[7] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}; rows = r; } break;
        case 'R': { static const unsigned char r[7] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; rows = r; } break;
        case 'S': { static const unsigned char r[7] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; rows = r; } break;
        case 'T': { static const unsigned char r[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; rows = r; } break;
        case 'U': { static const unsigned char r[7] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; rows = r; } break;
        case 'V': { static const unsigned char r[7] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; rows = r; } break;
        case 'W': { static const unsigned char r[7] = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}; rows = r; } break;
        case 'X': { static const unsigned char r[7] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; rows = r; } break;
        case 'Y': { static const unsigned char r[7] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; rows = r; } break;
        case 'Z': { static const unsigned char r[7] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}; rows = r; } break;

        default: break;
    }

    for (int i = 0; i < 7; ++i) {
        out[i] = rows[i];
    }
}

static void draw_glyph(double x, double y, double scale, char c) {
    unsigned char rows[7];
    glyph_for_char(c, rows);

    glBegin(GL_QUADS);
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            const unsigned char mask = (unsigned char)(1u << (4 - col));
            if ((rows[row] & mask) == 0) {
                continue;
            }

            {
                const double x0 = x + (double)col * scale;
                const double y0 = y + (double)row * scale;
                const double x1 = x0 + scale;
                const double y1 = y0 + scale;
                glVertex2d(x0, y0);
                glVertex2d(x1, y0);
                glVertex2d(x1, y1);
                glVertex2d(x0, y1);
            }
        }
    }
    glEnd();
}

static void draw_text_fit(const char *text, double x, double y, double w, double h) {
    const size_t len = strlen(text);
    const double pad = 2.0;
    const double max_w = w - (2.0 * pad);
    const double max_h = h - (2.0 * pad);

    if (len == 0 || max_w <= 1.0 || max_h <= 1.0) {
        return;
    }

    {
        const double text_units_w = (double)(len * 6u - 1u);
        const double text_units_h = 7.0;
        double scale = max_w / text_units_w;
        const double scale_h = max_h / text_units_h;
        if (scale_h < scale) {
            scale = scale_h;
        }
        if (scale < 0.7) {
            return;
        }

        {
            const double draw_w = text_units_w * scale;
            const double draw_h = text_units_h * scale;
            const double start_x = x + ((w - draw_w) * 0.5);
            const double start_y = y + ((h - draw_h) * 0.5);
            for (size_t i = 0; i < len; ++i) {
                draw_glyph(start_x + (double)i * 6.0 * scale, start_y, scale, text[i]);
            }
        }
    }
}

static void draw_rect(double x, double y, double w, double h) {
    glBegin(GL_LINE_LOOP);
    glVertex2d(x, y);
    glVertex2d(x + w, y);
    glVertex2d(x + w, y + h);
    glVertex2d(x, y + h);
    glEnd();
}

static void draw_filled_rect(double x, double y, double w, double h) {
    glBegin(GL_QUADS);
    glVertex2d(x, y);
    glVertex2d(x + w, y);
    glVertex2d(x + w, y + h);
    glVertex2d(x, y + h);
    glEnd();
}

static void reset_view(void) {
    g_zoom = 18.0;
    g_pan_x = 90.0;
    g_pan_y = 70.0;
}

static void toggle_layer(LayerId layer) {
    if (layer >= 0 && layer < LAYER_COUNT) {
        g_layers[(int)layer].visible = !g_layers[(int)layer].visible;
    }
}

static void set_color(Color3 c) {
    glColor3f(c.r, c.g, c.b);
}

static bool load_all(const char *library_path, const char *document_path, AssetLibrary *library, Document *document, char *err, int err_len) {
    if (!parse_asset_library_list(library_path, library, err, err_len)) {
        return false;
    }
    if (!parse_document(document_path, document, err, err_len)) {
        return false;
    }
    if (!layout_document(library, document, err, err_len)) {
        return false;
    }
    return true;
}

static bool reload_models(char *err, int err_len) {
    AssetLibrary next_library;
    Document next_document;
    if (!load_all(g_library_path, g_panel_path, &next_library, &next_document, err, err_len)) {
        return false;
    }
    g_library = next_library;
    g_document = next_document;
    g_have_library = true;
    g_have_document = true;
    return true;
}

static void maybe_show_data_warnings(SDL_Window *window) {
    ValidationError warnings[64];
    char msg[4096];
    int count;
    int pos = 0;

    if (!g_have_document || !g_have_library) {
        return;
    }

    count = collect_document_warnings(&g_library, &g_document, warnings, (int)(sizeof(warnings) / sizeof(warnings[0])));
    if (count <= 0) {
        return;
    }

    pos += snprintf(msg + pos, sizeof(msg) - (size_t)pos, "Configuration warnings:\n");
    for (int i = 0; i < count && i < (int)(sizeof(warnings) / sizeof(warnings[0])) && pos < (int)sizeof(msg) - 2; ++i) {
        pos += snprintf(msg + pos, sizeof(msg) - (size_t)pos, "- %s\n", warnings[i].message);
    }

    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Dwaing Warnings", msg, window);
}

static void clear_props(void) {
    g_prop_count = 0;
    g_active_prop = -1;
    if (g_editing) {
        g_editing = false;
        SDL_StopTextInput();
    }
}

static void add_prop(const char *key, const char *value, bool editable) {
    if (g_prop_count >= MAX_PROP_ROWS) {
        return;
    }
    snprintf(g_props[g_prop_count].key, sizeof(g_props[g_prop_count].key), "%s", key);
    snprintf(g_props[g_prop_count].value, sizeof(g_props[g_prop_count].value), "%s", value);
    g_props[g_prop_count].editable = editable;
    g_props[g_prop_count].box = (Box){0,0,0,0};
    ++g_prop_count;
}

static const char *selection_type_name(SelectionType t) {
    switch (t) {
        case SEL_PANEL: return "panel";
        case SEL_REGION: return "region";
        case SEL_RAIL: return "rail";
        case SEL_DEVICE: return "device";
        case SEL_DUCT: return "wire_duct";
        case SEL_DOCUMENT: return "document";
        case SEL_OBJECT: return "object";
        case SEL_CONNECTION: return "connection";
        default: return "none";
    }
}

static void rebuild_properties(void) {
    char buf[128];
    clear_props();

    if (g_selection.type == SEL_NONE || !g_have_document) {
        add_prop("selection", "click object", false);
        add_prop("mode", "viewer is read-only", false);
        return;
    }

    add_prop("type", selection_type_name(g_selection.type), false);

    if (!panel_document_active()) {
        if (g_selection.type == SEL_DOCUMENT) {
            add_prop("name", g_document.name[0] ? g_document.name : "(unnamed)", false);
            add_prop("kind", document_kind_to_str(g_document.kind), false);
            snprintf(buf, sizeof(buf), "%.3f", g_document.width); add_prop("width", buf, false);
            snprintf(buf, sizeof(buf), "%.3f", g_document.height); add_prop("height", buf, false);
            snprintf(buf, sizeof(buf), "%d", g_document.object_count); add_prop("objects", buf, false);
            snprintf(buf, sizeof(buf), "%d", g_document.connection_count); add_prop("connections", buf, false);
            return;
        }

        if (g_selection.type == SEL_OBJECT && g_selection.index >= 0 && g_selection.index < g_document.object_count) {
            const DocumentObject *object = &g_document.objects[g_selection.index];
            Box box;
            document_object_world_box(object, &box);
            add_prop("id", object->id, false);
            add_prop("parent", object->parent_id[0] ? object->parent_id : "(root)", false);
            add_prop("asset", object->asset_id[0] ? object->asset_id : "-", false);
            add_prop("role", object->role[0] ? object->role : "-", false);
            snprintf(buf, sizeof(buf), "%d", (int)object->kind); add_prop("kind_code", buf, false);
            snprintf(buf, sizeof(buf), "%.3f", box.x); add_prop("x", buf, false);
            snprintf(buf, sizeof(buf), "%.3f", box.y); add_prop("y", buf, false);
            snprintf(buf, sizeof(buf), "%.3f", box.w); add_prop("width", buf, false);
            snprintf(buf, sizeof(buf), "%.3f", box.h); add_prop("height", buf, false);
            return;
        }

        if (g_selection.type == SEL_CONNECTION && g_selection.index >= 0 && g_selection.index < g_document.connection_count) {
            const DocumentConnection *connection = &g_document.connections[g_selection.index];
            add_prop("id", connection->id, false);
            snprintf(buf, sizeof(buf), "%d", (int)connection->kind); add_prop("kind_code", buf, false);
            add_prop("from", connection->from_object_id, false);
            add_prop("from_port", connection->from_port_id[0] ? connection->from_port_id : "(default)", false);
            add_prop("to", connection->to_object_id, false);
            add_prop("to_port", connection->to_port_id[0] ? connection->to_port_id : "(default)", false);
            snprintf(buf, sizeof(buf), "%d", connection->point_count); add_prop("points", buf, false);
            return;
        }
    }

    if (g_selection.type == SEL_PANEL) {
        int root_regions = 0;
        for (int i = 0; i < g_panel.region_count; ++i) {
            if (g_panel.regions[i].parent_region_id[0] == '\0') {
                ++root_regions;
            }
        }
        snprintf(buf, sizeof(buf), "%d", root_regions);
        add_prop("id", "panel", false);
        add_prop("children", buf, false);

        add_prop("name", g_panel.name, true);
        snprintf(buf, sizeof(buf), "%.3f", g_panel.panel_width); add_prop("width", buf, true);
        snprintf(buf, sizeof(buf), "%.3f", g_panel.panel_height); add_prop("height", buf, true);
        snprintf(buf, sizeof(buf), "%.3f", g_panel.backplane_width); add_prop("back_width", buf, true);
        snprintf(buf, sizeof(buf), "%.3f", g_panel.backplane_height); add_prop("back_height", buf, true);
        return;
    }

    if (g_selection.type == SEL_REGION && g_selection.index >= 0 && g_selection.index < g_panel.region_count) {
        const Region *r = &g_panel.regions[g_selection.index];
        int child_regions = 0;
        int child_rails = 0;
        int child_devices = 0;
        int child_ducts = 0;
        for (int i = 0; i < g_panel.region_count; ++i) if (strcmp(g_panel.regions[i].parent_region_id, r->id) == 0) ++child_regions;
        for (int i = 0; i < g_panel.rail_count; ++i) if (strcmp(g_panel.rails[i].region_id, r->id) == 0) ++child_rails;
        for (int i = 0; i < g_panel.device_count; ++i) if (strcmp(g_panel.devices[i].region_id, r->id) == 0) ++child_devices;
        for (int i = 0; i < g_panel.duct_count; ++i) if (strcmp(g_panel.ducts[i].region_id, r->id) == 0) ++child_ducts;

        add_prop("id", r->id, false);
        add_prop("parent", r->parent_region_id[0] ? r->parent_region_id : "panel", false);
        snprintf(buf, sizeof(buf), "R:%d Rail:%d Dev:%d Duct:%d", child_regions, child_rails, child_devices, child_ducts);
        add_prop("children", buf, false);

        add_prop("corner", r->corner, true);
        add_prop("flow", r->flow, true);
        snprintf(buf, sizeof(buf), "%.3f", r->margin); add_prop("margin", buf, true);

        if (r->has_explicit_bounds) {
            snprintf(buf, sizeof(buf), "%.3f", r->min.x); add_prop("min_x", buf, true);
            snprintf(buf, sizeof(buf), "%.3f", r->min.y); add_prop("min_y", buf, true);
            snprintf(buf, sizeof(buf), "%.3f", r->max.x); add_prop("max_x", buf, true);
            snprintf(buf, sizeof(buf), "%.3f", r->max.y); add_prop("max_y", buf, true);
        } else {
            snprintf(buf, sizeof(buf), "%.3f", r->width); add_prop("width", buf, true);
            snprintf(buf, sizeof(buf), "%.3f", r->height); add_prop("height", buf, true);
        }
        return;
    }

    if (g_selection.type == SEL_RAIL && g_selection.index >= 0 && g_selection.index < g_panel.rail_count) {
        const Rail *r = &g_panel.rails[g_selection.index];
        int child_devices = 0;
        char ori[2] = {r->orientation, '\0'};
        for (int i = 0; i < g_panel.device_count; ++i) if (strcmp(g_panel.devices[i].rail_id, r->id) == 0) ++child_devices;

        add_prop("id", r->id, false);
        add_prop("parent", r->region_id, false);
        snprintf(buf, sizeof(buf), "%d", child_devices); add_prop("children", buf, false);

        add_prop("orientation", ori, true);
        add_prop("layout", r->layout, true);
        if (r->has_explicit_line) {
            snprintf(buf, sizeof(buf), "%.3f", r->start.x); add_prop("x1", buf, true);
            snprintf(buf, sizeof(buf), "%.3f", r->start.y); add_prop("y1", buf, true);
            snprintf(buf, sizeof(buf), "%.3f", r->end.x); add_prop("x2", buf, true);
            snprintf(buf, sizeof(buf), "%.3f", r->end.y); add_prop("y2", buf, true);
        } else {
            add_prop("align", r->align, true);
            snprintf(buf, sizeof(buf), "%.3f", r->offset); add_prop("offset", buf, true);
            snprintf(buf, sizeof(buf), "%.3f", r->length); add_prop("length", buf, true);
            snprintf(buf, sizeof(buf), "%.3f", r->margin); add_prop("margin", buf, true);
        }
        return;
    }

    if (g_selection.type == SEL_DEVICE && g_selection.index >= 0 && g_selection.index < g_panel.device_count) {
        const Device *d = &g_panel.devices[g_selection.index];
        const BlockDef *block = model_find_block_const(&g_library.legacy, d->block_id);
        add_prop("id", d->id, false);
        add_prop("name", (block && block->name[0]) ? block->name : d->block_id, false);
        add_prop("parent_region", d->region_id, false);
        add_prop("parent_rail", d->rail_id, false);

        add_prop("block", d->block_id, true);
        add_prop("domain", d->domain == DOMAIN_AC ? "AC" : (d->domain == DOMAIN_DC ? "DC" : "UNSPECIFIED"), true);
        if (d->domain == DOMAIN_AC || d->ac_level != AC_LEVEL_NONE) {
            snprintf(buf, sizeof(buf), "%d", (int)d->ac_level); add_prop("ac_level", buf, true);
        }
        if (d->has_amax) {
            snprintf(buf, sizeof(buf), "%.3f", d->amax);
        } else {
            buf[0] = '\0';
        }
        add_prop("Amax", buf, true);
        add_prop("align", d->align, true);
        snprintf(buf, sizeof(buf), "%.3f", d->gap); add_prop("gap", buf, true);
        if (d->has_explicit_position) {
            snprintf(buf, sizeof(buf), "%.3f", d->position.x); add_prop("x", buf, true);
            snprintf(buf, sizeof(buf), "%.3f", d->position.y); add_prop("y", buf, true);
        }
        if (d->relation != DEVICE_REL_NONE || d->of_device_id[0]) {
            add_prop("relation", d->relation == DEVICE_REL_NEXT_TO ? "next_to" : (d->relation == DEVICE_REL_ABOVE ? "above" : (d->relation == DEVICE_REL_BELOW ? "below" : "")), true);
            add_prop("of", d->of_device_id, true);
            add_prop("self_connector", d->self_connector_id, true);
            add_prop("target_connector", d->target_connector_id, true);
        }
        return;
    }

    if (g_selection.type == SEL_DUCT && g_selection.index >= 0 && g_selection.index < g_panel.duct_count) {
        const WireDuct *d = &g_panel.ducts[g_selection.index];
        char ori[2] = {d->orientation, '\0'};
        add_prop("id", d->id, false);
        add_prop("parent", d->region_id, false);
        add_prop("orientation", ori, true);
        snprintf(buf, sizeof(buf), "%.3f", d->length); add_prop("length", buf, true);
        snprintf(buf, sizeof(buf), "%.3f", d->width_in); add_prop("width_in", buf, true);
        add_prop("align", d->align, true);
        snprintf(buf, sizeof(buf), "%.3f", d->margin); add_prop("margin", buf, true);
        snprintf(buf, sizeof(buf), "%.3f", d->gap); add_prop("gap", buf, true);
        if (d->has_explicit_origin) {
            snprintf(buf, sizeof(buf), "%.3f", d->origin.x); add_prop("x", buf, true);
            snprintf(buf, sizeof(buf), "%.3f", d->origin.y); add_prop("y", buf, true);
        }
        return;
    }
}

static void draw_grid(void) {
    glColor3f(0.90f, 0.90f, 0.90f);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (int x = -20; x <= 80; x += 1) {
        glVertex2i(x, -20);
        glVertex2i(x, 80);
    }
    for (int y = -20; y <= 80; y += 1) {
        glVertex2i(-20, y);
        glVertex2i(80, y);
    }
    glEnd();
}

static void draw_back_plane(void) {
    const Color3 c = g_layers[LAYER_BACK_PLANE].color;
    glColor3f(c.r, c.g, c.b);

    if (panel_document_active() && g_panel.backplane_width > 0.0 && g_panel.backplane_height > 0.0) {
        draw_filled_rect(0.0, 0.0, g_panel.backplane_width, g_panel.backplane_height);
        return;
    }
}

static void draw_panel_geometry(void) {
    set_color(g_layers[LAYER_PANEL].color);
    glLineWidth(2.0f);

    if (panel_document_active() && g_panel.panel_width > 0.0 && g_panel.panel_height > 0.0) {
        draw_rect(0.0, 0.0, g_panel.panel_width, g_panel.panel_height);
    }

    for (int i = 0; i < g_panel.region_count; ++i) {
        const Region *r = &g_panel.regions[i];
        draw_rect(r->min.x, r->min.y, r->max.x - r->min.x, r->max.y - r->min.y);
    }
}

static void draw_din_rails(void) {
    set_color(g_layers[LAYER_DIN_RAIL].color);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    for (int i = 0; i < g_panel.rail_count; ++i) {
        const Rail *r = &g_panel.rails[i];
        glVertex2d(r->start.x, r->start.y);
        glVertex2d(r->end.x, r->end.y);
    }
    glEnd();
}

static void draw_wire_ducts(void) {
    const Color3 c = {0.28f, 0.28f, 0.32f};
    set_color(c);

    for (int i = 0; i < g_panel.duct_count; ++i) {
        const WireDuct *d = &g_panel.ducts[i];
        double w = d->length;
        double h = d->width_in;
        double x = d->origin.x;
        double y = d->origin.y;

        if (d->orientation == 'V' || d->orientation == 'v') {
            w = d->width_in;
            h = d->length;
        }

        draw_filled_rect(x, y, w, h);
        glColor3f(0.16f, 0.16f, 0.19f);
        draw_rect(x, y, w, h);
        set_color(c);
    }
}

static void draw_devices(void) {
    set_color(g_layers[LAYER_DEVICES].color);
    glLineWidth(2.0f);
    for (int i = 0; i < g_panel.device_count; ++i) {
        const Device *d = &g_panel.devices[i];
        const BlockDef *block = g_have_library ? model_find_block_const(&g_library.legacy, d->block_id) : 0;
        const char *label = (block && block->name[0] != '\0') ? block->name : d->block_id;
        const double w = (block && block->width > 0.0) ? block->width : 1.25;
        const double h = (block && block->height > 0.0) ? block->height : 0.75;
        glPushMatrix();
        glTranslated(d->position.x, d->position.y, 0.0);
        glRotated((double)d->rotation_deg, 0.0, 0.0, 1.0);

        draw_rect(-(w / 2.0), -(h / 2.0), w, h);
        glColor3f(0.07f, 0.07f, 0.07f);
        draw_text_fit(label, -(w / 2.0), -(h / 2.0), w, h);
        glPopMatrix();

        set_color(g_layers[LAYER_DEVICES].color);
    }
}

static void draw_wires(void) {
    if (g_panel.device_count < 2) {
        return;
    }

    set_color(g_layers[LAYER_WIRES].color);
    glLineWidth(1.8f);

    for (int i = 0; i < g_panel.device_count; ++i) {
        const Device *a = &g_panel.devices[i];
        int next_index = -1;
        double best_dx = 1e12;

        for (int j = 0; j < g_panel.device_count; ++j) {
            const Device *b = &g_panel.devices[j];
            if (i == j) continue;
            if (strcmp(a->rail_id, b->rail_id) != 0) continue;
            if (b->position.x <= a->position.x) continue;
            if ((b->position.x - a->position.x) < best_dx) {
                best_dx = b->position.x - a->position.x;
                next_index = j;
            }
        }

        if (next_index >= 0) {
            const Device *b = &g_panel.devices[next_index];
            const double mid_x = (a->position.x + b->position.x) * 0.5;
            const double offset = 0.5;
            glBegin(GL_LINE_STRIP);
            glVertex2d(a->position.x, a->position.y);
            glVertex2d(mid_x, a->position.y - offset);
            glVertex2d(mid_x, b->position.y - offset);
            glVertex2d(b->position.x, b->position.y);
            glEnd();
        }
    }
}

static void draw_selection_highlight(void) {
    if (!panel_document_active() || g_selection.type == SEL_NONE) {
        return;
    }
    glColor3f(0.96f, 0.76f, 0.04f);
    glLineWidth(3.5f);

    if (g_selection.type == SEL_PANEL) {
        draw_rect(0.0, 0.0, g_panel.panel_width, g_panel.panel_height);
    } else if (g_selection.type == SEL_REGION && g_selection.index >= 0 && g_selection.index < g_panel.region_count) {
        const Region *r = &g_panel.regions[g_selection.index];
        draw_rect(r->min.x, r->min.y, r->max.x - r->min.x, r->max.y - r->min.y);
    } else if (g_selection.type == SEL_RAIL && g_selection.index >= 0 && g_selection.index < g_panel.rail_count) {
        const Rail *r = &g_panel.rails[g_selection.index];
        glBegin(GL_LINES);
        glVertex2d(r->start.x, r->start.y);
        glVertex2d(r->end.x, r->end.y);
        glEnd();
    } else if (g_selection.type == SEL_DUCT && g_selection.index >= 0 && g_selection.index < g_panel.duct_count) {
        const WireDuct *d = &g_panel.ducts[g_selection.index];
        if (d->orientation == 'V' || d->orientation == 'v') {
            draw_rect(d->origin.x, d->origin.y, d->width_in, d->length);
        } else {
            draw_rect(d->origin.x, d->origin.y, d->length, d->width_in);
        }
    } else if (g_selection.type == SEL_DEVICE && g_selection.index >= 0 && g_selection.index < g_panel.device_count) {
        const Device *d = &g_panel.devices[g_selection.index];
        const BlockDef *block = g_have_library ? model_find_block_const(&g_library.legacy, d->block_id) : 0;
        const double w0 = (block && block->width > 0.0) ? block->width : 1.25;
        const double h0 = (block && block->height > 0.0) ? block->height : 0.75;
        glPushMatrix();
        glTranslated(d->position.x, d->position.y, 0.0);
        glRotated((double)d->rotation_deg, 0.0, 0.0, 1.0);
        draw_rect(-(w0 / 2.0), -(h0 / 2.0), w0, h0);
        glPopMatrix();
    }
}

static void draw_generic_canvas(void) {
    set_color(g_layers[LAYER_PANEL].color);
    glLineWidth(2.0f);
    draw_rect(0.0, 0.0, g_document.width, g_document.height);
}

static void draw_generic_objects(void) {
    for (int i = 0; i < g_document.object_count; ++i) {
        const DocumentObject *object = &g_document.objects[i];
        Box box;
        const AssetDef *asset = object->asset_id[0] ? model_find_asset_const(&g_library, object->asset_id) : 0;
        const char *label = object->id;

        if (!document_object_world_box(object, &box)) {
            continue;
        }

        if (object->kind == DOC_OBJECT_GROUP || object->kind == DOC_OBJECT_PANEL) {
            if (!g_layers[LAYER_PANEL].visible) {
                continue;
            }
            glColor3f(0.10f, 0.45f, 0.22f);
            draw_rect(box.x, box.y, box.w, box.h);
        } else if (object->kind == DOC_OBJECT_GUIDE) {
            if (!g_layers[LAYER_DIN_RAIL].visible) {
                continue;
            }
            glColor3f(0.62f, 0.40f, 0.10f);
            if (box.w >= box.h) {
                glBegin(GL_LINES);
                glVertex2d(box.x, box.y + box.h / 2.0);
                glVertex2d(box.x + box.w, box.y + box.h / 2.0);
                glEnd();
            } else {
                glBegin(GL_LINES);
                glVertex2d(box.x + box.w / 2.0, box.y);
                glVertex2d(box.x + box.w / 2.0, box.y + box.h);
                glEnd();
            }
        } else {
            if (!g_layers[LAYER_DEVICES].visible) {
                continue;
            }
            glColor3f(0.12f, 0.36f, 0.80f);
            draw_rect(box.x, box.y, box.w, box.h);
            glColor3f(0.07f, 0.07f, 0.07f);
            if (asset && asset->name[0] != '\0') {
                label = asset->name;
            }
            draw_text_fit(label, box.x, box.y, box.w, box.h);
        }
    }
}

static void draw_generic_connections(void) {
    if (!g_layers[LAYER_WIRES].visible) {
        return;
    }
    glColor3f(0.80f, 0.22f, 0.14f);
    glLineWidth(1.8f);
    for (int i = 0; i < g_document.connection_count; ++i) {
        const DocumentConnection *connection = &g_document.connections[i];
        if (connection->point_count < 2) {
            continue;
        }
        glBegin(GL_LINE_STRIP);
        for (int p = 0; p < connection->point_count; ++p) {
            glVertex2d(connection->points[p].x, connection->points[p].y);
        }
        glEnd();
    }
}

static void draw_generic_selection_highlight(void) {
    if (!g_have_document || g_selection.type == SEL_NONE) {
        return;
    }
    glColor3f(0.96f, 0.76f, 0.04f);
    glLineWidth(3.0f);
    if (g_selection.type == SEL_DOCUMENT) {
        draw_rect(0.0, 0.0, g_document.width, g_document.height);
    } else if (g_selection.type == SEL_OBJECT && g_selection.index >= 0 && g_selection.index < g_document.object_count) {
        Box box;
        if (document_object_world_box(&g_document.objects[g_selection.index], &box)) {
            draw_rect(box.x, box.y, box.w, box.h);
        }
    } else if (g_selection.type == SEL_CONNECTION && g_selection.index >= 0 && g_selection.index < g_document.connection_count) {
        const DocumentConnection *connection = &g_document.connections[g_selection.index];
        if (connection->point_count >= 2) {
            glBegin(GL_LINE_STRIP);
            for (int p = 0; p < connection->point_count; ++p) {
                glVertex2d(connection->points[p].x, connection->points[p].y);
            }
            glEnd();
        }
    }
}

static void draw_ui_overlay(void) {
    Box pane = properties_pane_box();

    glLineWidth(1.5f);

    if (g_reset_btn_pressed) glColor3f(0.80f, 0.80f, 0.80f);
    else glColor3f(0.88f, 0.88f, 0.88f);
    draw_filled_rect(g_reset_button.x, g_reset_button.y, g_reset_button.w, g_reset_button.h);
    glColor3f(0.18f, 0.18f, 0.18f);
    draw_rect(g_reset_button.x, g_reset_button.y, g_reset_button.w, g_reset_button.h);
    glColor3f(0.08f, 0.08f, 0.08f);
    draw_text_fit("RESET", g_reset_button.x, g_reset_button.y, g_reset_button.w, g_reset_button.h);

    for (int i = 0; i < LAYER_COUNT; ++i) {
        char label[64];
        LayerState *layer = &g_layers[i];
        const Color3 c = layer->color;

        if (layer->button_pressed) glColor3f(0.78f, 0.78f, 0.78f);
        else glColor3f(0.90f, 0.90f, 0.90f);
        draw_filled_rect(layer->button.x, layer->button.y, layer->button.w, layer->button.h);

        glColor3f(c.r, c.g, c.b);
        glLineWidth(2.0f);
        draw_rect(layer->button.x, layer->button.y, layer->button.w, layer->button.h);

        snprintf(label, sizeof(label), "%s %s", layer->name, layer->visible ? "ON" : "OFF");
        glColor3f(0.08f, 0.08f, 0.08f);
        draw_text_fit(label, layer->button.x, layer->button.y, layer->button.w, layer->button.h);
    }

    glColor3f(0.95f, 0.95f, 0.97f);
    draw_filled_rect(pane.x, pane.y, pane.w, pane.h);
    glColor3f(0.70f, 0.70f, 0.74f);
    draw_rect(pane.x, pane.y, pane.w, pane.h);

    glColor3f(0.08f, 0.08f, 0.10f);
    draw_text_fit("PROPERTIES", pane.x + 8.0, pane.y + 8.0, pane.w - 16.0, 28.0);

    {
        double y = 44.0;
        const double row_h = 22.0;
        const double key_w = pane.w * 0.42;
        const double value_w = pane.w - key_w - 16.0;

        for (int i = 0; i < g_prop_count; ++i) {
            Box row = {pane.x + 8.0, y, pane.w - 16.0, row_h};
            Box key_box = {row.x, row.y, key_w, row_h};
            Box val_box = {row.x + key_w, row.y, value_w, row_h};
            const bool row_error = current_prop_has_error(g_props[i].key);

            g_props[i].box = row;

            if (row_error) glColor3f(1.00f, 0.83f, 0.60f);
            else glColor3f(0.98f, 0.98f, 0.99f);
            draw_filled_rect(row.x, row.y, row.w, row.h);

            if (i == g_active_prop && g_editing) {
                glColor3f(0.94f, 0.93f, 0.70f);
                draw_filled_rect(val_box.x, val_box.y, val_box.w, val_box.h);
            }

            glColor3f(0.82f, 0.82f, 0.85f);
            draw_rect(row.x, row.y, row.w, row.h);
            glColor3f(0.82f, 0.82f, 0.85f);
            draw_rect(val_box.x, val_box.y, val_box.w, val_box.h);

            glColor3f(0.10f, 0.10f, 0.12f);
            draw_text_fit(g_props[i].key, key_box.x + 2.0, key_box.y + 1.0, key_box.w - 4.0, key_box.h - 2.0);
            if (i == g_active_prop && g_editing) {
                draw_text_fit(g_edit_buf, val_box.x + 2.0, val_box.y + 1.0, val_box.w - 4.0, val_box.h - 2.0);
            } else {
                draw_text_fit(g_props[i].value, val_box.x + 2.0, val_box.y + 1.0, val_box.w - 4.0, val_box.h - 2.0);
            }

            y += row_h + 2.0;
            if (y > pane.h - 60.0) {
                break;
            }
        }
    }

    if (g_prop_error_active) {
        glColor3f(0.70f, 0.10f, 0.06f);
        draw_text_fit(g_prop_error_msg, pane.x + 8.0, pane.h - 66.0, pane.w - 16.0, 54.0);
    }
}

static void set_ortho(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)w, (double)h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void render_frame(void) {
    glClearColor(0.98f, 0.98f, 0.98f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glLoadIdentity();
    glTranslated(g_pan_x, g_pan_y, 0.0);
    glScaled(g_zoom, g_zoom, 1.0);

    draw_grid();
    if (panel_document_active()) {
        if (g_layers[LAYER_BACK_PLANE].visible) draw_back_plane();
        if (g_layers[LAYER_PANEL].visible) draw_panel_geometry();
        if (g_layers[LAYER_DIN_RAIL].visible) draw_din_rails();
        if (g_layers[LAYER_PANEL].visible) draw_wire_ducts();
        if (g_layers[LAYER_WIRES].visible) draw_wires();
        if (g_layers[LAYER_DEVICES].visible) draw_devices();
        draw_selection_highlight();
    } else if (g_have_document) {
        if (g_layers[LAYER_PANEL].visible) draw_generic_canvas();
        draw_generic_objects();
        draw_generic_connections();
        draw_generic_selection_highlight();
    }

    glLoadIdentity();
    draw_ui_overlay();
}

static bool read_file_lines(const char *path, char ***lines_out, int *count_out, char *error, int error_len) {
    FILE *f = fopen(path, "r");
    char **lines = 0;
    int count = 0;
    int cap = 0;
    char buf[2048];

    if (!f) {
        snprintf(error, error_len, "Failed to open file.");
        return false;
    }

    while (fgets(buf, sizeof(buf), f)) {
        char *copy;
        size_t len = strlen(buf);
        if (count >= cap) {
            int new_cap = cap == 0 ? 64 : cap * 2;
            char **next = (char **)realloc(lines, (size_t)new_cap * sizeof(char *));
            if (!next) {
                fclose(f);
                snprintf(error, error_len, "Out of memory");
                for (int i = 0; i < count; ++i) free(lines[i]);
                free(lines);
                return false;
            }
            lines = next;
            cap = new_cap;
        }
        copy = (char *)malloc(len + 1);
        if (!copy) {
            fclose(f);
            snprintf(error, error_len, "Out of memory");
            for (int i = 0; i < count; ++i) free(lines[i]);
            free(lines);
            return false;
        }
        memcpy(copy, buf, len + 1);
        lines[count++] = copy;
    }

    fclose(f);
    *lines_out = lines;
    *count_out = count;
    return true;
}

static void free_file_lines(char **lines, int count) {
    for (int i = 0; i < count; ++i) free(lines[i]);
    free(lines);
}

static bool write_file_lines(const char *path, char **lines, int count, char *error, int error_len) {
    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(error, error_len, "Failed to write file.");
        return false;
    }
    for (int i = 0; i < count; ++i) {
        fputs(lines[i], f);
    }
    fclose(f);
    return true;
}

static bool set_attr_on_line(const char *line, const char *key, const char *value, char *out, int out_len) {
    char pattern[128];
    const char *start = line;
    const char *found = 0;
    const char *endq = 0;
    snprintf(pattern, sizeof(pattern), "%s=\"", key);

    while ((start = strstr(start, pattern)) != 0) {
        const bool boundary_ok = (start == line) || start[-1] == ' ' || start[-1] == '\t' || start[-1] == '<';
        if (boundary_ok) {
            found = start;
            break;
        }
        ++start;
    }

    if (found) {
        const char *value_start = found + strlen(pattern);
        endq = strchr(value_start, '"');
        if (!endq) {
            return false;
        }
        snprintf(out, out_len, "%.*s%s%s", (int)(value_start - line), line, value, endq);
        return true;
    }

    {
        const char *insert = strstr(line, "/>");
        if (!insert) insert = strchr(line, '>');
        if (!insert) return false;
        snprintf(out, out_len, "%.*s %s=\"%s\"%s", (int)(insert - line), line, key, value, insert);
        return true;
    }
}

static bool update_selected_property_in_xml(const char *key, const char *value, char *error, int error_len) {
    char **lines = 0;
    int count = 0;
    int target = -1;
    const char *tag_prefix = 0;
    char line_out[4096];

    if (!panel_document_active()) {
        snprintf(error, error_len, "Property editing is currently only available for legacy panel documents.");
        return false;
    }

    if (!read_file_lines(g_panel_path, &lines, &count, error, error_len)) {
        return false;
    }

    if (g_selection.type == SEL_PANEL) {
        tag_prefix = "<panel ";
    } else if (g_selection.type == SEL_REGION) {
        tag_prefix = "<region ";
    } else if (g_selection.type == SEL_RAIL) {
        tag_prefix = "<rail ";
    } else if (g_selection.type == SEL_DEVICE) {
        tag_prefix = "<device ";
    } else if (g_selection.type == SEL_DUCT) {
        tag_prefix = "<wire_duct ";
    }

    if (!tag_prefix) {
        free_file_lines(lines, count);
        snprintf(error, error_len, "No editable selection");
        return false;
    }

    for (int i = 0; i < count; ++i) {
        char local[2048];
        char idbuf[MAX_ID_LEN + 1];
        char *p;

        snprintf(local, sizeof(local), "%s", lines[i]);
        trim_right(local);
        p = trim_left(local);
        if (!starts_with(p, tag_prefix)) continue;

        if (g_selection.type == SEL_PANEL) {
            target = i;
            break;
        }

        if (extract_attr(p, "id", idbuf, (int)sizeof(idbuf)) && strcmp(idbuf, g_selection.id) == 0) {
            target = i;
            break;
        }
    }

    if (target < 0) {
        free_file_lines(lines, count);
        snprintf(error, error_len, "Could not find selected object in panel.xml.");
        return false;
    }

    if (!set_attr_on_line(lines[target], key, value, line_out, (int)sizeof(line_out))) {
        free_file_lines(lines, count);
        snprintf(error, error_len, "Failed to update attribute '%s'", key);
        return false;
    }

    free(lines[target]);
    lines[target] = (char *)malloc(strlen(line_out) + 1);
    if (!lines[target]) {
        free_file_lines(lines, count);
        snprintf(error, error_len, "Out of memory");
        return false;
    }
    snprintf(lines[target], strlen(line_out) + 1, "%s", line_out);

    if (!write_file_lines(g_panel_path, lines, count, error, error_len)) {
        free_file_lines(lines, count);
        return false;
    }

    free_file_lines(lines, count);
    return true;
}

static void apply_active_property_edit(SDL_Window *window) {
    char err[512];
    if (g_active_prop < 0 || g_active_prop >= g_prop_count) {
        return;
    }
    if (!g_props[g_active_prop].editable) {
        return;
    }

    if (!update_selected_property_in_xml(g_props[g_active_prop].key, g_edit_buf, err, (int)sizeof(err))) {
        set_prop_error_for_current(g_props[g_active_prop].key, err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Update Failed", err, window);
        g_editing = false;
        SDL_StopTextInput();
        rebuild_properties();
        return;
    }

    if (!reload_models(err, (int)sizeof(err))) {
        set_prop_error_for_current(g_props[g_active_prop].key, err);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Refresh Failed", err, window);
        g_editing = false;
        SDL_StopTextInput();
        rebuild_properties();
        return;
    }

    clear_current_prop_error_if_match(g_props[g_active_prop].key);
    g_editing = false;
    SDL_StopTextInput();
    rebuild_properties();
}

static void start_edit_prop(int row) {
    if (!panel_document_active()) return;
    if (row < 0 || row >= g_prop_count) return;
    if (!g_props[row].editable) return;
    g_active_prop = row;
    snprintf(g_edit_buf, sizeof(g_edit_buf), "%s", g_props[row].value);
    g_editing = true;
    SDL_StartTextInput();
}

static void select_object(SelectionType type, int idx, const char *id) {
    g_selection.type = type;
    g_selection.index = idx;
    snprintf(g_selection.id, sizeof(g_selection.id), "%s", id ? id : "");
    rebuild_properties();
}

static void pick_object_at(int sx, int sy) {
    double wx;
    double wy;

    world_from_screen(sx, sy, &wx, &wy);

    if (!g_have_document) {
        select_object(SEL_NONE, -1, "");
        return;
    }

    if (!panel_document_active()) {
        for (int i = 0; i < g_document.connection_count; ++i) {
            const DocumentConnection *connection = &g_document.connections[i];
            for (int p = 0; p + 1 < connection->point_count; ++p) {
                if (point_segment_distance(wx, wy, connection->points[p], connection->points[p + 1]) <= 0.22) {
                    select_object(SEL_CONNECTION, i, connection->id);
                    return;
                }
            }
        }

        for (int i = g_document.object_count - 1; i >= 0; --i) {
            Box box;
            if (!document_object_world_box(&g_document.objects[i], &box)) {
                continue;
            }
            if (wx >= box.x && wx <= box.x + box.w && wy >= box.y && wy <= box.y + box.h) {
                select_object(SEL_OBJECT, i, g_document.objects[i].id);
                return;
            }
        }

        if (wx >= 0.0 && wx <= g_document.width && wy >= 0.0 && wy <= g_document.height) {
            select_object(SEL_DOCUMENT, -1, g_document.name);
            return;
        }

        select_object(SEL_NONE, -1, "");
        return;
    }

    for (int i = g_panel.device_count - 1; i >= 0; --i) {
        const Device *d = &g_panel.devices[i];
        const BlockDef *b = model_find_block_const(&g_library.legacy, d->block_id);
        double w = (b && b->width > 0.0) ? b->width : 1.25;
        double h = (b && b->height > 0.0) ? b->height : 0.75;
        if ((d->rotation_deg % 180) != 0) {
            const double t = w; w = h; h = t;
        }
        if (wx >= d->position.x - w / 2.0 && wx <= d->position.x + w / 2.0 &&
            wy >= d->position.y - h / 2.0 && wy <= d->position.y + h / 2.0) {
            select_object(SEL_DEVICE, i, d->id);
            return;
        }
    }

    for (int i = g_panel.duct_count - 1; i >= 0; --i) {
        const WireDuct *d = &g_panel.ducts[i];
        double w = (d->orientation == 'V' || d->orientation == 'v') ? d->width_in : d->length;
        double h = (d->orientation == 'V' || d->orientation == 'v') ? d->length : d->width_in;
        if (wx >= d->origin.x && wx <= d->origin.x + w && wy >= d->origin.y && wy <= d->origin.y + h) {
            select_object(SEL_DUCT, i, d->id);
            return;
        }
    }

    for (int i = g_panel.rail_count - 1; i >= 0; --i) {
        const Rail *r = &g_panel.rails[i];
        if (point_segment_distance(wx, wy, r->start, r->end) <= 0.22) {
            select_object(SEL_RAIL, i, r->id);
            return;
        }
    }

    {
        int best = -1;
        double best_area = 1e18;
        for (int i = 0; i < g_panel.region_count; ++i) {
            const Region *r = &g_panel.regions[i];
            if (wx >= r->min.x && wx <= r->max.x && wy >= r->min.y && wy <= r->max.y) {
                double area = (r->max.x - r->min.x) * (r->max.y - r->min.y);
                if (area < best_area) {
                    best_area = area;
                    best = i;
                }
            }
        }
        if (best >= 0) {
            select_object(SEL_REGION, best, g_panel.regions[best].id);
            return;
        }
    }

    if (wx >= 0.0 && wx <= g_panel.panel_width && wy >= 0.0 && wy <= g_panel.panel_height) {
        select_object(SEL_PANEL, -1, "panel");
        return;
    }

    select_object(SEL_NONE, -1, "");
}

int main(int argc, char **argv) {
    bool running = true;
    SDL_Window *window;
    SDL_GLContext gl_ctx;
    char err[512];

    if (argc >= 2) {
        snprintf(g_panel_path, sizeof(g_panel_path), "%s", argv[1]);
    }
    if (argc >= 3) {
        snprintf(g_library_path, sizeof(g_library_path), "%s", argv[2]);
    }

    if (!reload_models(err, (int)sizeof(err))) {
        fprintf(stderr, "warning: load failed: %s\n", err);
        model_init_document(&g_document);
        model_init_asset_library(&g_library);
        g_have_document = false;
        g_have_library = false;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    window = SDL_CreateWindow(
        "Dwaing Viewer (OpenGL MVP)",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        g_win_w,
        g_win_h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(1);
    set_ortho(g_win_w, g_win_h);
    maybe_show_data_warnings(window);
    rebuild_properties();

    printf("Viewer controls: click object to inspect, click value to edit, Enter apply, Esc cancel\n");
    printf("Pan/zoom: wheel and arrows, r reset, q/ESC quit\n");
    printf("Document: %s | Library: %s\n", g_panel_path, g_library_path);

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                g_win_w = event.window.data1;
                g_win_h = event.window.data2;
                set_ortho(g_win_w, g_win_h);
            } else if (event.type == SDL_MOUSEWHEEL) {
                if (event.wheel.y > 0) {
                    g_zoom *= 1.08;
                } else if (event.wheel.y < 0) {
                    g_zoom /= 1.08;
                    if (g_zoom < 0.1) g_zoom = 0.1;
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                if (point_in_box(event.button.x, event.button.y, &g_reset_button)) {
                    g_reset_btn_pressed = true;
                }
                for (int i = 0; i < LAYER_COUNT; ++i) {
                    if (point_in_box(event.button.x, event.button.y, &g_layers[i].button)) {
                        g_layers[i].button_pressed = true;
                    }
                }
            } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                bool consumed = false;
                const bool reset_was_pressed = g_reset_btn_pressed;
                Box pane = properties_pane_box();
                g_reset_btn_pressed = false;

                if (reset_was_pressed && point_in_box(event.button.x, event.button.y, &g_reset_button)) {
                    reset_view();
                    consumed = true;
                }

                for (int i = 0; i < LAYER_COUNT; ++i) {
                    const bool was_pressed = g_layers[i].button_pressed;
                    g_layers[i].button_pressed = false;
                    if (was_pressed && point_in_box(event.button.x, event.button.y, &g_layers[i].button)) {
                        toggle_layer((LayerId)i);
                        consumed = true;
                    }
                }

                if (!consumed && point_in_box(event.button.x, event.button.y, &pane)) {
                    for (int i = 0; i < g_prop_count; ++i) {
                        if (point_in_box(event.button.x, event.button.y, &g_props[i].box)) {
                            start_edit_prop(i);
                            consumed = true;
                            break;
                        }
                    }
                }

                if (!consumed) {
                    if (g_editing) {
                        g_editing = false;
                        SDL_StopTextInput();
                    }
                    pick_object_at(event.button.x, event.button.y);
                }
            } else if (event.type == SDL_TEXTINPUT) {
                if (g_editing) {
                    size_t cur = strlen(g_edit_buf);
                    size_t add = strlen(event.text.text);
                    if (cur + add < sizeof(g_edit_buf) - 1) {
                        strcat(g_edit_buf, event.text.text);
                    }
                }
            } else if (event.type == SDL_KEYDOWN) {
                if (g_editing) {
                    if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) {
                        apply_active_property_edit(window);
                    } else if (event.key.keysym.sym == SDLK_ESCAPE) {
                        g_editing = false;
                        SDL_StopTextInput();
                        rebuild_properties();
                    } else if (event.key.keysym.sym == SDLK_BACKSPACE) {
                        size_t n = strlen(g_edit_buf);
                        if (n > 0) g_edit_buf[n - 1] = '\0';
                    }
                    continue;
                }

                switch (event.key.keysym.sym) {
                    case SDLK_PLUS:
                    case SDLK_EQUALS:
                    case SDLK_KP_PLUS:
                        g_zoom *= 1.10;
                        break;
                    case SDLK_MINUS:
                    case SDLK_KP_MINUS:
                        g_zoom /= 1.10;
                        if (g_zoom < 0.1) g_zoom = 0.1;
                        break;
                    case SDLK_LEFT:
                        g_pan_x += 20.0;
                        break;
                    case SDLK_RIGHT:
                        g_pan_x -= 20.0;
                        break;
                    case SDLK_UP:
                        g_pan_y += 20.0;
                        break;
                    case SDLK_DOWN:
                        g_pan_y -= 20.0;
                        break;
                    case SDLK_r:
                        reset_view();
                        break;
                    case SDLK_1:
                        toggle_layer(LAYER_PANEL);
                        break;
                    case SDLK_2:
                        toggle_layer(LAYER_BACK_PLANE);
                        break;
                    case SDLK_3:
                        toggle_layer(LAYER_DIN_RAIL);
                        break;
                    case SDLK_4:
                        toggle_layer(LAYER_DEVICES);
                        break;
                    case SDLK_5:
                        toggle_layer(LAYER_WIRES);
                        break;
                    case SDLK_ESCAPE:
                    case SDLK_q:
                        running = false;
                        break;
                    default:
                        break;
                }
            }
        }

        render_frame();
        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }

    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
