#include "tilemap.h"

#include "../renderer/renderer.h"

#include <SDL3_image/SDL_image.h>
#include <stdint.h>

static inline int tilemap_cell_index(const Tilemap *const tilemap, const int x, const int y) {
    return y * tilemap->width + x;
}

static void
tilemap_build_render_instance(const Tilemap *const tilemap, const int x, const int y, SpriteInstance *const out) {
    if (!tilemap || !tilemap->tileset || !out) {
        return;
    }

    const int idx = tilemap_cell_index(tilemap, x, y);
    const int tile_index = tilemap->tiles[idx];
    const uint8_t flags = tilemap->flags[idx];

    const float tile_w = (float)tilemap->tileset->tile_width;
    const float tile_h = (float)tilemap->tileset->tile_height;
    const float iso_w = tile_w;
    const float iso_h = tile_h / 2.0f;

    const float start_x = ((float)(tilemap->height - 1) * iso_w) / 2.0f;
    const float iso_x = start_x + (float)(x - y) * iso_w / 2.0f;
    const float iso_y = (float)(x + y) * (iso_h / 2.0f);
    const float depth = 1.0f - (float)(x + y) / (float)(tilemap->width + tilemap->height);

    if (tile_index < 0 || tilemap->tileset->columns == 0U || tilemap->tileset->rows == 0U) {
        *out = (SpriteInstance){.x = iso_x,
                                .y = iso_y,
                                .z = depth,
                                .flags = 0.0f,
                                .w = 0.0f,
                                .h = 0.0f,
                                .tile_x = (float)x,
                                .tile_y = (float)y,
                                .u = 0.0f,
                                .v = 0.0f,
                                .uw = 0.0f,
                                .vh = 0.0f};
        return;
    }

    const float tex_w = (float)(tilemap->tileset->columns * tilemap->tileset->tile_width);
    const float tex_h = (float)(tilemap->tileset->rows * tilemap->tileset->tile_height);
    const int col = tile_index % (int)tilemap->tileset->columns;
    const int row = tile_index / (int)tilemap->tileset->columns;
    const float u = (float)(col * (int)tilemap->tileset->tile_width) / tex_w;
    const float v = (float)(row * (int)tilemap->tileset->tile_height) / tex_h;
    const float uw = (float)tilemap->tileset->tile_width / tex_w;
    const float vh = (float)tilemap->tileset->tile_height / tex_h;
    const float is_water = (flags & TILE_FLAG_WATER) ? 1.0f : 0.0f;

    *out = (SpriteInstance){.x = iso_x,
                            .y = iso_y,
                            .z = depth,
                            .flags = is_water,
                            .w = tile_w,
                            .h = tile_h,
                            .tile_x = (float)x,
                            .tile_y = (float)y,
                            .u = u,
                            .v = v,
                            .uw = uw,
                            .vh = vh};
}

static void tilemap_rebuild_render_cache(const Tilemap *const tilemap) {
    SpriteInstance *const instances = (SpriteInstance *)tilemap->render_instances;
    if (!instances) {
        return;
    }

    for (int y = 0; y < tilemap->height; y++) {
        for (int x = 0; x < tilemap->width; x++) {
            const int idx = tilemap_cell_index(tilemap, x, y);
            tilemap_build_render_instance(tilemap, x, y, &instances[idx]);
        }
    }
}

// =============================================================================
// Tileset Implementation
// =============================================================================

Tileset *Tileset_Load(const char *const image_path, const unsigned int tile_width, const unsigned int tile_height) {
    Tileset *const tileset = SDL_malloc(sizeof(Tileset));
    if (!tileset) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for tileset");
        return nullptr;
    }

    // Load texture via renderer
    tileset->texture = Renderer_LoadTexture(image_path);
    if (!tileset->texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create texture from %s", image_path);
        SDL_free(tileset);
        return nullptr;
    }

    // Load image to get dimensions (init time only, acceptable overhead)
    SDL_Surface *const surface = IMG_Load(image_path);
    if (surface) {
        tileset->columns = (unsigned int)surface->w / tile_width;
        tileset->rows = (unsigned int)surface->h / tile_height;
        SDL_DestroySurface(surface);
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not load image for dimensions: %s", image_path);
        tileset->columns = 0;
        tileset->rows = 0;
    }

    tileset->tile_width = tile_width;
    tileset->tile_height = tile_height;
    tileset->total_tiles = tileset->columns * tileset->rows;

    SDL_Log("Loaded tileset: %ux%u tiles, %u columns, %u rows, %u total tiles",
            tile_width,
            tile_height,
            tileset->columns,
            tileset->rows,
            tileset->total_tiles);

    return tileset;
}

void Tileset_Destroy(Tileset *const tileset) {
    if (tileset) {
        if (tileset->texture) {
            Renderer_DestroyTexture(tileset->texture);
        }
        SDL_free(tileset);
    }
}

// =============================================================================
// Tilemap Implementation
// =============================================================================

