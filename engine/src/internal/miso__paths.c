#include "miso__paths.h"

#include "miso__engine_internal.h"

#include <SDL3/SDL.h>

static bool miso__copy_path(const char *const path, char *const out, const size_t out_size) {
    if (!path || !out || out_size == 0) {
        return false;
    }

    return SDL_strlcpy(out, path, out_size) < out_size;
}

static bool miso__path_exists(const char *const path) {
    if (!path || path[0] == '\0') {
        return false;
    }

    SDL_PathInfo info = {0};
    return SDL_GetPathInfo(path, &info);
}

static bool
miso__try_root_anchor(const char *const root, const char *const anchor, char *const out, const size_t out_size) {
    char candidate[MISO_PATH_MAX];
    if (!miso__path_join(root, anchor, candidate, sizeof(candidate))) {
        return false;
    }
    if (!miso__path_exists(candidate)) {
        return false;
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Found anchor: %s", candidate);
    return miso__copy_path(root, out, out_size);
}

bool miso__path_is_absolute(const char *const path) {
    if (!path || path[0] == '\0') {
        return false;
    }

    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }

    return SDL_isalpha((unsigned char)path[0]) && path[1] == ':';
}

bool miso__path_join(const char *const root, const char *const relative, char *const out, const size_t out_size) {
    if (!root || !relative || !out || out_size == 0) {
        return false;
    }
    if (relative[0] == '\0') {
        return miso__copy_path(root, out, out_size);
    }
    if (miso__path_is_absolute(relative)) {
        return miso__copy_path(relative, out, out_size);
    }

    const size_t root_len = SDL_strlen(root);
    if (root_len == 0) {
        return miso__copy_path(relative, out, out_size);
    }

    const bool root_has_separator = root[root_len - 1] == '/' || root[root_len - 1] == '\\';
    const int written = SDL_snprintf(out, out_size, "%s%s%s", root, root_has_separator ? "" : "/", relative);
    return written >= 0 && (size_t)written < out_size;
}

bool miso__resolve_data_root(const MisoConfig *const cfg, char *const out, const size_t out_size) {
    if (!out || out_size == 0) {
        return false;
    }

    if (cfg && cfg->data_root && cfg->data_root[0] != '\0') {
        return miso__copy_path(cfg->data_root, out, out_size);
    }

    const char *const base_path = SDL_GetBasePath();
    if (!base_path || base_path[0] == '\0') {
        return false;
    }

    static const char *const anchor = "shaders/sprite.metal";
    if (miso__try_root_anchor(base_path, anchor, out, out_size)) {
        return true;
    }

    char app_bundle_root[MISO_PATH_MAX];
    if (miso__path_join(base_path, "../../../../", app_bundle_root, sizeof(app_bundle_root)) &&
        miso__try_root_anchor(app_bundle_root, anchor, out, out_size)) {
        return true;
    }

    return miso__copy_path(base_path, out, out_size);
}

bool miso__resolve_asset_path(const MisoEngine *const engine,
                              const char *const relative_or_absolute,
                              char *const out,
                              const size_t out_size) {
    if (!engine || !relative_or_absolute || !out || out_size == 0) {
        return false;
    }

    if (miso__path_is_absolute(relative_or_absolute)) {
        return miso__copy_path(relative_or_absolute, out, out_size);
    }

    const char *const root = engine->has_data_root ? engine->data_root : SDL_GetBasePath();
    if (!root || root[0] == '\0') {
        return false;
    }

    char primary[MISO_PATH_MAX];
    if (!miso__path_join(root, relative_or_absolute, primary, sizeof(primary))) {
        return false;
    }
    if (miso__path_exists(primary)) {
        return miso__copy_path(primary, out, out_size);
    }

    //TODO: needed rn bc assets is in SDL/assets, not in project dir, but consider removing in the future
    // (could lead to permission error on some platforms maybe)
    char sibling[MISO_PATH_MAX];
    if (miso__path_join(root, "../", sibling, sizeof(sibling))) {
        char sibling_candidate[MISO_PATH_MAX];
        if (miso__path_join(sibling, relative_or_absolute, sibling_candidate, sizeof(sibling_candidate)) &&
            miso__path_exists(sibling_candidate)) {
            return miso__copy_path(sibling_candidate, out, out_size);
        }
    }

    return miso__copy_path(primary, out, out_size);
}

bool miso_resolve_asset_path(const MisoEngine *const engine,
                             const char *const path,
                             char *const out_path,
                             const size_t out_path_size) {
    return miso__resolve_asset_path(engine, path, out_path, out_path_size);
}

const char *miso_get_data_root(const MisoEngine *const engine) {
    if (!engine || !engine->has_data_root) {
        return nullptr;
    }
    return engine->data_root;
}
