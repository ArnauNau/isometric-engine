#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>

#include "camera/camera.h"
#include "engine.h"
#include "profiler.h"

#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080
static int screen_width = WINDOW_WIDTH;
static int screen_height = WINDOW_HEIGHT;

#define NUM_CIRCLE_SEGMENTS 16 // Adjust for smoother curves

int is_point_in_rect(const float x, const float y,
                     const SDL_FRect *const SDL_RESTRICT rect) {
  return (x >= rect->x && x <= rect->x + rect->w && y >= rect->y &&
          y <= rect->y + rect->h);
}

typedef enum TileType_ {
  TILE_PLACEHOLDER_BUILDING = 48,
  TILE_PLACEHOLDER_TERRAIN = 18,
  TILE_PLACEHOLDER_SEA = 13,
  TILE_PLACEHOLDER_BOAT = 54
} TileType;

// typedef enum {
//     TYPE_BUILDING_TILE,
//     TYPE_TERRAIN_TILE,
//     TYPE_SEA_TILE,
//     TYPE_BOAT
// } EntityType;
//
// typedef struct SpriteDefinition_ {
//     int tile_index;
//     int width, height;
// } SpriteDefinition;
//
// static const SpriteDefinition sprite_definitions[] = {
//     [TYPE_BUILDING_TILE] = { TILE_PLACEHOLDER_BUILDING, 1, 1},
//     [TYPE_TERRAIN_TILE] = { TILE_PLACEHOLDER_TERRAIN, 1, 1},
//     [TYPE_SEA_TILE] = { TILE_PLACEHOLDER_SEA, 1, 1},
//     [TYPE_BOAT] = { TILE_PLACEHOLDER_BOAT, 2, 2},
// };

#define MAX_BUILDINGS 512

// NOTE: ECS stuff:
//  Track if an entity is in use. (p.e:
//       typedef uint32_t Entity;
//       #define MAX_ENTITIES 1024
//       bool entity_alive[MAX_ENTITIES])
//  Create/destroy with a pool allocator or free list.

typedef struct TransformComponent_ {
  int x, y;
} TransformComponent;

typedef struct BuildingComponent_ {
  // EntityType type;
  int width, length;
} BuildingComponent;

typedef struct RenderableComponent_ {
  int tile_index;
  int sprite_w, sprite_h;
} RenderableComponent;

static RenderableComponent
    renderables[MAX_BUILDINGS]; // TODO: think about allocations etc...
static TransformComponent transforms[MAX_BUILDINGS];
static BuildingComponent buildings[MAX_BUILDINGS];
static int building_count;

static bool wireframe_mode = false;
static bool debug_mode = false;

typedef struct Tileset_ {
  SDL_Texture *texture;     // The tileset image
  unsigned int tile_width;  // Width of each tile (32px)
  unsigned int tile_height; // Height of each tile (32px)
  unsigned int columns;     // Number of columns in the tileset
  unsigned int rows;        // Number of rows in the tileset
  unsigned int total_tiles; // Total number of tiles
} Tileset;

/**
 * Map information including size, logical representation, rendering info, and
 * view info
 */
typedef struct Map_ {
  int *tiles;       // Array of tile indices
  int width;        // Map width in tiles
  int height;       // Map height in tiles
  bool *occupied;   // Building occupancy
  Tileset *tileset; // Reference to the tileset
} Map;

// typedef struct Boat_ {
//     int x, y;
//     SDL_Texture *sprite;
// } Boat;

// bool load_boat(SDL_Renderer *const renderer, const char *const image_path,
// Boat *const boat) {
//
//     SDL_Surface *surface = IMG_Load(image_path);
//     if (!surface) {
//         SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load boat
//         image: %s", SDL_GetError()); return false;
//     }
//
//     boat->sprite = SDL_CreateTextureFromSurface(renderer, surface);
//     SDL_DestroyTexture(boat->sprite);
//
//     if (!boat->sprite) {
//         SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create boat
//         texture: %s", SDL_GetError()); return false;
//     }
//
//     SDL_SetTextureScaleMode(boat->sprite, SDL_SCALEMODE_NEAREST);
//
//     return true;
// }

Tileset *load_tileset(SDL_Renderer *const SDL_RESTRICT renderer,
                      const char *const SDL_RESTRICT image_path,
                      const unsigned int tile_width,
                      const unsigned int tile_height) {
  // Allocate tileset structure
  Tileset *const SDL_RESTRICT tileset = SDL_malloc(sizeof(Tileset));
  if (!tileset) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Failed to allocate memory for tileset");
    return nullptr;
  }

  // Load the image
  SDL_Surface *const SDL_RESTRICT surface = IMG_Load(image_path);
  if (!surface) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Failed to load tileset image: %s", SDL_GetError());
    SDL_free(tileset);
    return nullptr;
  }

  // Create texture from surface
  tileset->texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_DestroySurface(surface);

  if (!tileset->texture) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create texture: %s",
                 SDL_GetError());
    SDL_free(tileset);
    return nullptr;
  }

  // Set nearest neighbor filtering for crisp pixel art
  SDL_SetTextureScaleMode(tileset->texture, SDL_SCALEMODE_PIXELART);

  // Calculate tileset dimensions
  float texture_width_f, texture_height_f;
  SDL_GetTextureSize(tileset->texture, &texture_width_f, &texture_height_f);
  const unsigned int texture_width = (unsigned int)texture_width_f;
  const unsigned int texture_height = (unsigned int)texture_height_f;

  tileset->tile_width = tile_width;
  tileset->tile_height = tile_height;
  tileset->columns = texture_width / tile_width;
  tileset->rows = texture_height / tile_height;
  tileset->total_tiles = tileset->columns * tileset->rows;

  SDL_Log("Loaded tileset: %d×%d tiles, %d columns, %d rows, %d total tiles",
          tile_width, tile_height, tileset->columns, tileset->rows,
          tileset->total_tiles);

  return tileset;
}

Map *create_map(const int width, const int height,
                Tileset *const SDL_RESTRICT tileset) {
  Map *const map = SDL_malloc(sizeof(Map));
  if (!map) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Failed to allocate memory for map");
    return nullptr;
  }

  map->tiles = (int *)SDL_malloc((unsigned int)width * (unsigned int)height *
                                 sizeof(int));
  if (!map->tiles) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Failed to allocate memory for map data");
    SDL_free(map);
    return nullptr;
  }

  map->occupied = (bool *)SDL_malloc((unsigned int)width *
                                     (unsigned int)height * sizeof(bool));
  if (!map->occupied) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Failed to allocate memory for map data");
    SDL_free(map->tiles);
    SDL_free(map);
    return nullptr;
  }

  // initialize even on empty, odd on first tile
  memset(map->occupied, false,
         (unsigned int)width * (unsigned int)height * sizeof(bool));
  for (int i = 0; i < (width * height); i++) {
    int tile_index = i % 2 ? 0 : 36;
    if (i > ((width * height) - (width / 2) * (height / 2))) {
      tile_index = TILE_PLACEHOLDER_SEA;
    }
    map->tiles[i] = tile_index;
    if (tile_index == TILE_PLACEHOLDER_SEA) {
      // map->occupied[i] = true; //NOTE: commented to allow putting boats on
      // water for now, as I'm considering them buildings to test
    }
  }

  map->width = width;
  map->height = height;
  map->tileset = tileset;

  return map;
}

SDL_Point hover_tile = {-1, -1};

// TODO: idea, weather conditions like in P3, can affect this! also refine wave
// math formula so it's more like a real sea wave
static float sea_time = 0.0f;
#define WAVE_SPEED 0.2f // how fast does the wave cycle go
#define WAVE_AMPLITUDE                                                         \
  0.5f // how high are the waves (amplitude) in terms of tile height
