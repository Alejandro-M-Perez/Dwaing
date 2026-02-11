#include "rules.h"

#include <stdio.h>
#include <string.h>

#define MIN_DEVICE_SPACING 8.0

static int push_error(ValidationError *errors, int max_errors, int count, const char *msg) {
    if (count < max_errors) {
        strncpy(errors[count].message, msg, sizeof(errors[count].message) - 1);
        errors[count].message[sizeof(errors[count].message) - 1] = '\0';
    }
    return count + 1;
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

int validate_panel(const BlockLibrary *library, const Panel *panel, ValidationError *errors, int max_errors) {
    int count = 0;

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
            if (d->position.x < region->min.x || d->position.x > region->max.x || d->position.y < region->min.y || d->position.y > region->max.y) {
                snprintf(msg, sizeof(msg), "device '%s': position is outside region '%s'", d->id, d->region_id);
                count = push_error(errors, max_errors, count, msg);
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
