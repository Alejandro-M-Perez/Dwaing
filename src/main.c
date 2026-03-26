#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "layout.h"
#include "parser.h"
#include "rules.h"

static const char *domain_to_str(VoltageDomain d) {
    switch (d) {
        case DOMAIN_AC: return "AC";
        case DOMAIN_DC: return "DC";
        default: return "UNSPECIFIED";
    }
}

static int load_inputs(const char *library_path, const char *document_path, AssetLibrary *library, Document *document) {
    char err[256];

    if (!parse_asset_library_list(library_path, library, err, (int)sizeof(err))) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }
    if (!parse_document(document_path, document, err, (int)sizeof(err))) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }
    if (!layout_document(library, document, err, (int)sizeof(err))) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }
    return 0;
}

static int cmd_validate(const char *library_path, const char *document_path) {
    AssetLibrary *library = (AssetLibrary *)calloc(1, sizeof(*library));
    Document *document = (Document *)calloc(1, sizeof(*document));
    ValidationError *errors = (ValidationError *)calloc(1024, sizeof(*errors));
    int result = 2;

    if (!library || !document || !errors) {
        fprintf(stderr, "Error: out of memory\n");
        result = 1;
        goto cleanup;
    }

    if (load_inputs(library_path, document_path, library, document) != 0) {
        result = 1;
        goto cleanup;
    }

    {
        const int max_errors = 1024;
        const int error_count = validate_document(library, document, errors, max_errors);
        if (error_count == 0) {
            printf("VALID: %s '%s' passed all checks.\n", document_kind_to_str(document->kind), document->name[0] ? document->name : "(unnamed)");
            result = 0;
            goto cleanup;
        }

        printf("INVALID: found %d issue(s).\n", error_count);
        {
            const int shown = error_count > max_errors ? max_errors : error_count;
            for (int i = 0; i < shown; ++i) {
                printf("- %s\n", errors[i].message);
            }
            if (shown < error_count) {
                printf("- ... %d more issue(s) omitted ...\n", error_count - shown);
            }
        }
    }

cleanup:
    free(errors);
    free(document);
    free(library);
    return result;
}

static int cmd_summary(const char *library_path, const char *document_path) {
    AssetLibrary *library = (AssetLibrary *)calloc(1, sizeof(*library));
    Document *document = (Document *)calloc(1, sizeof(*document));
    int result = 0;

    if (!library || !document) {
        fprintf(stderr, "Error: out of memory\n");
        result = 1;
        goto cleanup;
    }

    if (load_inputs(library_path, document_path, library, document) != 0) {
        result = 1;
        goto cleanup;
    }

    printf("Document: %s\n", document->name[0] ? document->name : "(unnamed)");
    printf("Kind: %s\n", document_kind_to_str(document->kind));
    printf("Assets: %d\n", library->asset_count);
    printf("Objects: %d\n", document->object_count);
    printf("Connections: %d\n", document->connection_count);

    if (document->kind == DOCUMENT_KIND_PANEL && document->has_panel_model) {
        const Panel *panel = &document->panel;
        printf("Regions: %d\n", panel->region_count);
        printf("WireDucts: %d\n", panel->duct_count);
        printf("Rails: %d\n", panel->rail_count);
        printf("Devices: %d\n", panel->device_count);

        for (int i = 0; i < panel->region_count; ++i) {
            const Region *r = &panel->regions[i];
            printf("region=%s parent=%s bounds=(%.1f,%.1f)-(%.1f,%.1f)\n",
                   r->id,
                   r->parent_region_id[0] ? r->parent_region_id : "(root)",
                   r->min.x, r->min.y, r->max.x, r->max.y);
        }

        for (int i = 0; i < panel->device_count; ++i) {
            const Device *d = &panel->devices[i];
            printf("device=%s block=%s rail=%s domain=%s Amax=%.2f rot=%d",
                   d->id, d->block_id, d->rail_id, domain_to_str(d->domain), d->amax, d->rotation_deg);
            if (d->domain == DOMAIN_AC) {
                printf(" ac_level=%d", (int)d->ac_level);
            }
            printf(" pos=(%.1f,%.1f)\n", d->position.x, d->position.y);
        }
    } else {
        for (int i = 0; i < document->object_count; ++i) {
            const DocumentObject *object = &document->objects[i];
            printf("object=%s kind=%d parent=%s asset=%s pos=(%.1f,%.1f)\n",
                   object->id,
                   (int)object->kind,
                   object->parent_id[0] ? object->parent_id : "(root)",
                   object->asset_id[0] ? object->asset_id : "-",
                   object->position.x,
                   object->position.y);
        }
        for (int i = 0; i < document->connection_count; ++i) {
            const DocumentConnection *connection = &document->connections[i];
            printf("connection=%s from=%s.%s to=%s.%s points=%d\n",
                   connection->id,
                   connection->from_object_id,
                   connection->from_port_id[0] ? connection->from_port_id : "-",
                   connection->to_object_id,
                   connection->to_port_id[0] ? connection->to_port_id : "-",
                   connection->point_count);
        }
    }

cleanup:
    free(document);
    free(library);
    return result;
}

static void print_usage(const char *argv0) {
    printf("Usage:\n");
    printf("  %s validate <library.xml[,library2.xml,...]> <document.xml>\n", argv0);
    printf("  %s summary <library.xml[,library2.xml,...]> <document.xml>\n", argv0);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "validate") == 0) {
        return cmd_validate(argv[2], argv[3]);
    }
    if (strcmp(argv[1], "summary") == 0) {
        return cmd_summary(argv[2], argv[3]);
    }

    print_usage(argv[0]);
    return 1;
}
