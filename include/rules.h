#ifndef RULES_H
#define RULES_H

#include "model.h"

int validate_panel(const BlockLibrary *library, const Panel *panel, ValidationError *errors, int max_errors);

#endif
