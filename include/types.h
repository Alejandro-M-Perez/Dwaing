#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>

#define MAX_ID_LEN 63
#define MAX_BLOCKS 256
#define MAX_CONNECTORS 32
#define MAX_REGIONS 128
#define MAX_RAILS 256
#define MAX_DEVICES 1024

typedef enum {
    DOMAIN_UNSPECIFIED = 0,
    DOMAIN_AC,
    DOMAIN_DC
} VoltageDomain;

typedef enum {
    AC_LEVEL_NONE = 0,
    AC_LEVEL_120 = 120,
    AC_LEVEL_240 = 240,
    AC_LEVEL_480 = 480
} AcLevel;

typedef struct {
    char message[256];
} ValidationError;

#endif
