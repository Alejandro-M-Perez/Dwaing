#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include "model.h"

bool parse_block_library(const char *path, BlockLibrary *library, char *error, int error_len);
bool parse_panel(const char *path, Panel *panel, char *error, int error_len);

#endif
