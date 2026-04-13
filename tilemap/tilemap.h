/**
 * @file tilemap.h
 * @brief Isometric tilemap rendering system.
 *
 * This module provides tileset loading and tilemap management for isometric
 * 2D games. Water tiles are animated via GPU shaders - call
 * Renderer_SetWaterParams() before Tilemap_Render() to configure animation.
 *
 * Coordinate system:
 * - Tile (0,0) is at the top of the isometric diamond
 * - X increases towards bottom-right, Y increases towards bottom-left
 * - North is up, making the isometric diamond point north
 */

#ifndef TILEMAP_H
#define TILEMAP_H

#include "miso_iso.h"

#include <SDL3/SDL.h>

// =============================================================================
// Tileset - GPU texture + tile metadata
// =============================================================================

/**
 * @brief A tileset containing a GPU texture and tile metadata.
 *
 * The tileset image is a grid of equally-sized tiles. Tiles are indexed
 * left-to-right, top-to-bottom starting from 0.
 */
typedef struct Tileset {
    SDL_GPUTexture *texture;  ///< GPU texture handle (owned)
    unsigned int tile_width;  ///< Width of each tile in pixels
    unsigned int tile_height; ///< Height of each tile in pixels
    unsigned int columns;     ///< Number of columns in the tileset image
    unsigned int rows;        ///< Number of rows in the tileset image
    unsigned int total_tiles; ///< Total number of tiles (columns * rows)
} Tileset;

/**
 * @brief Load a tileset from an image file.
 *
 * The image dimensions must be evenly divisible by the tile dimensions.
 * Supported formats depend on SDL_image (PNG, BMP, etc.).
 *
 * @param image_path  Path to the tileset image file.
 * @param tile_width  Width of each tile in pixels.
 * @param tile_height Height of each tile in pixels.
 * @return Pointer to the loaded tileset, or NULL on failure.
 *
 * @note The caller is responsible for calling Tileset_Destroy() when done.
 */
Tileset *Tileset_Load(const char *image_path, unsigned int tile_width, unsigned int tile_height);

/**
 * @brief Destroy a tileset and free its GPU resources.
 * @param tileset The tileset to destroy (may be NULL).
 */
void Tileset_Destroy(Tileset *tileset);

// =============================================================================
// Tilemap - isometric tile-based map
// =============================================================================

/**
 * @brief Flags that can be applied to individual tiles.
 *
 * Flags are stored in a separate array from tile indices, allowing multiple
 * flags per tile. Use bitwise OR to combine flags.
 */
typedef enum TileFlags {
    TILE_FLAG_NONE = 0,         ///< No special behavior
    TILE_FLAG_WATER = 1 << 0,   ///< Tile is water (animated by GPU shader)
    TILE_FLAG_BLOCKED = 1 << 1, ///< Tile blocks movement/placement
} TileFlags;

/**
 * @brief An isometric tilemap with per-tile data and occupancy tracking.
 *
 * The tilemap stores tile indices, flags, and building occupancy in separate
 * arrays for cache-friendly access patterns. All arrays are sized
 * [width * height] and indexed as [y * width + x].
 */
typedef struct Tilemap {
    int *tiles;             ///< Tile indices [width * height] (owned)
    uint8_t *flags;         ///< Per-tile flags [width * height] (owned)
    bool *occupied;         ///< Building occupancy [width * height] (owned)
    void *render_instances; ///< Cached SpriteInstance array [width * height] (owned, internal)
    int width;              ///< Map width in tiles
    int height;             ///< Map height in tiles
    Tileset *tileset;       ///< Reference to the tileset (not owned)
} Tilemap;

/**
 * @brief Create a new tilemap with the given dimensions.
 *
 * All tiles are initialized to index 0 with no flags and unoccupied.
 *
 * @param width   Map width in tiles.
 * @param height  Map height in tiles.
 * @param tileset Tileset to use for rendering (not owned, must outlive tilemap).
 * @return Pointer to the created tilemap, or NULL on failure.
 *
 * @note The caller is responsible for calling Tilemap_Destroy() when done.
 */
Tilemap *Tilemap_Create(int width, int height, Tileset *tileset);

/**
 * @brief Destroy a tilemap and free its memory.
 * @param tilemap The tilemap to destroy (may be NULL).
 */
void Tilemap_Destroy(Tilemap *tilemap);

// =============================================================================
// Tile Access
// =============================================================================

/**
 * @brief Get the tile index at a position.
 * @param tilemap The tilemap to query.
 * @param x       Tile X coordinate.
 * @param y       Tile Y coordinate.
 * @return Tile index, or -1 if coordinates are out of bounds.
 */
int Tilemap_GetTile(const Tilemap *tilemap, int x, int y);

/**
 * @brief Set the tile index at a position.
 * @param tilemap    The tilemap to modify.
 * @param x          Tile X coordinate.
 * @param y          Tile Y coordinate.
 * @param tile_index The tile index to set (0 to tileset->total_tiles-1).
 * @note No-op if coordinates are out of bounds.
 */
void Tilemap_SetTile(const Tilemap *tilemap, int x, int y, int tile_index);