#define WAVE_PHASE                                                             \
  0.1f // how 'wide' are the waves (period) //TODO: in terms of tile width, not
       // done yet.

void render_map(SDL_Renderer *const SDL_RESTRICT renderer,
                const Map *const SDL_RESTRICT map, const float offset_x,
                const float offset_y) {
  PROF_start(PROFILER_RENDER_MAP);

  // Calculate tile dimensions
  const float tile_w = (float)map->tileset->tile_width;
  const float tile_h = (float)map->tileset->tile_height;

  // For isometric maps, we need to convert coordinates
  // Isometric tile width and height (visible portion)
  const float iso_w = tile_w;
  const float iso_h = tile_h / 2.0f; // Half height for proper overlap

  // Start position for rendering
  const float iso_map_w = ((float)map->width * iso_w);
  const float iso_map_h = ((float)map->height * iso_h);
  const float render_map_w = iso_map_w / 2 + ((float)map->height * iso_w) / 2;
  const float render_map_h = iso_map_h / 2 + ((float)map->width * iso_h) / 2;
  const float start_x =
      offset_x +
      ((float)(map->height - 1) * iso_w) /
          2; /*+ (WINDOW_WIDTH / 2.0f) - ((float)map->width * iso_w / 2.0f);*/
  const float start_y =
      offset_y /*+ 100.0f*/; // Adjust vertical position as needed
                             //  SDL_Log("Start x: %.2f", start_x);
                             //  SDL_Log("Start y: %.2f", start_y);
                             //  SDL_Log("IsoMap width: %.2f", iso_map_w);

  // Loop through all tiles in the map
  for (int y = 0; y < map->height; y++) {
    for (int x = 0; x < map->width; x++) {
      // Get the tile index from the map data
      const int tile_index = map->tiles[y * map->width + x];

      // Skip if it's an empty tile (assuming 0 is empty)
      if (tile_index < 0)
        continue;

      // Calculate source rectangle from the tileset
      const float src_x =
          (float)((tile_index % (int)map->tileset->columns) * (int)tile_w);
      const float src_y =
          (float)((tile_index / (int)map->tileset->columns) * (int)tile_h);
      const SDL_FRect src = {src_x, src_y, tile_w, tile_h};

      // Calculate isometric position
      // Isometric formula: screen_x = (cart_x - cart_y) * tile_width/2
      //                    screen_y = (cart_x + cart_y) * tile_height/2
      const float iso_x = start_x + (float)(x - y) * iso_w / 2.0f;
      float tile_y_offset = 0.0f; // for things like sea animation / placement
      if (tile_index == TILE_PLACEHOLDER_SEA) {
        // every tile has a different phase so they don't all move in unison
        const float phase = (float)(x + y) * WAVE_PHASE;
        const float adjusted_amplitude = (iso_h / 2.0f) * WAVE_AMPLITUDE;
        constexpr float sea_tide = 0;
        const float wave_offset_compensation =
            /*iso_h - */ ((iso_h / 2.0f) * WAVE_AMPLITUDE) +
            (sea_tide + iso_h / 2.0f);
        tile_y_offset = /*-(iso_h/4.0f) +*/ SDL_sinf(sea_time + phase) *
                            adjusted_amplitude -
                        wave_offset_compensation;
      }
      const float iso_y =
          start_y + ((float)(x + y) * (iso_h / 2.0f)) - tile_y_offset;

      // Create destination rectangle
      SDL_FRect dst = {iso_x, iso_y, (float)tile_w, (float)tile_h};
      /*f (x == hover_tile.x && y == hover_tile.y) {
          dst.y -= iso_h / 4;
      }*/

      // Render the tile
      SDL_RenderTexture(renderer, map->tileset->texture, &src, &dst);

      // debug
      if (x == map->width - 1 && y == map->height - 1) {
        // SDL_Log("Sea level: %.2f", tile_y_offset);
        // SDL_Log("iso_y: %.2f | iso_y_og: %.2f", iso_y, start_y + ((float)(x +
        // y) * (iso_h / 2.0f)));

        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        SDL_RenderLine(renderer, iso_x, iso_y, iso_x + iso_w, iso_y);
        SDL_RenderLine(renderer, iso_x, iso_y + iso_h, iso_x + iso_w,
                       iso_y + iso_h);
        SDL_RenderLine(renderer, iso_x, iso_y, iso_x, iso_y + iso_h);
        SDL_RenderLine(renderer, iso_x + iso_w, iso_y, iso_x + iso_w,
                       iso_y + iso_h);

        SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
        SDL_RenderLine(renderer, iso_x + iso_w / 2, iso_y, iso_x + iso_w,
                       iso_y + iso_h / 2.0f);
        SDL_RenderLine(renderer, iso_x + iso_w / 2, iso_y, iso_x,
                       iso_y + iso_h / 2.0f);
        SDL_RenderLine(renderer, iso_x + iso_w / 2, iso_y, iso_x + iso_w,
                       iso_y - iso_h / 2.0f);
        SDL_RenderLine(renderer, iso_x + iso_w / 2, iso_y, iso_x,
                       iso_y - iso_h / 2.0f);
      }
    }
  }
  // limit rulers
  SDL_SetRenderDrawColor(renderer, 255, 0, 200, 255);
  SDL_RenderLine(renderer, start_x + iso_w / 2, start_y,
                 start_x + iso_map_w / 2, start_y); // drawing y, start y
  SDL_RenderLine(renderer, start_x + iso_w / 2, start_y, start_x + iso_w / 2,
                 start_y + iso_map_h); // drawing x
  SDL_RenderLine(renderer, offset_x, start_y, offset_x,
                 start_y + iso_map_h); // offset x
  // center rulers
  SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
  SDL_RenderLine(renderer, offset_x + render_map_w, offset_y,
                 offset_x + render_map_w,
                 offset_y + render_map_h); // render w x
  SDL_RenderLine(renderer, offset_x, offset_y + render_map_h / 2,
                 offset_x + render_map_w,
                 offset_y + render_map_h / 2); // center w x
  SDL_RenderLine(renderer, offset_x, offset_y + render_map_h,
                 offset_x + render_map_w,
                 offset_y + render_map_h); // render h y
  SDL_RenderLine(renderer, offset_x + render_map_w / 2, offset_y,
                 offset_x + render_map_w / 2,
                 offset_y + render_map_h); // center h y

  // selected tile debug
  {
    // Calculate isometric position
    // Isometric formula: screen_x = (cart_x - cart_y) * tile_width/2
    //                    screen_y = (cart_x + cart_y) * tile_height/2
    const float iso_x =
        start_x + (float)(hover_tile.x - hover_tile.y) * iso_w / 2.0f;
    const float iso_y =
        start_y + ((float)(hover_tile.x + hover_tile.y) * (iso_h / 2.0f));

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 100);
    SDL_RenderLine(renderer, iso_x + iso_w / 2, iso_y + iso_h / 2,
                   iso_x + iso_w / 2, 0);

    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_RenderLine(renderer, iso_x, iso_y, iso_x + iso_w, iso_y);
    SDL_RenderLine(renderer, iso_x, iso_y + iso_h, iso_x + iso_w,
                   iso_y + iso_h);
    SDL_RenderLine(renderer, iso_x, iso_y, iso_x, iso_y + iso_h);
    SDL_RenderLine(renderer, iso_x + iso_w, iso_y, iso_x + iso_w,
                   iso_y + iso_h);

    SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
    SDL_RenderLine(renderer, iso_x + iso_w / 2, iso_y, iso_x + iso_w,
                   iso_y + iso_h / 2.0f);
    SDL_RenderLine(renderer, iso_x + iso_w / 2, iso_y, iso_x,
                   iso_y + iso_h / 2.0f);
    SDL_RenderLine(renderer, iso_x + iso_w / 2, iso_y, iso_x + iso_w,
                   iso_y - iso_h / 2.0f);
    SDL_RenderLine(renderer, iso_x + iso_w / 2, iso_y, iso_x,
                   iso_y - iso_h / 2.0f);
  }

  PROF_stop(PROFILER_RENDER_MAP);
}

