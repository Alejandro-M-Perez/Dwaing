#ifndef RULES_H
#define RULES_H

#include "model.h"

int validate_panel(const BlockLibrary *library, const Panel *panel, ValidationError *errors, int max_errors);
int collect_panel_warnings(const BlockLibrary *library, const Panel *panel, ValidationError *warnings, int max_warnings);
int validate_document(const AssetLibrary *library, const Document *document, ValidationError *errors, int max_errors);
int collect_document_warnings(const AssetLibrary *library, const Document *document, ValidationError *warnings, int max_warnings);

#endif