/**
 * @brief Get the flags for a tile.
 * @param tilemap The tilemap to query.
 * @param x       Tile X coordinate.
 * @param y       Tile Y coordinate.
 * @return Tile flags, or TILE_FLAG_NONE if coordinates are out of bounds.
 */
TileFlags Tilemap_GetFlags(const Tilemap *tilemap, int x, int y);

/**
 * @brief Set the flags for a tile.
 * @param tilemap The tilemap to modify.
 * @param x       Tile X coordinate.
 * @param y       Tile Y coordinate.
 * @param flags   Flags to set (use bitwise OR to combine).
 * @note No-op if coordinates are out of bounds.
 */
void Tilemap_SetFlags(const Tilemap *tilemap, int x, int y, TileFlags flags);

/**
 * @brief Check if a tile is free (not occupied by a building).
 * @param tilemap The tilemap to query.
 * @param x       Tile X coordinate.
 * @param y       Tile Y coordinate.
 * @return true if the tile exists and is not occupied, false otherwise.
 */
bool Tilemap_IsTileFree(const Tilemap *tilemap, int x, int y);

/**
 * @brief Set the occupancy state of a tile.
 * @param tilemap  The tilemap to modify.
 * @param x        Tile X coordinate.
 * @param y        Tile Y coordinate.
 * @param occupied true to mark as occupied, false to mark as free.
 * @note No-op if coordinates are out of bounds.
 */
void Tilemap_SetOccupied(const Tilemap *tilemap, int x, int y, bool occupied);

// =============================================================================
// Coordinate Conversion (Isometric)
// =============================================================================

/**
 * @brief Convert world coordinates to tile coordinates.
 *
 * The input coordinates should be in world space (after applying the inverse
 * camera transform to screen coordinates).
 *
 * @param tilemap  The tilemap (used for isometric dimensions).
 * @param screen_x World X coordinate.
 * @param screen_y World Y coordinate.
 * @return Tile coordinates. May be out of bounds; caller should validate.
 *
 * @see Tilemap_GetTile() for bounds-checked tile access.
 */
SDL_Point Tilemap_ScreenToTile(const Tilemap *tilemap, float screen_x, float screen_y);

// =============================================================================
// Rendering
// =============================================================================

/**
 * @brief Render the entire tilemap.
 *
 * Renders all tiles in a single batched draw call. Tiles marked with
 * TILE_FLAG_WATER are animated by the GPU shader.
 *
 * @param tilemap The tilemap to render.
 *
 * @pre Renderer_BeginFrame() has been called.
 * @pre Renderer_SetViewProjection() has been called with the camera matrix.
 * @pre Renderer_SetWaterParams() should be called to configure water animation.
 *
 * @see Renderer_SetWaterParams() for water animation configuration.
 */
void Tilemap_Render(const Tilemap *tilemap);

// =============================================================================
// Isometric Helpers (exposed for game code that needs them)
// =============================================================================

/**
 * @brief Get the isometric rendering dimensions for a tileset.
 *
 * In isometric projection, the visual tile height is half the sprite height.
 * These dimensions are used for world coordinate calculations.
 *
 * @param tileset    The tileset to query.
 * @param iso_width  Output: isometric tile width (same as sprite width).
 * @param iso_height Output: isometric tile height (sprite height / 2).
 */
static inline void
Tileset_GetIsoDimensions(const Tileset *const tileset, float *const iso_width, float *const iso_height) {
    *iso_width = (float)tileset->tile_width;
    *iso_height = (float)tileset->tile_height / 2.0f;
}

/**
 * @brief Convert tile coordinates to world position.
 *
 * Returns the top-left corner of the sprite in world coordinates.
 * The world origin is at the top of the isometric diamond.
 *
 * @param tilemap The tilemap (provides tileset dimensions).
 * @param tile_x  Tile X coordinate.
 * @param tile_y  Tile Y coordinate.
 * @param world_x Output: world X position.
 * @param world_y Output: world Y position.
 */
static inline void Tilemap_TileToWorld(
    const Tilemap *const tilemap, const int tile_x, const int tile_y, float *const world_x, float *const world_y) {
    const MisoIsoMapDesc desc = {
        .width_tiles = tilemap->width,
        .height_tiles = tilemap->height,
        .tile_w_px = (int)tilemap->tileset->tile_width,
        .tile_h_px = (int)tilemap->tileset->tile_height,
    };
    miso_iso_tile_to_world(&desc, tile_x, tile_y, world_x, world_y);
}

/**
 * @brief Calculate the depth value for a tile (for z-sorting).
 *
 * Tiles with higher (x + y) are closer to the camera and should be drawn
 * on top. This function returns a depth value where lower values are closer.
 *
 * @param tilemap The tilemap (provides dimensions for normalization).
 * @param tile_x  Tile X coordinate.
 * @param tile_y  Tile Y coordinate.
 * @return Depth value in range [0, 1], where 0 is closest to camera.
 */
static inline float Tilemap_GetTileDepth(const Tilemap *tilemap, const int tile_x, const int tile_y) {
    return 1.0f - (float)(tile_x + tile_y) / (float)(tilemap->width + tilemap->height);
}

#endif // TILEMAP_H
