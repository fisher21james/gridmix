// Keeps settings in the board's flash (survives power cuts and firmware updates).
#pragma once
#include "settings.h"

bool settings_load(gridcore::Settings &s);
bool settings_save(const gridcore::Settings &s);