typedef struct WireframeMesh_ {
  SDL_Vertex *verts;
  int vert_count;
} WireframeMesh;

static WireframeMesh wireframe_meshes[MAX_BUILDINGS];

// --- helper to build and cache a mesh ---
static WireframeMesh build_wireframe_mesh(const Map *const SDL_RESTRICT map,
                                          const float iso_x, const float iso_y,
                                          const float iso_w, const float iso_h,
                                          const int bw, const int bl,
                                          const int sw, const int sh) {
  const float half_w = iso_w * 0.5f;
  const float half_h = iso_h * 0.5f;
  const float tile_h = iso_h * 2.0f;
  const float base_y = iso_y + tile_h * (float)sh;
  const SDL_FColor col = {0, 255, 255, 255};
  WireframeMesh thin_mesh;

  {
    // estimate max lines and allocate twice as many vertices, aligned to a
    // multiple of 3, for rendering constraints
    const int max_lines =
        (bw + bl) + bw * (sh + 1) + bl * (sh + 1) + (bw + 1) * (bl + 1);
    const int num_vertices = max_lines * 2 + (3 - ((max_lines * 2) % 3));
    SDL_Vertex *v = SDL_malloc(sizeof(SDL_Vertex) * (unsigned int)num_vertices);
    int idx = 0;

    // width-wise lines
    for (int x = 0; x < bw; x++) {
      const float vx = iso_x + half_w * (float)x;
      const float vb = base_y - half_h * (float)(bw - x);
      const float vt = vb - iso_h * (float)sh;
      // vertical
      v[idx++] = (SDL_Vertex){{vx, vb}, col, {0, 0}};
      v[idx++] = (SDL_Vertex){{vx, vt}, col, {0, 0}};
      // top faces
      for (int y = 0; y <= sh; y++) {
        const float dy = vb - iso_h * (float)y;
        v[idx++] = (SDL_Vertex){{vx, dy}, col, {0, 0}};
        v[idx++] = (SDL_Vertex){{vx + half_w, dy + half_h}, col, {0, 0}};
      }
    }
    // length-wise lines
    for (int x = 0; x <= bl; x++) {
      const float vx = iso_x + iso_w * (float)(bw + x) * 0.5f;
      const float vb = base_y - iso_h * 0.5f * (float)x;
      const float vt = vb - iso_h * (float)sh;
      v[idx++] = (SDL_Vertex){{vx, vb}, col, {0, 0}};
      v[idx++] = (SDL_Vertex){{vx, vt}, col, {0, 0}};
      if (x < bl) {
        for (int y = 0; y <= sh; y++) {
          const float dy = vb - iso_h * (float)y;
          v[idx++] = (SDL_Vertex){{vx, dy}, col, {0, 0}};
          v[idx++] = (SDL_Vertex){{vx + half_w, dy - half_h}, col, {0, 0}};
        }
      }
    }

    // roof side
    const SDL_FPoint roof_fl = {iso_x, base_y - half_h * (float)bw -
                                           iso_h * (float)sh}; // front-left
    const SDL_FPoint roof_fr = {iso_x + half_w * (float)bw,
                                base_y - half_h * (float)bw -
                                    iso_h * (float)sh +
                                    half_h * (float)bw}; // front-right
    const SDL_FPoint roof_bl = {iso_x + half_w * (float)bl,
                                roof_fl.y - half_h * (float)bl}; // back-left
    const SDL_FPoint roof_br = {iso_x + half_w * (bw + bl),
                                roof_fr.y - half_h * bl}; // back-right

    const SDL_FPoint dv_len = {half_w, -half_h};  // length direction
    const SDL_FPoint dv_width = {half_w, half_h}; // width direction

    // lines parallel to length (splitting width units on the roof)
    for (int i = 1; i <= bl; i++) {
      SDL_FPoint p0 = {roof_fl.x + dv_len.x * (float)i,
                       roof_fl.y + dv_len.y * (float)i};
      SDL_FPoint p1 = {roof_fr.x + dv_len.x * (float)i,
                       roof_fr.y + dv_len.y * (float)i};
      v[idx++] = (SDL_Vertex){p0, col, {0, 0}};
      v[idx++] = (SDL_Vertex){p1, col, {0, 0}};
    }

    // lines parallel to width (splitting length units on the roof)
    for (int j = 0; j < bw; j++) {
      SDL_FPoint p0 = {roof_fl.x + dv_width.x * (float)j,
                       roof_fl.y + dv_width.y * (float)j};
      SDL_FPoint p1 = {roof_bl.x + dv_width.x * (float)j,
                       roof_bl.y + dv_width.y * (float)j};
      v[idx++] = (SDL_Vertex){p0, col, {0, 0}};
      v[idx++] = (SDL_Vertex){p1, col, {0, 0}};
    }

    const int remaining = num_vertices - idx;
    for (int i = 0; i < remaining; i++) {
      const SDL_Vertex last = v[idx - 1];
      v[idx++] = last;
    }

    thin_mesh = (WireframeMesh){.verts = v, .vert_count = idx};
  }

  constexpr float thickness = 1.0f;

  // Each segment is two consecutive vertices in thin.verts
  const int segments = thin_mesh.vert_count / 2;
  const int max_verts = segments * 6;
  SDL_Vertex *v = SDL_malloc(sizeof(SDL_Vertex) * (unsigned int)max_verts);
  int idx = 0;

  for (int i = 0; i < segments; ++i) {
    // endpoints
    SDL_FPoint p1 = thin_mesh.verts[2 * i].position;
    SDL_FPoint p2 = thin_mesh.verts[2 * i + 1].position;
    // direction & perpendicular
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    const float len = SDL_sqrtf(dx * dx + dy * dy);
    if (len < 1e-6f)
      continue;
    dx /= len;
    dy /= len;
    const float nx = -dy * (thickness * 0.5f);
    const float ny = dx * (thickness * 0.5f);
    // quad corners
    const SDL_FPoint q1 = {p1.x + nx, p1.y + ny};
    const SDL_FPoint q2 = {p2.x + nx, p2.y + ny};
    const SDL_FPoint q3 = {p2.x - nx, p2.y - ny};
    const SDL_FPoint q4 = {p1.x - nx, p1.y - ny};
    // two triangles
    v[idx++] = (SDL_Vertex){q1, col, {0, 0}};
    v[idx++] = (SDL_Vertex){q2, col, {0, 0}};
    v[idx++] = (SDL_Vertex){q3, col, {0, 0}};
    v[idx++] = (SDL_Vertex){q3, col, {0, 0}};
    v[idx++] = (SDL_Vertex){q4, col, {0, 0}};
    v[idx++] = (SDL_Vertex){q1, col, {0, 0}};
  }

  // free the thin mesh
  SDL_free(thin_mesh.verts);
  return (WireframeMesh){.verts = v, .vert_count = idx};
}

/**
 *
 * @param renderer render targer
 * @param iso_x starting x position of the building in isometric coordinates
 * (game world)
 * @param iso_y starting y position of the building in isometric coordinates
 * (game world)
 * @param iso_w width of the isometric tile unit
 * @param iso_h height of the isometric tile unit
 * @param bw building width in isometric tile units (game world)
 * @param bl building length in isometric tile units (game world)
 * @param sw sprite width in visual tiles (spritesheet tile units)
 * @param sh sprite height in visual tiles (spritesheet tile units)
 */
