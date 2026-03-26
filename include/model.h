#ifndef MODEL_H
#define MODEL_H

#include "types.h"
#include "geom.h"

typedef struct {
    char id[MAX_ID_LEN + 1];
    Vec2 position;
    Vec2 direction;
    char voltage_tag[MAX_ID_LEN + 1];
} Connector;

typedef struct {
    char id[MAX_ID_LEN + 1];
    char name[MAX_ID_LEN + 1];
    double width;
    double height;
    Connector connectors[MAX_CONNECTORS];
    int connector_count;
} BlockDef;

typedef struct {
    char id[MAX_ID_LEN + 1];
    char parent_region_id[MAX_ID_LEN + 1];
    Vec2 min;
    Vec2 max;
    bool has_explicit_bounds;
    bool has_auto_size;
    char corner[MAX_ID_LEN + 1];
    char flow[MAX_ID_LEN + 1];
    double width;
    double height;
    double margin;
} Region;

typedef struct {
    char id[MAX_ID_LEN + 1];
    Vec2 min;
    Vec2 max;
} LibraryRect;

typedef struct {
    char id[MAX_ID_LEN + 1];
    char region_id[MAX_ID_LEN + 1];
    Vec2 start;
    Vec2 end;
    bool has_explicit_line;
    char orientation;
    char align[MAX_ID_LEN + 1];
    char layout[MAX_ID_LEN + 1];
    double offset;
    double length;
    double margin;
} Rail;

typedef enum {
    DEVICE_REL_NONE = 0,
    DEVICE_REL_NEXT_TO,
    DEVICE_REL_ABOVE,
    DEVICE_REL_BELOW
} DeviceRelation;

typedef struct {
    char id[MAX_ID_LEN + 1];
    char block_id[MAX_ID_LEN + 1];
    char region_id[MAX_ID_LEN + 1];
    char rail_id[MAX_ID_LEN + 1];
    Vec2 position;
    bool has_explicit_position;
    DeviceRelation relation;
    char of_device_id[MAX_ID_LEN + 1];
    char self_connector_id[MAX_ID_LEN + 1];
    char target_connector_id[MAX_ID_LEN + 1];
    double gap;
    double amax;
    bool has_amax;
    char align[MAX_ID_LEN + 1];
    int rotation_deg;
    VoltageDomain domain;
    AcLevel ac_level;
} Device;

typedef struct {
    char id[MAX_ID_LEN + 1];
    char region_id[MAX_ID_LEN + 1];
    Vec2 origin;
    char orientation;
    double length;
    double width_in;
    bool has_explicit_origin;
    char align[MAX_ID_LEN + 1];
    double margin;
    double gap;
} WireDuct;

typedef struct {
    BlockDef blocks[MAX_BLOCKS];
    int block_count;
    LibraryRect panel_vectors[MAX_REGIONS];
    int panel_vector_count;
    LibraryRect backplane_vectors[MAX_REGIONS];
    int backplane_vector_count;
} BlockLibrary;

typedef struct {
    char name[MAX_ID_LEN + 1];
    double panel_width;
    double panel_height;
    double backplane_width;
    double backplane_height;
    Region regions[MAX_REGIONS];
    int region_count;
    WireDuct ducts[MAX_DUCTS];
    int duct_count;
    Rail rails[MAX_RAILS];
    int rail_count;
    Device devices[MAX_DEVICES];
    int device_count;
} Panel;

typedef enum {
    DOCUMENT_KIND_UNKNOWN = 0,
    DOCUMENT_KIND_PANEL,
    DOCUMENT_KIND_ONE_LINE,
    DOCUMENT_KIND_PID,
    DOCUMENT_KIND_GA
} DocumentKind;

typedef enum {
    ASSET_KIND_UNKNOWN = 0,
    ASSET_KIND_SYMBOL,
    ASSET_KIND_ASSEMBLY
} AssetKind;

typedef enum {
    DOC_OBJECT_UNKNOWN = 0,
    DOC_OBJECT_GROUP,
    DOC_OBJECT_SYMBOL,
    DOC_OBJECT_GUIDE,
    DOC_OBJECT_PANEL
} DocumentObjectKind;

typedef enum {
    DOC_CONNECTION_UNKNOWN = 0,
    DOC_CONNECTION_WIRE,
    DOC_CONNECTION_PIPE,
    DOC_CONNECTION_LINK
} DocumentConnectionKind;

typedef struct {
    char id[MAX_ID_LEN + 1];
    char name[MAX_ID_LEN + 1];
    AssetKind kind;
    double width;
    double height;
    Connector connectors[MAX_CONNECTORS];
    int connector_count;
    char child_asset_ids[MAX_CONNECTORS][MAX_ID_LEN + 1];
    Vec2 child_positions[MAX_CONNECTORS];
    int child_count;
} AssetDef;

typedef struct {
    char id[MAX_ID_LEN + 1];
    DocumentObjectKind kind;
    char parent_id[MAX_ID_LEN + 1];
    char asset_id[MAX_ID_LEN + 1];
    char role[MAX_ID_LEN + 1];
    Vec2 position;
    Vec2 size;
    Vec2 min;
    Vec2 max;
    bool has_bounds;
} DocumentObject;

typedef struct {
    char id[MAX_ID_LEN + 1];
    DocumentConnectionKind kind;
    char from_object_id[MAX_ID_LEN + 1];
    char from_port_id[MAX_ID_LEN + 1];
    char to_object_id[MAX_ID_LEN + 1];
    char to_port_id[MAX_ID_LEN + 1];
    Vec2 points[MAX_CONNECTION_POINTS];
    int point_count;
} DocumentConnection;

typedef struct {
    BlockLibrary legacy;
    AssetDef assets[MAX_BLOCKS];
    int asset_count;
} AssetLibrary;

typedef struct {
    DocumentKind kind;
    char name[MAX_ID_LEN + 1];
    double width;
    double height;
    DocumentObject objects[MAX_DOCUMENT_OBJECTS];
    int object_count;
    DocumentConnection connections[MAX_DOCUMENT_CONNECTIONS];
    int connection_count;
    Panel panel;
    bool has_panel_model;
} Document;

void model_init_library(BlockLibrary *library);
void model_init_panel(Panel *panel);
void model_init_asset_library(AssetLibrary *library);
void model_init_document(Document *document);

const BlockDef *model_find_block_const(const BlockLibrary *library, const char *id);
const Region *model_find_region_const(const Panel *panel, const char *id);
const Rail *model_find_rail_const(const Panel *panel, const char *id);
const AssetDef *model_find_asset_const(const AssetLibrary *library, const char *id);
const DocumentObject *model_find_document_object_const(const Document *document, const char *id);
const Connector *model_find_asset_connector_const(const AssetDef *asset, const char *id);

const char *document_kind_to_str(DocumentKind kind);
DocumentKind document_kind_from_str(const char *raw);
void model_project_block_library_to_asset_library(const BlockLibrary *source, AssetLibrary *dest);
bool model_project_panel_to_document(const Panel *panel, Document *document, char *error, int error_len);

#endif