Tilemap *Tilemap_Create(const int width, const int height, Tileset *const tileset) {
    if (width <= 0 || height <= 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid tilemap dimensions: %dx%d", width, height);
        return nullptr;
    }

    Tilemap *const tilemap = SDL_malloc(sizeof(Tilemap));
    if (!tilemap) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for tilemap");
        return nullptr;
    }

    const size_t tile_count = (size_t)width * (size_t)height;

    tilemap->tiles = SDL_malloc(tile_count * sizeof(int));
    if (!tilemap->tiles) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for tilemap tiles");
        SDL_free(tilemap);
        return nullptr;
    }

    tilemap->flags = SDL_malloc(tile_count * sizeof(uint8_t));
    if (!tilemap->flags) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for tilemap flags");
        SDL_free(tilemap->tiles);
        SDL_free(tilemap);
        return nullptr;
    }

    tilemap->occupied = SDL_malloc(tile_count * sizeof(bool));
    if (!tilemap->occupied) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for tilemap occupancy");
        SDL_free(tilemap->flags);
        SDL_free(tilemap->tiles);
        SDL_free(tilemap);
        return nullptr;
    }

    tilemap->render_instances = SDL_malloc(tile_count * sizeof(SpriteInstance));
    if (!tilemap->render_instances) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for tilemap render cache");
        SDL_free(tilemap->occupied);
        SDL_free(tilemap->flags);
        SDL_free(tilemap->tiles);
        SDL_free(tilemap);
        return nullptr;
    }

    // Initialize to default values
    SDL_memset(tilemap->tiles, 0, tile_count * sizeof(int));
    SDL_memset(tilemap->flags, 0, tile_count * sizeof(uint8_t));
    SDL_memset(tilemap->occupied, false, tile_count * sizeof(bool));

    tilemap->width = width;
    tilemap->height = height;
    tilemap->tileset = tileset;
    tilemap_rebuild_render_cache(tilemap);

    SDL_Log("Created tilemap: %dx%d tiles", width, height);
    return tilemap;
}

void Tilemap_Destroy(Tilemap *const tilemap) {
    if (tilemap) {
        SDL_free(tilemap->render_instances);
        SDL_free(tilemap->tiles);
        SDL_free(tilemap->flags);
        SDL_free(tilemap->occupied);
        SDL_free(tilemap);
    }
}

// =============================================================================
// Tile Access
// =============================================================================

static inline bool tilemap_in_bounds(const Tilemap *tilemap, const int x, const int y) {
    return x >= 0 && y >= 0 && x < tilemap->width && y < tilemap->height;
}

int Tilemap_GetTile(const Tilemap *const tilemap, const int x, const int y) {
    if (!tilemap_in_bounds(tilemap, x, y)) {
        return -1;
    }
    return tilemap->tiles[tilemap_cell_index(tilemap, x, y)];
}

void Tilemap_SetTile(const Tilemap *const tilemap, const int x, const int y, const int tile_index) {
    if (tilemap_in_bounds(tilemap, x, y)) {
        const int idx = tilemap_cell_index(tilemap, x, y);
        tilemap->tiles[idx] = tile_index;

        SpriteInstance *const instances = (SpriteInstance *)tilemap->render_instances;
        if (instances) {
            tilemap_build_render_instance(tilemap, x, y, &instances[idx]);
        }
    }
}

TileFlags Tilemap_GetFlags(const Tilemap *const tilemap, const int x, const int y) {
    if (!tilemap_in_bounds(tilemap, x, y)) {
        return TILE_FLAG_NONE;
    }
    return (TileFlags)tilemap->flags[tilemap_cell_index(tilemap, x, y)];
}

void Tilemap_SetFlags(const Tilemap *const tilemap, const int x, const int y, const TileFlags flags) {
    if (tilemap_in_bounds(tilemap, x, y)) {
        const int idx = tilemap_cell_index(tilemap, x, y);
        tilemap->flags[idx] = (uint8_t)flags;

        SpriteInstance *const instances = (SpriteInstance *)tilemap->render_instances;
        if (instances) {
            tilemap_build_render_instance(tilemap, x, y, &instances[idx]);
        }
    }
}

bool Tilemap_IsTileFree(const Tilemap *const tilemap, const int x, const int y) {
    if (!tilemap_in_bounds(tilemap, x, y)) {
        return false;
    }
    return !tilemap->occupied[tilemap_cell_index(tilemap, x, y)];
}

void Tilemap_SetOccupied(const Tilemap *const tilemap, const int x, const int y, const bool occupied) {
    if (tilemap_in_bounds(tilemap, x, y)) {
        tilemap->occupied[tilemap_cell_index(tilemap, x, y)] = occupied;
    }
}

// =============================================================================
// Coordinate Conversion
// =============================================================================

SDL_Point Tilemap_ScreenToTile(const Tilemap *const tilemap, const float screen_x, const float screen_y) {
    const float tile_w = (float)tilemap->tileset->tile_width;
    const float tile_h = (float)tilemap->tileset->tile_height;

    // Isometric dimensions
    const float iso_w = tile_w;
    const float iso_h_step = tile_h / 2.0f;

    const float half_iso_w = iso_w / 2.0f;
    const float half_iso_h_step = iso_h_step / 2.0f;

    // Map origin (matches Tilemap_Render start position)
    const float origin_x = ((float)(tilemap->height - 1) * iso_w) / 2.0f + half_iso_w;
    constexpr float origin_y = 0.0f;

    // Mouse relative to origin
    const float rel_x = screen_x - origin_x;
    const float rel_y = screen_y - origin_y;

    // Inverse isometric projection
    const float term_a = rel_x / half_iso_w;
    const float term_b = rel_y / half_iso_h_step;

    const float cart_x = (term_a + term_b) / 2.0f;
    const float cart_y = (term_b - term_a) / 2.0f;

    return (SDL_Point){.x = (int)SDL_floorf(cart_x), .y = (int)SDL_floorf(cart_y)};
}

// =============================================================================
// Rendering
// =============================================================================

void Tilemap_Render(const Tilemap *const tilemap) {
    if (!tilemap || !tilemap->tileset || !tilemap->tileset->texture) {
        return;
    }

    const int tile_count = tilemap->width * tilemap->height;
    if (tile_count <= 0) {
        return;
    }

    const SpriteInstance *const instances = (const SpriteInstance *)tilemap->render_instances;
    if (!instances) {
        return;
    }

    Renderer_DrawSprites(tilemap->tileset->texture, instances, tile_count);
}