void render_building_wireframe(SDL_Renderer *const SDL_RESTRICT renderer,
                               const float iso_x, const float iso_y,
                               const float iso_w, const float iso_h,
                               const int bw, const int bl, const int sw,
                               const int sh) {
  PROF_start(PROFILER_RENDER_WIREFRAMES);
  SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255);

  const float tile_h = iso_h * 2.0f;
  const float base_y = iso_y + tile_h * (float)sh;
  const float half_iso_w = iso_w * 0.5f;
  const float half_iso_h = iso_h * 0.5f;

  // float bw_n = (float)bw - 0.5f;
  for (int x = 0; x < bw; x++) {
    const float v_line_bottom = base_y - half_iso_h * (float)(bw - x);
    const float v_line_top = v_line_bottom - iso_h * (float)sh;
    const float v_line_x = iso_x + half_iso_w * (float)x;
    // vertical lines
    SDL_RenderLine(renderer, v_line_x, v_line_bottom, v_line_x, v_line_top);

    // top layer
    for (int y = 0; y <= sh; y++) {
      const float d_line_left = v_line_bottom - iso_h * (float)y;
      SDL_RenderLine(renderer, v_line_x, d_line_left, v_line_x + half_iso_w,
                     d_line_left + half_iso_h);
    }

    SDL_RenderLine(renderer, v_line_x, v_line_top,
                   v_line_x + half_iso_w * (float)bl,
                   v_line_top - iso_h * (float)bl * 0.5f);

    // bw_n -= 0.5f;
  }

  for (int x = 0; x <= bl; x++) {
    const float v_line_bottom = base_y - iso_h * (float)x * 0.5f;
    const float v_line_top = v_line_bottom - iso_h * (float)sh;
    const float v_line_x = iso_x + iso_w * ((float)(x + bw) * 0.5f);
    // vertical lines
    SDL_RenderLine(renderer, v_line_x, v_line_bottom, v_line_x, v_line_top);

    // top layer
    for (int y = 0; y <= sh && x < bl; y++) {
      const float d_line_left = v_line_bottom - iso_h * (float)y;
      SDL_RenderLine(renderer, v_line_x, d_line_left, v_line_x + iso_w * 0.5f,
                     d_line_left - iso_h * 0.5f);
    }

    if (x > 0)
      SDL_RenderLine(renderer, v_line_x, v_line_top,
                     v_line_x - iso_w * (float)bw * 0.5f,
                     v_line_top - iso_h * (float)bw * 0.5f);
  }

  // TODO finish the top later wireframe. rethink how to do that since 2x2
  // building size would make it not render completely I think.

  PROF_stop(PROFILER_RENDER_WIREFRAMES);
}

// TODO: test times with x buildings with restrict on map, ts and rs.

void render_buildings(SDL_Renderer *const renderer, const Map *const map,
                      const TransformComponent *const ts,
                      const RenderableComponent *const rs, const int b_count,
                      const float offset_x, const float offset_y) {
  PROF_start(PROFILER_RENDER_BUILDINGS);

  const int tile_w = (int)map->tileset->tile_width;
  const int tile_h = (int)map->tileset->tile_height;
  const float iso_w = (float)tile_w;
  const float iso_h = (float)tile_h / 2.0f;
  const float start_x = offset_x + (float)(map->height - 1) * iso_w / 2.0f;
  const float start_y = offset_y;

  for (int entity = 0; entity < b_count; entity++) {
    const int bw = rs[entity].sprite_w;
    const int bh = rs[entity].sprite_h;
    const int mx = ts[entity].x;
    const int my = ts[entity].y;

    SDL_FRect src = {
        (float)((rs[entity].tile_index % (int)map->tileset->columns) * tile_w),
        (float)((rs[entity].tile_index / (int)map->tileset->columns) *
                (int)tile_h),
        (float)(bw * tile_w), (float)(bh * tile_h)};

    float iso_x = start_x + (float)(mx - my) * (iso_w / 2.0f);
    float iso_y = start_y + (float)(mx + my) * (iso_h / 2.0f);

    iso_y -=
        (float)tile_h; // -tile_h because buildings are _on top_ of the terrain
    iso_y -= (float)bh * iso_h; // iso_h bc tile_h / 2

    // iso_x -= (float)bw * iso_w;
    iso_x -= (float)(buildings[entity].width - 1) * 0.5f * iso_w;

    SDL_FRect dst = {iso_x, iso_y, (float)bw * (float)tile_w,
                     (float)bh * (float)tile_h};

    SDL_RenderTexture(renderer, map->tileset->texture, &src, &dst);

    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_RenderRect(renderer, &(SDL_FRect){iso_x, iso_y, (float)(tile_w * bw),
                                          (float)(tile_h * bh)});

    SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255);

    if (wireframe_mode) {
      PROF_stop(PROFILER_RENDER_BUILDINGS);
      PROF_start(PROFILER_RENDER_WIREFRAMES);
      // render_building_wireframe(renderer, iso_x, iso_y, iso_w, iso_h,
      // buildings[entity].width, buildings[entity].length, bw, bh);
      const WireframeMesh *const m = &wireframe_meshes[entity];
      if (unlikely(false == SDL_RenderGeometry(renderer, nullptr, m->verts,
                                               m->vert_count, nullptr, 0))) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Error rendering geometry: %s",
                     SDL_GetError());
      }
      PROF_stop(PROFILER_RENDER_WIREFRAMES);
      PROF_start(PROFILER_RENDER_BUILDINGS);
    }
  }

  const SDL_FRect src = {
      (float)0,
      (float)((TILE_PLACEHOLDER_BOAT / map->tileset->columns) *
              (unsigned int)tile_w),
      (float)64, (float)96};

  const SDL_FRect dst = {(float)1080, (float)1080, (float)64, (float)96};
  SDL_SetTextureColorMod(map->tileset->texture, 255, 0, 0); // tint red
  SDL_SetTextureAlphaMod(map->tileset->texture,
                         200); // optional: semi-transparent
  SDL_RenderTexture(renderer, map->tileset->texture, &src, &dst);
  SDL_SetTextureColorMod(map->tileset->texture, 255, 255, 255);
  SDL_SetTextureAlphaMod(map->tileset->texture, 255);

  PROF_stop(PROFILER_RENDER_BUILDINGS);
}

// TODO:
//  - buildings (in general, trees also count, things that fill the map)
//  - bounds checking
//  - clickable entities (buildings, boats etc... even people????)
//  - use ECS to animate tiles (adding transform components or enabling them
//  whatever), first example would be animating the sea!
//  - make some kind of 'fixedUpdate' to run logic (transforms, economy, etc),
//  'ticks'.
static inline bool is_tile_free(const Map *const map, const SDL_Point point) {
  if (point.x < 0 || point.y < 0 || point.x >= map->width ||
      point.y >= map->height) {
    return false;
  }
  return !map->occupied[point.y * map->width + point.x];
}

