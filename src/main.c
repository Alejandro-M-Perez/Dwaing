#include <stdio.h>
#include <string.h>

#include "parser.h"
#include "rules.h"

static const char *domain_to_str(VoltageDomain d) {
    switch (d) {
        case DOMAIN_AC: return "AC";
        case DOMAIN_DC: return "DC";
        default: return "UNSPECIFIED";
    }
}

static int load_inputs(const char *library_path, const char *panel_path, BlockLibrary *library, Panel *panel) {
    char err[256];

    if (!parse_block_library(library_path, library, err, (int)sizeof(err))) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }
    if (!parse_panel(panel_path, panel, err, (int)sizeof(err))) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }
    return 0;
}

static int cmd_validate(const char *library_path, const char *panel_path) {
    BlockLibrary library;
    Panel panel;
    ValidationError errors[1024];

    if (load_inputs(library_path, panel_path, &library, &panel) != 0) {
        return 1;
    }

    int error_count = validate_panel(&library, &panel, errors, (int)(sizeof(errors) / sizeof(errors[0])));
    if (error_count == 0) {
        printf("VALID: panel '%s' passed all checks.\n", panel.name[0] ? panel.name : "(unnamed)");
        return 0;
    }

    printf("INVALID: found %d issue(s).\n", error_count);
    const int shown = error_count > (int)(sizeof(errors) / sizeof(errors[0])) ? (int)(sizeof(errors) / sizeof(errors[0])) : error_count;
    for (int i = 0; i < shown; ++i) {
        printf("- %s\n", errors[i].message);
    }
    if (shown < error_count) {
        printf("- ... %d more issue(s) omitted ...\n", error_count - shown);
    }

    return 2;
}

static int cmd_summary(const char *library_path, const char *panel_path) {
    BlockLibrary library;
    Panel panel;

    if (load_inputs(library_path, panel_path, &library, &panel) != 0) {
        return 1;
    }

    printf("Panel: %s\n", panel.name[0] ? panel.name : "(unnamed)");
    printf("Blocks: %d\n", library.block_count);
    printf("Regions: %d\n", panel.region_count);
    printf("Rails: %d\n", panel.rail_count);
    printf("Devices: %d\n", panel.device_count);

    for (int i = 0; i < panel.device_count; ++i) {
        const Device *d = &panel.devices[i];
        printf("device=%s block=%s rail=%s domain=%s", d->id, d->block_id, d->rail_id, domain_to_str(d->domain));
        if (d->domain == DOMAIN_AC) {
            printf(" ac_level=%d", (int)d->ac_level);
        }
        printf(" pos=(%.1f,%.1f)\n", d->position.x, d->position.y);
    }

    return 0;
}

static void print_usage(const char *argv0) {
    printf("Usage:\n");
    printf("  %s validate <library.xml> <panel.xml>\n", argv0);
    printf("  %s summary <library.xml> <panel.xml>\n", argv0);
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
