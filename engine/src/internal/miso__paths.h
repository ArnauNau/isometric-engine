#ifndef NAU_ENGINE_MISO__PATHS_H
#define NAU_ENGINE_MISO__PATHS_H

#include "miso_engine.h"

#include <SDL3/SDL.h>
#include <stddef.h>

bool miso__path_is_absolute(const char *path);
bool miso__path_join(const char *root, const char *relative, char *out, size_t out_size);
bool miso__resolve_data_root(const MisoConfig *cfg, char *out, size_t out_size);
bool miso__resolve_asset_path(const MisoEngine *engine, const char *relative_or_absolute, char *out, size_t out_size);

#endif //NAU_ENGINE_MISO__PATHS_H