SDL_Point screen_to_map_tile(const float mouse_x, const float mouse_y,
                             const Map *const map, const float offset_x,
                             const float offset_y /*, const float scale*/) {
  // Tile dimensions from the map's tileset (logical, before scaling)
  const float tile_pixel_width = (float)map->tileset->tile_width;
  const float tile_pixel_height = (float)map->tileset->tile_height;

  // Isometric dimensions used for projection (logical, before scaling)
  // iso_tile_width is the width of the base of the diamond tile.
  // iso_tile_height_step is the vertical distance moved on screen for each step
  // in cartesian Y.
  const float iso_tile_width = tile_pixel_width;
  const float iso_tile_height_step =
      tile_pixel_height / 2.0f; // This is `iso_h` in render_map

  // Half of these dimensions are used in the projection formulas
  const float half_iso_tile_width = iso_tile_width / 2.0f;
  const float half_iso_tile_height_step = iso_tile_height_step / 2.0f;

  // Calculate the logical origin point (anchor of tile 0,0) on the screen,
  // before scaling. This must exactly match the start_x and start_y calculation
  // in render_map.
  const float map_origin_logical_x =
      offset_x + ((float)(map->height - 1) * iso_tile_width) / 2.0f +
      half_iso_tile_width;
  const float map_origin_logical_y = offset_y;

  // Convert mouse coordinates from window (screen pixel) space to logical space
  // by dividing by the render scale.
  const float logical_mouse_x = mouse_x /* / scale */;
  const float logical_mouse_y = mouse_y /* / scale */;

  // Calculate mouse position relative to the map's logical origin.
  const float mouse_rel_to_origin_x = logical_mouse_x - map_origin_logical_x;
  const float mouse_rel_to_origin_y = logical_mouse_y - map_origin_logical_y;

  // Inverse isometric projection:
  // screen_x = (cart_x - cart_y) * half_iso_tile_width
  // screen_y = (cart_x + cart_y) * half_iso_tile_height_step
  //
  // Let A = cart_x - cart_y = mouse_rel_to_origin_x / half_iso_tile_width
  // Let B = cart_x + cart_y = mouse_rel_to_origin_y / half_iso_tile_height_step
  //
  // cart_x = (A + B) / 2
  // cart_y = (B - A) / 2

  const float term_a = mouse_rel_to_origin_x / half_iso_tile_width;
  const float term_b = mouse_rel_to_origin_y / half_iso_tile_height_step;

  const float cart_map_x_float = (term_a + term_b) / 2.0f;
  const float cart_map_y_float = (term_b - term_a) / 2.0f;

  // Convert floating point map coordinates to integer tile indices
  SDL_Point map_coords;
  map_coords.x = (int)SDL_floorf(cart_map_x_float);
  map_coords.y = (int)SDL_floorf(cart_map_y_float);

  return map_coords;
}

void destroy_tileset(Tileset *tileset) {
  if (tileset) {
    if (tileset->texture) {
      SDL_DestroyTexture(tileset->texture);
    }
    SDL_free(tileset);
  }
}

void destroy_map(Map *map) {
  if (map) {
    if (map->tiles) {
      SDL_free(map->tiles);
    }
    if (map->occupied) {
      SDL_free(map->occupied);
    }
    SDL_free(map);
  }
}

SDL_Window *window = nullptr;
SDL_Renderer *renderer = nullptr;
TTF_Font *font = nullptr;
#define MAP_SIZE_X 70
#define MAP_SIZE_Y 40
Tileset tileset;
Map map;
Entity main_camera;
ECSWorld ecs;

int initialize(void) {

  SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);

  // SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
  SDL_SetHint(SDL_HINT_RENDER_GPU_DEBUG, "1");

  if (SDL_Init(SDL_INIT_VIDEO) == false) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s\n",
                 SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL_Init failed",
                             SDL_GetError(), nullptr);
    return false;
  }

  if (TTF_Init() == false) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TTF_Init failed: %s\n",
                 SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TTF_Init failed",
                             SDL_GetError(), nullptr);
  }

  /* On Apple's macOS, **must** set the NSHighResolutionCapable Info. plist
   * property to YES, otherwise you will not receive a High-DPI OpenGL canvas.*/
  if (SDL_CreateWindowAndRenderer("NAU Engine", WINDOW_WIDTH, WINDOW_HEIGHT,
                                  SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                      SDL_WINDOW_RESIZABLE,
                                  &window, &renderer) == false) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "SDL_CreateWindowAndRenderer failed: %s\n", SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SDL_CreateRenderer failed",
                             SDL_GetError(), window);
    SDL_DestroyWindow(window);
    return false;
  }

  if (SDL_SetRenderVSync(renderer, SDL_RENDERER_VSYNC_DISABLED) ==
      false) { // 1 for VSYNC enabled
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "SDL_SetRenderVSync failed: %s\n", SDL_GetError());
  }
  // SDL_SetRenderLogicalPresentation(renderer, WINDOW_WIDTH, WINDOW_HEIGHT,
  // SDL_LOGICAL_PRESENTATION_LETTERBOX);
  float base_x, base_y;
  SDL_RenderCoordinatesFromWindow(renderer, 0.0f, 0.0f, &base_x, &base_y);

  SDL_GetWindowSizeInPixels(window, &screen_width, &screen_height);
  SDL_Log("Window real size: %d x %d", screen_width, screen_height);
  const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
  const SDL_DisplayMode *const display_mode =
      SDL_GetCurrentDisplayMode(display);
  SDL_Log("Display resolution: %d x %d", display_mode->w, display_mode->h);
  SDL_Log("Display DPI: %.2f", display_mode->pixel_density);
  // int window_w = (int)SDL_roundf((float)display_mode->w *
  // display_mode->pixel_density); int window_h =
  // (int)SDL_roundf((float)display_mode->h * display_mode->pixel_density); //I
  // think using height to calculate rendering quality is better, avoiding wide
  // screen miscalculations

  const float scale_y = (float)screen_height / WINDOW_HEIGHT;
  SDL_Log("Scale: %.2f", scale_y);

  SDL_Surface *icon_surface = IMG_Load("../../../../icon.png");
  if (icon_surface) {
    SDL_SetWindowIcon(window, icon_surface);
    SDL_DestroySurface(icon_surface);
  } else {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load icon: %s\n",
                 SDL_GetError());
  }

  font =
      TTF_OpenFont("/Users/arnau/Library/Fonts/JetBrainsMono-Regular.ttf", 24);
  if (!font) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TTF_OpenFont failed: %s\n",
                 SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TTF_OpenFont failed",
                             SDL_GetError(), window);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return false;
  }

  const Tileset *const ts =
      load_tileset(renderer, "../../../../isometric-sheet.png", 32, 32);
  if (ts == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load tileset");
    return false;
  }
  tileset = *ts;
  const Map *const mp = create_map(MAP_SIZE_X, MAP_SIZE_Y, &tileset);
  if (mp == nullptr) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create map");
    return false;
  }
  map = *mp;

  ecs = ECS_create();

  const Camera2D camera2d = {.position = {.x = 10.0f, .y = 10.0f},
                             .zoom = 2.0f,
                             .viewport = {0, 0, screen_width, screen_height},
                             .pixel_snap = true};

  main_camera = ECS_create_entity(&ecs);
  CAMERA_add(&ecs, main_camera, camera2d);

  // if (!load_boat(renderer, "../../../../pixel_ship.png", &boat)) {
  //     SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load boat");
  //     return false;
  // }

  return true;
}

void handle_events(void) {}

void update(void) {}

void render(void) {}

