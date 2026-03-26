#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdbool.h>
#include "model.h"

bool resolve_relative_devices(const BlockLibrary *library, Panel *panel, char *error, int error_len);
bool layout_document(const AssetLibrary *library, Document *document, char *error, int error_len);

#endif