void clean(void) {

  ECS_destroy(&ecs);

  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Bye!", "see you!", window);
  SDL_Log("Application quit.\n");

  for (int i = 0; i < building_count; i++) {
    SDL_free(wireframe_meshes[i].verts);
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  TTF_Quit();
  SDL_Quit();
}

void spawn_boat(const int x, const int y) {
  const Camera2D_Component *main_camera_component =
      ss_get(&ecs.cameras, main_camera);
  constexpr int b_w = 1;
  constexpr int b_l = 3; // NOTE: to test building width and length

  SDL_Log("tile index at bpl: %d",
          map.tiles[hover_tile.x + hover_tile.y * map.width]);

  // if (map.tiles[hover_tile.x + hover_tile.y * map.width] ==
  // TILE_PLACEHOLDER_SEA) { spawn a boat instead
  renderables[building_count].tile_index = TILE_PLACEHOLDER_BOAT;
  renderables[building_count].sprite_w = 2;
  renderables[building_count].sprite_h = 3;
  SDL_Log("Boat placed at: %d, %d", hover_tile.x, hover_tile.y);
  // }
  // else {
  //     buildings[building_count].tile_index = TILE_PLACEHOLDER_BUILDING;
  //     SDL_Log("Building placed at: %d, %d", hover_tile.x, hover_tile.y);
  // }

  transforms[building_count].x = x;
  transforms[building_count].y = y;
  buildings[building_count].width = b_w;
  buildings[building_count].length = b_l;
  // buildings[building_count].type = TYPE_BOAT;
  // mark occupancy
  for (int dy = 0; dy > -b_l; dy--) {
    for (int dx = 0; dx > -b_w; dx--) {
      map.occupied[(hover_tile.y + dy) * map.width + (hover_tile.x + dx)] =
          true;
      map.tiles[(hover_tile.y + dy) * map.width + (hover_tile.x + dx)] =
          TILE_PLACEHOLDER_TERRAIN;
    }
  }

  // TODO: refactor this wireframe generation stuff into sprite loading
  constexpr float b_h_ = 3.0f;
  constexpr float b_w_ = 1.0f;
  const float tile_w = (float)map.tileset->tile_width;
  const float tile_h = (float)map.tileset->tile_height;
  const float iso_w = tile_w;
  const float iso_h = tile_h * 0.5f;
  const float start_x = main_camera_component->camera.position.x +
                        (float)(map.height - 1) * iso_w * 0.5f;
  const float start_y = main_camera_component->camera.position.y;
  const float iso_x =
      start_x + (float)(x - y) * iso_w * 0.5f - (b_w_ - 1) * iso_w * 0.5f;
  const float iso_y =
      start_y + (float)(x + y) * iso_h * 0.5f - tile_h - b_h_ * iso_h;
  wireframe_meshes[building_count] =
      build_wireframe_mesh(&map, iso_x, iso_y, iso_w, iso_h, 1, 3, 2, 3);

  building_count++;
}

void spawn_boats(const int amount, int *const restrict bldng_count,
                 int *const restrict map_tiles,
                 bool *const restrict map_occupied, const int map_width,
                 const int map_height) {
  // TODO: get logic from spawn from click and delete this...
  const Camera2D_Component *main_camera_component =
      ss_get(&ecs.cameras, main_camera);
  int count = 0;
  for (int y = map_height - 1; y >= 2; y -= 3) {
    for (int x = 0; x < map_width; x++) {
      // Check if the boat can fit in the current position
      if (unlikely(count >= amount || *bldng_count >= MAX_BUILDINGS)) {
        return;
      }
      if (likely(y - 2 < map_height && y - 1 < map_height &&
                 !map_occupied[y * map_width + x] &&
                 !map_occupied[(y - 1) * map_width + (x)] &&
                 !map_occupied[(y - 2) * map_width + (x)])) {

        // Place the boat tiles
        map_tiles[y * map_width + x] = TILE_PLACEHOLDER_TERRAIN;
        map_tiles[(y - 1) * map_width + (x)] = TILE_PLACEHOLDER_TERRAIN;
        map_tiles[(y - 2) * map_width + (x)] = TILE_PLACEHOLDER_TERRAIN;

        // Mark the tiles as occupied
        map_occupied[y * map_width + x] = true;
        map_occupied[(y - 1) * map_width + (x)] = true;
        map_occupied[(y - 2) * map_width + (x)] = true;

        // renderables
        renderables[*bldng_count].tile_index = TILE_PLACEHOLDER_BOAT;
        renderables[*bldng_count].sprite_w = 2;
        renderables[*bldng_count].sprite_h = 3;

        // components
        transforms[*bldng_count].x = x;
        transforms[*bldng_count].y = y;
        buildings[*bldng_count].width = 1;
        buildings[*bldng_count].length = 3;

        constexpr float b_h_ = 3.0f;
        constexpr float b_w_ = 1.0f;
        const float tile_w = (float)map.tileset->tile_width;
        const float tile_h = (float)map.tileset->tile_height;
        const float iso_w = tile_w;
        const float iso_h = tile_h * 0.5f;
        const float start_x = main_camera_component->camera.position.x +
                              (float)(map.height - 1) * iso_w * 0.5f;
        const float start_y = main_camera_component->camera.position.y;
        const float iso_x =
            start_x + (float)(x - y) * iso_w * 0.5f - (b_w_ - 1) * iso_w * 0.5f;
        const float iso_y =
            start_y + (float)(x + y) * iso_h * 0.5f - tile_h - b_h_ * iso_h;
        wireframe_meshes[building_count] =
            build_wireframe_mesh(&map, iso_x, iso_y, iso_w, iso_h, 1, 3, 2, 3);

        (*bldng_count)++;
        count++;
      }
    }
  }
}

int mainLoop(void) {

  // Initialize square properties
  SDL_FRect square = {375, 200, 100,
                      100}; // Red square starting at the center (50x50)
  SDL_Color square_color = {255, 0, 0, 255}; // Red color

  // Button properties
  const SDL_FRect button = {1440, 20, 100,
                            40}; // Top-right corner button (70x30)
  const SDL_Color button_color = {0, 0, 255, 255}; // Button color: blue

  char fps_text[64] =
      "Min: _TBD | Avg: _TBD | Max: _TBD"; // TODO: same ^^^^^^^^^^^^^^^^^^^

  int running = 1;
  float mouse_x = 0.0f, mouse_y = 0.0f;
  SDL_Point mouse_tile = {0, 0};
  while (running) {
    PROF_frameStart();
    constexpr float square_speed = 15.0f;

    PROF_start(PROFILER_EVENT_HANDLING);
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_EVENT_QUIT:
        SDL_LogDebug(SDL_LOG_CATEGORY_CUSTOM, "Quit event received\n");
        running = 0;
        break;
      case SDL_EVENT_KEY_DOWN:
        // SDL_LogDebug(SDL_LOG_CATEGORY_CUSTOM, "Key down: %#010x\n",
        // event.key.key);
        switch (event.key.key) {
        case SDLK_ESCAPE: {
          running = 0;
          break;
        }
        case SDLK_APOSTROPHE: {
          wireframe_mode = !wireframe_mode;
          break;
        }
        case SDLK_P: {
          debug_mode = !debug_mode;
          break;
        }
        case SDLK_PLUS: {
          spawn_boats(50, &building_count, map.tiles, map.occupied, map.width,
                      map.height);
          break;
        }
        case SDLK_Z: {
          Camera2D_Component *main_camera_component =
              ss_get(&ecs.cameras, main_camera);
          main_camera_component->camera =
              (Camera2D){.position = {0.0f, 0.0f},
                         .zoom = 1.0f,
                         .viewport = {0, 0, screen_width, screen_height},
                         .pixel_snap = true};
          break;
        }
        default: {
          break;
        }
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        // SDL_Log("Mouse button: %d", event.button.button);

        // Check if the button is clicked
        // SDL_SetRenderScale(renderer, map.view.scale, map.view.scale);
        // SDL_ConvertEventToRenderCoordinates(renderer, &event);

        if (event.button.button == SDL_BUTTON_LEFT) {

          if (is_point_in_rect(event.button.x, event.button.y, &button)) {
            // Toggle square color
            if (square_color.r == 255 && square_color.g == 0) {
              square_color = (SDL_Color){0, 255, 0, 255}; // Green
            } else {
              square_color = (SDL_Color){255, 0, 0, 255}; // Red
            }
            break;
          }

          // tilemap interaction
          if (hover_tile.x < map.width && hover_tile.y < map.height &&
              hover_tile.x >= 0 && hover_tile.y >= 0) {

            // place 'building'
            if (building_count < MAX_BUILDINGS) {
              int b_w = 1, b_l = 3; // NOTE: to test building width and length

              bool ok = true;
              for (int dy = 0; dy < b_l && ok; dy++)
                for (int dx = 0; dx < b_w; dx++)
                  if (!is_tile_free(&map, (SDL_Point){hover_tile.x - dx,
                                                      hover_tile.y - dy}))
                    ok = false;
              // TODO: in the middle of trying to spawn a boat. also need to
              // change building placement / rendering / occupancy
              //  so it places the building 'upwards', with the reference being
              //  the low-left corner instead of the top-left bc of coordinates
              if (ok) {
                spawn_boat(hover_tile.x, hover_tile.y);
              }
            }
            // paint the clicked tile
            // map.tiles[hover_tile.x + hover_tile.y * map.width] =
            // TILE_PLACEHOLDER_TERRAIN;
          }
        }
        break;

      case SDL_EVENT_MOUSE_MOTION: {
        // TODO: when checking for mouse actions:
        //  - first check for UI elements (render scale is UI_SCALE (1.0f for
        //  now)
        //  - then check for game world actions (render scale of map.view.scale)

        // SDL_SetRenderScale(renderer, 1.0f, 1.0f);
        // SDL_ConvertEventToRenderCoordinates(renderer, &event);

        // SDL_SetRenderScale(renderer, map.view.scale, map.view.scale);
        // SDL_ConvertEventToRenderCoordinates(renderer, &event);
        SDL_RenderCoordinatesFromWindow(renderer, event.motion.x,
                                        event.motion.y, &mouse_x, &mouse_y);

        // SDL_Log("Mouse motion buttons: " BYTE_TO_BINARY_PATTERN " - %d",
        // BYTE_TO_BINARY(event.motion.state), event.motion.state);

        Camera2D_Component *main_camera_component =
            ss_get(&ecs.cameras, main_camera);

        if (event.motion.state & SDL_BUTTON_MMASK) {
          main_camera_component->camera.position.x -= event.motion.xrel;
          main_camera_component->camera.position.y -= event.motion.yrel;
        }

        if (is_point_in_rect(mouse_x, mouse_y, &button)) {
          if (SDL_SetCursor(
                  SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER)) == false) {
            SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "SDL_SetCursor failed: %s\n",
                         SDL_GetError());
          }
        } else if (is_point_in_rect(event.motion.x, event.motion.y, &square)) {
          square_color = (SDL_Color){255, 100, 100, 255};
        } else {
          SDL_SetCursor(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT));
        }
        // mouse_x = event.motion.x;
        // mouse_y = event.motion.y;
        SDL_FPoint world_position = cam_screen_to_world(
            &main_camera_component->camera, mouse_x, mouse_y);
        mouse_tile = screen_to_map_tile(world_position.x, world_position.y,
                                        &map, 0.0f, 0.0f);
        hover_tile = mouse_tile;
      } break;

      case SDL_EVENT_MOUSE_WHEEL: {
        // TODO: depending on where the mouse is the action could change. for
        // this maybe we would need some kind of middleware that knows the
        // hovering item and the possible actions that can be performed on it.
        // stuff for the future i guess.

        // SDL_Log("Mouse wheel event (%d). X: %.2f, Y: %.2f, tX: %d, tY: %d",
        // event.wheel.direction, event.wheel.x, event.wheel.y,
        // event.wheel.integer_x, event.wheel.integer_y);

        // grab physical mouse pos
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        SDL_RenderCoordinatesFromWindow(renderer, mx, my, &mx, &my);

        const float zoom_direction =
            event.wheel.y > 0
                ? 1.0f
                : -1.0f; // positive away from user, negative towards user

        // TODO: review params (including impl 'magic' numbers) for nice zooming
        CAMERA_zoom_apply(&ecs, main_camera, zoom_direction, 0.02f);
        SDL_Log("Mouse wheel event: %.2f, %.2f", event.wheel.x, event.wheel.y);
        // Camera2D_Component *main_camera_component = ss_get(&ecs.cameras,
        // main_camera);
        //
        // const SDL_FPoint oldPosition =
        // cam_screen_to_world(&main_camera_component->camera, mx, my);
        //
        // main_camera_component->camera.zoom =
        // SDL_clamp(main_camera_component->camera.zoom + event.wheel.y * 0.05f,
        // min_zoom, max_zoom);
        //
        // const SDL_FPoint newPosition =
        // cam_screen_to_world(&main_camera_component->camera, mx, my);
        //
        // main_camera_component->camera.position.x += (oldPosition.x -
        // newPosition.x)/* * camera.zoom*/;
        // main_camera_component->camera.position.y += (oldPosition.y -
        // newPosition.y)/* * camera.zoom*/;
      } break;

        // case SDL_EVENT_MOUSE_WHEEL //TODO: mouse button is detected as mouse
        // down event, gotta check which button in there

      case SDL_EVENT_WINDOW_RESIZED: {
        SDL_GetWindowSizeInPixels(window, &screen_width, &screen_height);
        Camera2D_Component *main_camera_component =
            ss_get(&ecs.cameras, main_camera);

        main_camera_component->camera.viewport.w = screen_width;
        main_camera_component->camera.viewport.h = screen_height;
        SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO,
                    "Window resized to %dx%d, updated viewport to %dx%d",
                    event.window.data1, event.window.data2,
                    main_camera_component->camera.viewport.x,
                    main_camera_component->camera.viewport.y);
      } break;

      default:
        // SDL_Log("Unknwon event type: %#x", event.type);
        break;
      }
    }

    /*   -------------------------------------------     SYSTEMS
     * ------------------------------------------   */
    CAMERA_smooth_zoom_system(&ecs, PROF_getLastFrameTime(),
                              (SDL_FPoint){mouse_x, mouse_y});

    // Handle continuous key presses
    const bool *keyboard_state = SDL_GetKeyboardState(nullptr);
    if (keyboard_state[SDL_SCANCODE_RIGHT] || keyboard_state[SDL_SCANCODE_D]) {
      square.x += square_speed; // Move right
    } else if (keyboard_state[SDL_SCANCODE_LEFT] ||
               keyboard_state[SDL_SCANCODE_A]) {
      square.x -= square_speed; // Move left
    }
    if (keyboard_state[SDL_SCANCODE_DOWN] || keyboard_state[SDL_SCANCODE_S]) {
      square.y += square_speed; // Move down
    } else if (keyboard_state[SDL_SCANCODE_UP] ||
               keyboard_state[SDL_SCANCODE_W]) {
      square.y -= square_speed; // Move up
    }

    // Keep the square within screen boundaries
    if (square.x <= 0) {
      square.x = 0;
    }
    if (square.x + square.w >= (float)screen_width) {
      square.x = (float)screen_width - square.w;
    }
    if (square.y <= 0) {
      square.y = 0;
    }
    if (square.y + square.h >= (float)screen_height) {
      square.y = (float)screen_height - square.h;
    }
    PROF_stop(PROFILER_EVENT_HANDLING);

    // background color
    SDL_SetRenderDrawColor(renderer, 135, 206, 235, 255);
    SDL_RenderClear(renderer);

    float scale, offx, offy;
    Camera2D_Component *main_camera_component =
        ss_get(&ecs.cameras, main_camera);
    cam_get_render_params(&main_camera_component->camera, &scale, &offx, &offy);

    SDL_SetRenderScale(renderer, scale, scale);
    render_map(renderer, &map, offx, offy);
    render_buildings(renderer, &map, transforms, renderables, building_count,
                     offx, offy);
    SDL_SetRenderScale(renderer, 1.f, 1.f);

    PROF_start(PROFILER_RENDER_UI);
    // Render the square
    SDL_SetRenderDrawColor(renderer, square_color.r, square_color.g,
                           square_color.b, square_color.a);
    SDL_RenderFillRect(renderer, &square);
    // SDL_RenderRoundedRect(renderer, square, 30.0f, square_color);

    // Render the button
    SDL_SetRenderDrawColor(renderer, button_color.r, button_color.g,
                           button_color.b, button_color.a);
    SDL_RenderFillRect(renderer, &button);

    // Render button text //TODO: not showing the text anymore lol
    const SDL_Color text_color = {255, 255, 255, 255}; // White text
    SDL_Surface *button_surface = TTF_RenderText_LCD(
        font, "Toggle", strlen("Toggle"), text_color, (SDL_Color){0, 0, 0, 0});
    SDL_Texture *button_texture =
        SDL_CreateTextureFromSurface(renderer, button_surface);

    const SDL_FRect text_rect = {button.x + 10, button.y + 5,
                                 (float)button_surface->w,
                                 (float)button_surface->h};
    SDL_RenderTexture(renderer, button_texture, nullptr, &text_rect);

    SDL_DestroySurface(button_surface);

    char mouse_text[40] = " Mouse position: __, __ ";
    snprintf(mouse_text, 40, " Mouse position: %.2f, %.2f ", mouse_x, mouse_y);

    SDL_Surface *const mousepos_surface =
        TTF_RenderText_LCD(font, mouse_text, strlen(mouse_text), text_color,
                           (SDL_Color){0, 0, 0, 125});
    if (!mousepos_surface) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "TTF_RenderText_Blended failed: %s\n", SDL_GetError());
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                               "TTF_RenderText_Blended failed", SDL_GetError(),
                               window);
      TTF_CloseFont(font);
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
      return 1;
    }
    SDL_Texture *const mouse_texture =
        SDL_CreateTextureFromSurface(renderer, mousepos_surface);

    SDL_FRect mouse_rect = {20.0f, 60.0f, (float)mousepos_surface->w,
                            (float)mousepos_surface->h};
    SDL_RenderTexture(renderer, mouse_texture, nullptr, &mouse_rect);

    SDL_DestroySurface(mousepos_surface);

    char mouse_tile_text[32] = " Mouse on tile: __, __ ";
    snprintf(mouse_tile_text, 32, " Mouse on tile: %d, %d ", mouse_tile.x,
             mouse_tile.y);

    SDL_Surface *const mousetile_surface =
        TTF_RenderText_LCD(font, mouse_tile_text, strlen(mouse_tile_text),
                           text_color, (SDL_Color){0, 0, 0, 125});
    if (!mousetile_surface) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "TTF_RenderText_Blended failed: %s\n", SDL_GetError());
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                               "TTF_RenderText_Blended failed", SDL_GetError(),
                               window);
      TTF_CloseFont(font);
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
      return 1;
    }
    SDL_Texture *const mousetile_texture =
        SDL_CreateTextureFromSurface(renderer, mousetile_surface);

    SDL_FRect mousetile_rect = {20.0f, 100.0f, (float)mousetile_surface->w,
                                (float)mousetile_surface->h};
    SDL_RenderTexture(renderer, mousetile_texture, nullptr, &mousetile_rect);

    SDL_DestroySurface(mousetile_surface);

    char camera_scale_text[22] = " Camera zoom: __ ";
    snprintf(camera_scale_text, 22, " Camera zoom: %.2f ",
             main_camera_component->camera.zoom);

    SDL_Surface *const camera_scale_surface =
        TTF_RenderText_LCD(font, camera_scale_text, strlen(camera_scale_text),
                           text_color, (SDL_Color){0, 0, 0, 125});
    if (!camera_scale_surface) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "TTF_RenderText_Blended failed: %s\n", SDL_GetError());
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                               "TTF_RenderText_Blended failed", SDL_GetError(),
                               window);
      TTF_CloseFont(font);
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
      return 1;
    }
    SDL_Texture *const camera_scale_texture =
        SDL_CreateTextureFromSurface(renderer, camera_scale_surface);

    SDL_FRect camera_scale_rect = {20.0f, 140.0f,
                                   (float)camera_scale_surface->w,
                                   (float)camera_scale_surface->h};
    SDL_RenderTexture(renderer, camera_scale_texture, nullptr,
                      &camera_scale_rect);

    SDL_DestroySurface(camera_scale_surface);

    char mouse_coords_text[64] = " __, __ ";
    snprintf(mouse_coords_text, 64, " %.2f, %.2f ",
             mouse_x * main_camera_component->camera.zoom,
             mouse_y * main_camera_component->camera.zoom);

    SDL_Surface *const mousecoords_surface =
        TTF_RenderText_LCD(font, mouse_coords_text, strlen(mouse_coords_text),
                           text_color, (SDL_Color){0, 0, 0, 125});
    if (!mousecoords_surface) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "TTF_RenderText_Blended failed: %s\n", SDL_GetError());
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                               "TTF_RenderText_Blended failed", SDL_GetError(),
                               window);
      TTF_CloseFont(font);
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
      return 1;
    }
    SDL_Texture *const mousecoords_texture =
        SDL_CreateTextureFromSurface(renderer, mousecoords_surface);

    SDL_FRect mousecoords_rect = {mouse_x + 20.0f, mouse_y - 20.0f,
                                  (float)mousecoords_surface->w,
                                  (float)mousecoords_surface->h};
    SDL_RenderTexture(renderer, mousecoords_texture, nullptr,
                      &mousecoords_rect);

    SDL_DestroySurface(mousecoords_surface);

    float fps_min, fps_avg, fps_max;
    PROF_getFPS(&fps_min, &fps_avg, &fps_max);
    snprintf(fps_text, 64, " Min: %.2f | Avg: %.2f | Max: %.2f ", fps_min,
             fps_avg, fps_max);
    SDL_Surface *const fps_surface =
        TTF_RenderText_LCD(font, fps_text, strlen(fps_text), text_color,
                           (SDL_Color){0, 0, 0, 125});
    if (!fps_surface) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                   "TTF_RenderText_Blended failed: %s\n", SDL_GetError());
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                               "TTF_RenderText_Blended failed", SDL_GetError(),
                               window);
      TTF_CloseFont(font);
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
      return 1;
    }
    SDL_Texture *const fps_texture =
        SDL_CreateTextureFromSurface(renderer, fps_surface);

    SDL_FRect fps_rect = {20.0f, 20.0f, (float)fps_surface->w,
                          (float)fps_surface->h};
    SDL_RenderTexture(renderer, fps_texture, nullptr, &fps_rect);

    SDL_DestroySurface(fps_surface);

    PROF_stop(PROFILER_RENDER_UI);

    /* ------------ PROFILING UI ------------ */
    PROF_start(PROFILER_GPU);
    if (debug_mode)
      PROF_render(renderer, font, (SDL_FPoint){20.0f, 180.0f});

    SDL_RenderPresent(renderer);

    SDL_DestroyTexture(button_texture);
    SDL_DestroyTexture(mouse_texture);
    SDL_DestroyTexture(mousetile_texture);
    SDL_DestroyTexture(camera_scale_texture);
    SDL_DestroyTexture(mousecoords_texture);
    SDL_DestroyTexture(fps_texture);

    PROF_stop(PROFILER_GPU);

    // SDL_Delay(1);

    // TODO: rethink this, but at least move it to the end of the frame AFTER
    // RenderPresent(). take fps times from PROF? consistency!
    /* ------------ WAIT FRAME - FPS COUNTER ------------ */
    PROF_start(PROFILER_WAIT_FRAME);

    const Uint32 wait_time = (Uint32)PROF_getFrameWaitTime();

    if (wait_time >= 1) {
      SDL_Delay(wait_time - 1);
    }

    float dt = PROF_getLastFrameTime() / 1000.0f;
    sea_time += dt * SDL_PI_F * 2.0f * WAVE_SPEED;

    // SDL_LogDebug(SDL_LOG_CATEGORY_CUSTOM, "FPS: %f\n", fps);

    PROF_stop(PROFILER_WAIT_FRAME);

    PROF_frameEnd();
  }

  return 0;
}

int main(void) {
  atexit(SDL_Quit);

  if (initialize() == false) {
    return EXIT_FAILURE;
  }

  const int result = mainLoop();
  if (result != EXIT_SUCCESS) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Application exited with error code: %d\n", result);
    return result;
  }

  clean();

  return EXIT_SUCCESS;
}
