#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>

#include "camera/camera.h"
#include "profiler.h"
#include "renderer/renderer.h"
#include "renderer/ui.h"

#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080
static int screen_width = WINDOW_WIDTH;
static int screen_height = WINDOW_HEIGHT;
static float pixel_ratio = 0.0f;


int is_point_in_rect(const float x, const float y,
                     const SDL_FRect *const SDL_RESTRICT rect) {
  return (x >= rect->x && x <= rect->x + rect->w && y >= rect->y &&
          y <= rect->y + rect->h);
}

#define TILE_SIZE 32

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

static RenderableComponent renderables[MAX_BUILDINGS]; //TODO: think about allocations etc...
static TransformComponent transforms[MAX_BUILDINGS];
static BuildingComponent buildings[MAX_BUILDINGS];
static int building_count;

static bool wireframe_mode = false;
static bool debug_mode = false;
static bool vsync = true;

typedef struct Tileset_ {
    SDL_GPUTexture *texture;    // The tileset image (GPU)
    unsigned int tile_width;    // Width of each tile (32px)
    unsigned int tile_height;   // Height of each tile (32px)
    unsigned int columns;       // Number of columns in the tileset
    unsigned int rows;          // Number of rows in the tileset
    unsigned int total_tiles;   // Total number of tiles
} Tileset;

/**
 * Map information including size, logical representation, rendering info, and view info
 */
typedef struct Map_ {
    int *tiles;                 // Array of tile indices
    int width;                  // Map width in tiles
    int height;                 // Map height in tiles
    bool *occupied;             // Building occupancy
    Tileset *tileset;           // Reference to the tileset
} Map;

// typedef struct Boat_ {
//     int x, y;
//     SDL_Texture *sprite;
// } Boat;

// bool load_boat(SDL_Renderer *const renderer, const char *const image_path, Boat *const boat) {
//
//     SDL_Surface *surface = IMG_Load(image_path);
//     if (!surface) {
//         SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load boat image: %s", SDL_GetError());
//         return false;
//     }
//
//     boat->sprite = SDL_CreateTextureFromSurface(renderer, surface);
//     SDL_DestroyTexture(boat->sprite);
//
//     if (!boat->sprite) {
//         SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create boat texture: %s", SDL_GetError());
//         return false;
//     }
//
//     SDL_SetTextureScaleMode(boat->sprite, SDL_SCALEMODE_NEAREST);
//
//     return true;
// }

Tileset *load_tileset(const char *const SDL_RESTRICT image_path,
                      const unsigned int tile_width, const unsigned int tile_height)
{
    // Allocate tileset structure
    Tileset *const SDL_RESTRICT tileset = SDL_malloc(sizeof(Tileset));
    if (!tileset) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for tileset");
        return nullptr;
    }

    // Load the texture using the new renderer
    tileset->texture = Renderer_LoadTexture(image_path);
    if (!tileset->texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create texture from %s", image_path);
        SDL_free(tileset);
        return nullptr;
    }

    // Calculate tileset dimensions
    // Note: using IMG_Load here ONLY to get dimensions, then free it. It's init time, so it's fine for now.

    SDL_Surface *surface = IMG_Load(image_path);
    if (surface) {
        tileset->columns = surface->w / tile_width;
        tileset->rows = surface->h / tile_height;
        SDL_DestroySurface(surface);
    } else {
        tileset->columns = 0;
        tileset->rows = 0;
    }

    tileset->tile_width = tile_width;
    tileset->tile_height = tile_height;
    tileset->total_tiles = tileset->columns * tileset->rows;

    SDL_Log("Loaded tileset: %d×%d tiles, %d columns, %d rows, %d total tiles",
            tile_width, tile_height, tileset->columns, tileset->rows, tileset->total_tiles);

    return tileset;
}

Map* create_map(const int width, const int height, Tileset *const SDL_RESTRICT tileset) {
    Map *const map = SDL_malloc(sizeof(Map));
    if (!map) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for map");
        return nullptr;
    }

    map->tiles = (int *) SDL_malloc((unsigned int)width * (unsigned int)height * sizeof(int));
    if (!map->tiles) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for map data");
        SDL_free(map);
        return nullptr;
    }

    map->occupied = (bool *) SDL_malloc((unsigned int)width * (unsigned int)height * sizeof(bool));
    if (!map->occupied) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for map data");
        SDL_free(map->tiles);
        SDL_free(map);
        return nullptr;
    }

    //initialize even on empty, odd on first tile
    SDL_memset(map->occupied, false, (unsigned int)width * (unsigned int)height * sizeof(bool));
    for (int i = 0; i < (width * height); i++) {
        int tile_index = i % 2 ? 0 : 36;
        if (i > ((width * height) - (width / 2) * (height / 2))) {
            tile_index = TILE_PLACEHOLDER_SEA;
        }
        map->tiles[i] = tile_index;
        if (tile_index == TILE_PLACEHOLDER_SEA) {
            //map->occupied[i] = true; //NOTE: commented to allow putting boats on water for now, as I'm considering them buildings to test
        }
    }

    map->width = width;
    map->height = height;
    map->tileset = tileset;

    return map;
}

SDL_Point hover_tile = {-1, -1};


//TODO: idea, weather conditions like in P3, can affect this! also refine wave math formula so it's more like a real sea wave
static float sea_time = 0.0f;
#define WAVE_SPEED 0.2f //how fast does the wave cycle go
#define WAVE_AMPLITUDE 0.5f //how high are the waves (amplitude) in terms of tile height
#define WAVE_PHASE 0.1f //how 'wide' are the waves (period) //TODO: in terms of tile width, not done yet.

void render_map(const Map *const map, const float offset_x, const float offset_y) {
    PROF_start(PROFILER_RENDER_MAP);

    const float tile_w = (float)map->tileset->tile_width;
    const float tile_h = (float)map->tileset->tile_height;

    const float iso_w = tile_w;
    const float iso_h = tile_h / 2.0f;

    // start position for rendering
    const float start_x = offset_x + ((float) (map->height - 1) * iso_w) / 2;
    const float start_y = offset_y;

    // TODO: Optimize this to not re-allocate every frame if map is static.
    const int max_tiles = map->width * map->height;
    SpriteInstance *instances = SDL_malloc(sizeof(SpriteInstance) * max_tiles);
    int instance_count = 0;

    // loop through all tiles in the map
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            const int tile_index = map->tiles[y * map->width + x];
            if (tile_index < 0)
                continue;

            // calculate isometric position
            const float iso_x = start_x + (float) (x - y) * iso_w / 2.0f;
            float tile_y_offset = 0.0f;

            if (tile_index == TILE_PLACEHOLDER_SEA) {
                const float phase = (float) (x + y) * WAVE_PHASE;
                const float adjusted_amplitude = (iso_h / 2.0f) * WAVE_AMPLITUDE;
                const float wave_offset_compensation =
                        ((iso_h / 2.0f) * WAVE_AMPLITUDE) + (iso_h / 2.0f);
                tile_y_offset = SDL_sinf(sea_time + phase) * adjusted_amplitude -
                                wave_offset_compensation;
            }
            const float iso_y =
                    start_y + ((float) (x + y) * (iso_h / 2.0f)) - tile_y_offset;

            // UV Coordinates
            const int col = tile_index % map->tileset->columns;
            const int row = tile_index / map->tileset->columns;

            const float tex_w =
                    (float) (map->tileset->columns * map->tileset->tile_width);
            const float tex_h =
                    (float) (map->tileset->rows * map->tileset->tile_height);

            const float u = (float) (col * map->tileset->tile_width) / tex_w;
            const float v = (float) (row * map->tileset->tile_height) / tex_h;
            const float uw = (float) map->tileset->tile_width / tex_w;
            const float vh = (float) map->tileset->tile_height / tex_h;

            // Z-Sorting: Depth based on Y (painter's algorithm); map (x+y) to Z. In isometric view, tiles with
            // higher (x+y) are closer to bottom (camera) with "<=" depth test: Z=0 is front, Z=1 is back; so we
            // flip it: high (x+y) -> low depth (front).
            const float depth = 1.0f - (float) (x + y) / (float) (map->width + map->height);

            instances[instance_count++] = (SpriteInstance){
                .x = iso_x,
                .y = iso_y,
                .z = depth,
                .w = tile_w,
                .h = tile_h,
                .u = u,
                .v = v,
                .uw = uw,
                .vh = vh
            };
        }
    }

    Renderer_DrawSprites(map->tileset->texture, instances, instance_count);
    SDL_free(instances);

    PROF_stop(PROFILER_RENDER_MAP);
}

typedef struct WireframeMesh_ {
    SDL_Vertex *verts;
    int vert_count;
} WireframeMesh;

static WireframeMesh wireframe_meshes[MAX_BUILDINGS];

// --- helper to build and cache a mesh ---
static WireframeMesh build_wireframe_mesh(
    const Map *const SDL_RESTRICT map,
    const float iso_x, const float iso_y,
    const float iso_w, const float iso_h,
    const int bw, const int bl,
    const int sw, const int sh)
{
    const float half_w = iso_w*0.5f;
    const float half_h = iso_h*0.5f;
    const float tile_h = iso_h*2.0f;
    const float base_y = iso_y + tile_h*(float)sh;
    const SDL_FColor col = {0,255,255,255};
    WireframeMesh thin_mesh;

    {
        // estimate max lines and allocate twice as many vertices, aligned to a multiple of 3, for rendering constraints
        const int max_lines = (bw + bl) + bw*(sh+1) + bl*(sh+1) + (bw+1) * (bl+1);
        const int num_vertices = max_lines * 2 + (3 - ((max_lines * 2) % 3));
        SDL_Vertex *v = SDL_malloc(sizeof(SDL_Vertex) * (unsigned int)num_vertices);
        int idx = 0;

        // width-wise lines
        for (int x = 0; x < bw; x++) {
            const float vx = iso_x + half_w * (float)x;
            const float vb = base_y - half_h * (float)(bw-x);
            const float vt = vb - iso_h*(float)sh;
            // vertical
            v[idx++] = (SDL_Vertex){{vx,vb}, col,{0,0}};
            v[idx++] = (SDL_Vertex){{vx,vt}, col,{0,0}};
            // top faces
            for (int y = 0; y <= sh; y++) {
                const float dy = vb - iso_h*(float)y;
                v[idx++] = (SDL_Vertex){{vx,         dy},      col,{0,0}};
                v[idx++] = (SDL_Vertex){{vx+half_w,  dy+half_h},col,{0,0}};
            }
        }
        // length-wise lines
        for (int x = 0; x <= bl; x++) {
            const float vx = iso_x + iso_w * (float)(bw+x) * 0.5f;
            const float vb = base_y - iso_h * 0.5f * (float)x;
            const float vt = vb - iso_h * (float)sh;
            v[idx++] = (SDL_Vertex){{vx,vb}, col,{0,0}};
            v[idx++] = (SDL_Vertex){{vx,vt}, col,{0,0}};
            if (x < bl) {
                for (int y = 0; y <= sh; y++) {
                    const float dy = vb - iso_h*(float)y;
                    v[idx++] = (SDL_Vertex){{vx,           dy},         col,{0,0}};
                    v[idx++] = (SDL_Vertex){{vx+half_w,    dy-half_h},   col,{0,0}};
                }
            }
        }

        //roof side
        const SDL_FPoint roof_fl = {
            iso_x,
            base_y - half_h*(float)bw - iso_h*(float)sh
        }; // front-left
        const SDL_FPoint roof_fr = {
            iso_x + half_w*(float)bw,
            base_y - half_h*(float)bw - iso_h*(float)sh + half_h*(float)bw
        }; // front-right
        const SDL_FPoint roof_bl = {
            iso_x + half_w*(float)bl,
            roof_fl.y - half_h*(float)bl
        }; // back-left
        const SDL_FPoint roof_br = {
            iso_x + half_w*(bw+bl),
            roof_fr.y - half_h*bl
        }; // back-right

        const SDL_FPoint dv_len   = { half_w, -half_h };  // length direction
        const SDL_FPoint dv_width = { half_w,  half_h };  // width direction

        // lines parallel to length (splitting width units on the roof)
        for (int i = 1; i <= bl; i++) {
            SDL_FPoint p0 = {
                roof_fl.x + dv_len.x * (float) i,
                roof_fl.y + dv_len.y * (float) i
            };
            SDL_FPoint p1 = {
                roof_fr.x + dv_len.x * (float) i,
                roof_fr.y + dv_len.y * (float) i
            };
            v[idx++] = (SDL_Vertex){p0, col, {0, 0}};
            v[idx++] = (SDL_Vertex){p1, col, {0, 0}};
        }

        // lines parallel to width (splitting length units on the roof)
        for (int j = 0; j < bw; j++) {
            SDL_FPoint p0 = {
                roof_fl.x + dv_width.x * (float) j,
                roof_fl.y + dv_width.y * (float) j
            };
            SDL_FPoint p1 = {
                roof_bl.x + dv_width.x * (float) j,
                roof_bl.y + dv_width.y * (float) j
            };
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

    // Each segment is two consecutive vertices in thin.verts
    const int segments = thin_mesh.vert_count / 2;
    const int max_verts = segments * 6;
    SDL_Vertex *v = SDL_malloc(sizeof(SDL_Vertex) * (unsigned int) max_verts);
    int idx = 0;

    for (int i = 0; i < segments; ++i) {
        constexpr float thickness = 1.0f;
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

void render_buildings(const Map *const map, const TransformComponent *const ts,
                      const RenderableComponent *const rs, const int b_count,
                      const float offset_x, const float offset_y) {
    PROF_start(PROFILER_RENDER_BUILDINGS);

    const int tile_w = (int) map->tileset->tile_width;
    const int tile_h = (int) map->tileset->tile_height;
    const float iso_w = (float) tile_w;
    const float iso_h = (float) tile_h / 2.0f;
    const float start_x = offset_x + (float) (map->height - 1) * iso_w / 2.0f;
    const float start_y = offset_y;

    SpriteInstance *instances =
            SDL_malloc(sizeof(SpriteInstance) * (b_count + 1)); // +1 for boat
    int instance_count = 0;

    const float tex_w = (float) (map->tileset->columns * map->tileset->tile_width);
    const float tex_h = (float) (map->tileset->rows * map->tileset->tile_height);

    for (int entity = 0; entity < b_count; entity++) {
        const int bw = rs[entity].sprite_w;
        const int bh = rs[entity].sprite_h;
        const int mx = ts[entity].x;
        const int my = ts[entity].y;

        float iso_x = start_x + (float) (mx - my) * (iso_w / 2.0f);
        float iso_y = start_y + (float) (mx + my) * (iso_h / 2.0f);

        iso_y -= (float) tile_h;
        iso_y -= (float) bh * iso_h;
        iso_x -= (float) (buildings[entity].width - 1) * 0.5f * iso_w;

        // UVs
        const int col = rs[entity].tile_index % map->tileset->columns;
        const int row = rs[entity].tile_index / map->tileset->columns;
        const float u = (float) (col * tile_w) / tex_w;
        const float v = (float) (row * tile_h) / tex_h;
        const float uw = (float) (bw * tile_w) / tex_w;
        const float vh = (float) (bh * tile_h) / tex_h;

        // Depth: same logic as tiles but with small bias so buildings appear in front;
        // invert so higher (mx+my) = lower depth = closer to camera
        const float depth =
                1.0f - (float) (mx + my) / (float) (map->width + map->height) - 0.001f;

        instances[instance_count++] = (SpriteInstance){
            .x = iso_x,
            .y = iso_y,
            .z = depth,
            .w = (float) (bw * tile_w),
            .h = (float) (bh * tile_h),
            .u = u,
            .v = v,
            .uw = uw,
            .vh = vh
        };
    }

    Renderer_DrawSprites(map->tileset->texture, instances, instance_count);
    SDL_free(instances);

    PROF_stop(PROFILER_RENDER_BUILDINGS);
}

//TODO:
// - buildings (in general, trees also count, things that fill the map)
// - bounds checking
// - clickable entities (buildings, boats etc... even people????)
// - use ECS to animate tiles (adding transform components or enabling them whatever), first example would be animating the sea!
// - make some kind of 'fixedUpdate' to run logic (transforms, economy, etc), 'ticks'.
static inline bool is_tile_free(const Map *const map, const SDL_Point point) {
    if (point.x < 0 || point.y < 0 || point.x >= map->width || point.y >= map->height) {
        return false;
    }
    return !map->occupied[point.y * map->width + point.x];
}

SDL_Point screen_to_map_tile(const float mouse_x, const float mouse_y,
                             const Map *const map, const float offset_x,
                             const float offset_y /*, const float scale*/) {
    // Tile dimensions from the map's tileset (logical, before scaling)
    const float tile_pixel_width = (float) map->tileset->tile_width;
    const float tile_pixel_height = (float) map->tileset->tile_height;

    // Isometric dimensions used for projection (logical, before scaling)
    // iso_tile_width is the width of the base of the diamond tile.
    // iso_tile_height_step is the vertical distance moved on screen for each step in cartesian Y.
    const float iso_tile_width = tile_pixel_width;
    const float iso_tile_height_step = tile_pixel_height / 2.0f; // This is `iso_h` in render_map

    // Half of these dimensions are used in the projection formulas
    const float half_iso_tile_width = iso_tile_width / 2.0f;
    const float half_iso_tile_height_step = iso_tile_height_step / 2.0f;

    // Calculate the logical origin point (anchor of tile 0,0) on the screen, before scaling.
    // This must exactly match the start_x and start_y calculation in render_map.
    const float map_origin_logical_x = offset_x + ((float)(map->height - 1) * iso_tile_width) / 2.0f + half_iso_tile_width;
    const float map_origin_logical_y = offset_y;

    // Convert mouse coordinates from window (screen pixel) space to logical space
    // by dividing by the render scale.
    const float logical_mouse_x = mouse_x /* / scale */;
    const float logical_mouse_y = mouse_y /* / scale */;

    // Calculate mouse position relative to the map's logical origin.
    const float mouse_rel_to_origin_x = logical_mouse_x - map_origin_logical_x;
    const float mouse_rel_to_origin_y = logical_mouse_y - map_origin_logical_y;

    // Inverse isometric projection:
    const float term_a = mouse_rel_to_origin_x / half_iso_tile_width;
    const float term_b = mouse_rel_to_origin_y / half_iso_tile_height_step;

    const float cart_map_x_float = (term_a + term_b) / 2.0f;
    const float cart_map_y_float = (term_b - term_a) / 2.0f;

    // Convert floating point map coordinates to integer tile indices
    SDL_Point map_coords;
    map_coords.x = (int) SDL_floorf(cart_map_x_float);
    map_coords.y = (int) SDL_floorf(cart_map_y_float);

    return map_coords;
}

void destroy_tileset(Tileset *tileset) {
    if (tileset) {
        // Renderer_DestroyTexture(tileset->texture); // TODO: Implement this
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
TTF_Text *fps_text = nullptr;

#define MAP_SIZE_X 70
#define MAP_SIZE_Y 40
Tileset tileset;
Map map;
Entity main_camera;
ECSWorld ecs;

int initialize(void) {
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);
    SDL_SetHint(SDL_HINT_RENDER_GPU_DEBUG, "1");

    if (SDL_Init(SDL_INIT_VIDEO) == false) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    if (TTF_Init() == false) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TTF_Init failed: %s\n", SDL_GetError());
    }

    /* On Apple's macOS, **must** set the NSHighResolutionCapable Info. plist
     * property to YES, otherwise you will not receive a High-DPI OpenGL canvas.*/
    window = SDL_CreateWindow("NAU Engine", WINDOW_WIDTH, WINDOW_HEIGHT,
                             SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    if (!Renderer_Init(window)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Renderer_Init failed");
        return false;
    }

    SDL_GetWindowSizeInPixels(window, &screen_width, &screen_height);
    SDL_Log("Window real size: %d x %d", screen_width, screen_height);
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    const SDL_DisplayMode *const display_mode = SDL_GetCurrentDisplayMode(display);
    SDL_Log("Display resolution: %d x %d", display_mode->w, display_mode->h);
    SDL_Log("Display DPI: %.2f", display_mode->pixel_density);
    pixel_ratio = display_mode->pixel_density;


    SDL_Surface *icon_surface = IMG_Load("icon.png");
    if (icon_surface) {
        SDL_SetWindowIcon(window, icon_surface);
        SDL_DestroySurface(icon_surface);
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load icon: %s\n", SDL_GetError());
    }

    font =
            TTF_OpenFont("/Users/arnau/Library/Fonts/JetBrainsMono-Regular.ttf", 24);
    if (!font) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TTF_OpenFont failed: %s\n", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TTF_OpenFont failed", SDL_GetError(), window);
        SDL_DestroyWindow(window);
        return false;
    }

    TTF_TextEngine *engine = Renderer_GetTextEngine();
    if (engine) {
        fps_text = TTF_CreateText(engine, font, "FPS: 0", 0);
        PROF_initUI(engine, font);
    }

    UI_Init();

    const Tileset *const ts = load_tileset("isometric-sheet.png", TILE_SIZE, TILE_SIZE);
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

    int w_w, w_h;
    SDL_GetWindowSizeInPixels(window, &w_w, &w_h);
    const Camera2D camera2d = {
        .position = {.x = 10.0f, .y = 10.0f},
        .zoom = 2.0f,
        .viewport = {0, 0, w_w, w_h},
        .pixel_snap = true
    };

    main_camera = ECS_create_entity(&ecs);
    CAMERA_add(&ecs, main_camera, camera2d);

    return true;
}

void handle_events(void) {
}

void update(void) {
}

void clean(void) {
    ECS_destroy(&ecs);

    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Bye!", "see you!", window);
    SDL_Log("Application quit.\n");

    for (int i = 0; i < building_count; i++) {
        SDL_free(wireframe_meshes[i].verts);
    }

    if (fps_text)
        TTF_DestroyText(fps_text);

    PROF_deinitUI();
    UI_Shutdown();
    Renderer_Shutdown();
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
}

// TODO: make sure not to spawn boats or buildings out of bounds (even partially)
void spawn_boat(const int x, const int y) {
    //const Camera2D_Component *main_camera_component = ss_get(&ecs.cameras, main_camera);
    constexpr int b_w = 1;
    constexpr int b_l = 3; //NOTE: to test building width and length

    SDL_Log("tile index at bpl: %d", map.tiles[hover_tile.x + hover_tile.y * map.width]);

    //if (map.tiles[hover_tile.x + hover_tile.y * map.width] == TILE_PLACEHOLDER_SEA) {
        //spawn a boat instead
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
            map.occupied[(hover_tile.y+dy)*map.width + (hover_tile.x+dx)] = true;
            map.tiles[(hover_tile.y+dy)*map.width + (hover_tile.x+dx)] = TILE_PLACEHOLDER_TERRAIN;
        }
    }

    // TODO: refactor this wireframe generation stuff into sprite loading
    constexpr float b_h_ = 3.0f;
    constexpr float b_w_ = 1.0f;
    const float tile_w = (float) map.tileset->tile_width;
    const float tile_h = (float) map.tileset->tile_height;
    const float iso_w = tile_w;
    const float iso_h = tile_h * 0.5f;
    const float start_x = (float) (map.height - 1) * iso_w * 0.5f;
    constexpr float start_y = 0.0f;
    const float iso_x = start_x + (float) (x - y) * iso_w * 0.5f - (b_w_ - 1) * iso_w * 0.5f;
    const float iso_y = start_y + (float) (x + y) * iso_h * 0.5f - tile_h - b_h_ * iso_h;
    wireframe_meshes[building_count] = build_wireframe_mesh(&map, iso_x, iso_y, iso_w, iso_h, 1, 3, 2, 3);

    building_count++;
}

void spawn_boats(const int amount, int *const restrict bldng_count, int *const restrict map_tiles, bool *const restrict map_occupied, const int map_width, const int map_height) {
//TODO: get logic from spawn from click and delete this...
    int count = 0;
    for (int y = map_height-1; y >= 2; y-=3) {
        for (int x = 0; x < map_width; x++) {
            // Check if the boat can fit in the current position
            if (unlikely(count >= amount || *bldng_count >= MAX_BUILDINGS)) {
                return;
            }
            if (likely(y - 2 < map_height && y - 1 < map_height &&
                !map_occupied[y * map_width + x] &&
                !map_occupied[(y-1) * map_width + (x)] &&
                !map_occupied[(y-2) * map_width + (x)])) {

                // Place the boat tiles
                map_tiles[y * map_width + x] = TILE_PLACEHOLDER_TERRAIN;
                map_tiles[(y-1) * map_width + (x)] = TILE_PLACEHOLDER_TERRAIN;
                map_tiles[(y-2) * map_width + (x)] = TILE_PLACEHOLDER_TERRAIN;

                // Mark the tiles as occupied
                map_occupied[y * map_width + x] = true;
                map_occupied[(y-1) * map_width + (x)] = true;
                map_occupied[(y-2) * map_width + (x)] = true;

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
                const float tile_w = (float) map.tileset->tile_width;
                const float tile_h = (float) map.tileset->tile_height;
                const float iso_w = tile_w;
                const float iso_h = tile_h * 0.5f;
                const float start_x = (float) (map.height - 1) * iso_w * 0.5f;
                constexpr float start_y = 0.0f;
                const float iso_x = start_x + (float) (x - y) * iso_w * 0.5f - (b_w_ - 1) * iso_w * 0.5f;
                const float iso_y = start_y + (float) (x + y) * iso_h * 0.5f - tile_h - b_h_ * iso_h;
                wireframe_meshes[building_count] = build_wireframe_mesh(&map, iso_x, iso_y, iso_w, iso_h, 1, 3, 2, 3);

                (*bldng_count)++;
                count++;
            }
        }
    }
}

int mainLoop(void) {
    int running = 1;
    float mouse_x = 0.0f, mouse_y = 0.0f;
    SDL_Point mouse_tile = {0, 0};

    while (running) {
        const float dt = PROF_getLastFrameTime() / 1000.0f;
        PROF_frameStart();

        // --- Event Handling ---
        PROF_start(PROFILER_EVENT_HANDLING);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = 0;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    switch (event.key.key) {
                        case SDLK_ESCAPE:
                            running = 0;
                            break;
                        case SDLK_APOSTROPHE:
                            wireframe_mode = !wireframe_mode;
                            break;
                        case SDLK_P:
                            debug_mode = !debug_mode;
                            break;
                        case SDLK_PLUS:
                            spawn_boats(50, &building_count, map.tiles, map.occupied, map.width,
                                        map.height);
                            break;
                        case SDLK_Z: {
                            Camera2D_Component *main_camera_component =
                                    ss_get(&ecs.cameras, main_camera);
                            main_camera_component->camera.position = (SDL_FPoint){0.0f, 0.0f};
                            main_camera_component->camera.zoom = 1.0f;
                            break;
                        }
                        case SDLK_W: {
                            Camera2D_Component *main_camera_component = ss_get(&ecs.cameras, main_camera);
                            const float normalized_zoom = (main_camera_component->camera.zoom * -1.0f) + 4.5f;
                            main_camera_component->camera.position.y -= normalized_zoom * dt * 6000.0f;
                            break;
                        }
                        case SDLK_S: {
                            Camera2D_Component *main_camera_component = ss_get(&ecs.cameras, main_camera);
                            main_camera_component->camera.position.y += main_camera_component->camera.zoom * dt * 6000.0f;
                            break;
                        }
                        case SDLK_A: {
                            Camera2D_Component *main_camera_component = ss_get(&ecs.cameras, main_camera);
                            main_camera_component->camera.position.x -= main_camera_component->camera.zoom * dt * 6000.0f;
                            break;
                        }
                        case SDLK_D: {
                            Camera2D_Component *main_camera_component = ss_get(&ecs.cameras, main_camera);
                            main_camera_component->camera.position.x += main_camera_component->camera.zoom * dt * 6000.0f;
                            break;
                        }
                        case SDLK_V: {
                            vsync = !vsync;
                            Renderer_SetVSync(vsync);
                        }
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        if (hover_tile.x < map.width && hover_tile.y < map.height &&
                            hover_tile.x >= 0 && hover_tile.y >= 0) {
                            if (building_count < MAX_BUILDINGS) {
                                bool ok = true;
                                // Simple check
                                if (!is_tile_free(&map, hover_tile))
                                    ok = false;
                                if (ok)
                                    spawn_boat(hover_tile.x, hover_tile.y);
                            }
                        }
                    }
                    break;
                case SDL_EVENT_MOUSE_MOTION: {
                    SDL_GetMouseState(&mouse_x, &mouse_y);
                    mouse_x *= pixel_ratio;
                    mouse_y *= pixel_ratio;
                    // SDL_RenderCoordinatesFromWindow(renderer, ...); // No longer needed
                    // if we use window coords directly or map them

                    Camera2D_Component *main_camera_component =
                            ss_get(&ecs.cameras, main_camera);
                    if (event.motion.state & SDL_BUTTON_MMASK) {
                        main_camera_component->camera.position.x -=
                                event.motion.xrel / main_camera_component->camera.zoom;
                        main_camera_component->camera.position.y -=
                                event.motion.yrel / main_camera_component->camera.zoom;
                    }

                    const SDL_FPoint world_position = cam_screen_to_world(
                        &main_camera_component->camera, mouse_x, mouse_y);
                    mouse_tile = screen_to_map_tile(world_position.x, world_position.y,
                                                    &map, 0.0f, 0.0f);
                    hover_tile = mouse_tile;
                }
                break;
                case SDL_EVENT_MOUSE_WHEEL: {
                    const float zoom_direction = event.wheel.y > 0 ? 1.0f : -1.0f;
                    CAMERA_zoom_apply(&ecs, main_camera, zoom_direction, 0.02f);
                }
                break;
                case SDL_EVENT_WINDOW_RESIZED: {
                    SDL_GetWindowSizeInPixels(window, &screen_width, &screen_height);
                    Renderer_Resize(screen_width, screen_height);
                    Camera2D_Component *main_camera_component =
                            ss_get(&ecs.cameras, main_camera);
                    main_camera_component->camera.viewport.w = screen_width;
                    main_camera_component->camera.viewport.h = screen_height;
                }
                break;
            }
        }

        // --- Systems ---
        CAMERA_smooth_zoom_system(&ecs, PROF_getLastFrameTime(),
                                  (SDL_FPoint){mouse_x, mouse_y});
        PROF_stop(PROFILER_EVENT_HANDLING);

        // --- Rendering ---
        PROF_start(PROFILER_GPU);
        Renderer_BeginFrame(); // vsync point happens here
        PROF_stop(PROFILER_GPU);

        // Update View-Projection Matrix
        const Camera2D_Component *main_camera_component =
                ss_get(&ecs.cameras, main_camera);
        float scale, offx, offy;
        cam_get_render_params(&main_camera_component->camera, &scale, &offx, &offy);


        float view_proj[16];
        cam_get_view_projection_matrix(&main_camera_component->camera, view_proj);
        Renderer_SetViewProjection(view_proj);


        render_map(&map, 0, 0); // Offset handled by matrix now?
        // Wait, `render_map` uses `offset_x` to calculate positions.
        // If we pass 0,0, it calculates world positions relative to 0,0.
        // The matrix handles the camera offset.
        // BUT `render_map` logic: `start_x = offset_x + ...`
        // If we pass 0, `start_x` is just the map origin in world space.
        // Yes, that's correct.

        render_buildings(&map, transforms, renderables, building_count, 0, 0);

        // DEBUG: Draw a filled red quad to verify geometry pipeline works for
        // filled shapes Renderer_DrawFilledQuadDebug(100, 150, 200, 100,
        // (SDL_FColor){1.0f, 0.0f, 0.0f, 1.0f});

        // DEBUG: Draw the tileset texture in the corner to verify sprite pipeline
        Renderer_DrawTextureDebug(map.tileset->texture, 50,
                                  screen_height - 384 - 50, 192, 384);

        if (wireframe_mode) {
            PROF_start(PROFILER_RENDER_WIREFRAMES);
            SDL_Vertex *combined = SDL_malloc(
                sizeof(SDL_Vertex) * wireframe_meshes->vert_count * building_count);
            ssize_t offset = 0;
            for (int i = 0; i < building_count; i++) {
                SDL_memcpy(combined + offset, wireframe_meshes[i].verts,
                           sizeof(SDL_Vertex) * wireframe_meshes[i].vert_count);
                offset += wireframe_meshes[i].vert_count;
                // Renderer_DrawGeometry(wireframe_meshes[i].verts,
                //                       wireframe_meshes[i].vert_count);
            }
            Renderer_DrawGeometry(combined, offset);
            SDL_free(combined);
            PROF_stop(PROFILER_RENDER_WIREFRAMES);
        }

        PROF_start(PROFILER_RENDER_UI);

        float ui_y_pos = 20.0f;

        char hover_tile_info[64];
        snprintf(hover_tile_info, sizeof(hover_tile_info), "Tile: (%d, %d)",
                 hover_tile.x, hover_tile.y);
        TTF_SetTextString(fps_text, hover_tile_info, 0);
        UI_TextWithBackground(fps_text, 10, ui_y_pos);
        ui_y_pos += 40.0f;

        char camera_info[128];
        snprintf(camera_info, sizeof(camera_info),
                 "Camera Pos: (%5.1f, %5.1f) | Zoom: %4.2f",
                 main_camera_component->camera.position.x,
                 main_camera_component->camera.position.y,
                 main_camera_component->camera.zoom);
        TTF_SetTextString(fps_text, camera_info, 0);
        UI_TextWithBackground(fps_text, 10, ui_y_pos);
        ui_y_pos += 40.0f;

        char fps_str[64];
        if (debug_mode) {
            float min, max, avg;
            PROF_getFPS(&min, &avg, &max);
            snprintf(fps_str, sizeof(fps_str), "FPS: min %4.0f  |  avg %4.0f  |  max %4.0f ", min, avg, max);
        } else {
            snprintf(fps_str, sizeof(fps_str), "FPS %4.0f ", 1.0f / dt);
        }
        TTF_SetTextString(fps_text, fps_str, 0);
        UI_TextWithBackground(fps_text, 10, ui_y_pos);
        ui_y_pos += 40.0f;

        if (debug_mode) {
            PROF_render((SDL_FPoint){10.0f, ui_y_pos});
        }

        //mouse pos next to mouse
        char mouse_pos_info[64];
        snprintf(mouse_pos_info, sizeof(mouse_pos_info), " %.1f, %.1f ", mouse_x, mouse_y);
        TTF_SetTextString(fps_text, mouse_pos_info, 0);
        UI_TextWithBackground(fps_text, mouse_x + 15.0f, mouse_y + 15.0f);
        UI_Flush();

        PROF_stop(PROFILER_RENDER_UI);

        PROF_start(PROFILER_GPU);
        Renderer_EndFrame();
        PROF_stop(PROFILER_GPU);

        // --- Wait Frame ---
        PROF_start(PROFILER_WAIT_FRAME);
        // const Uint32 wait_time = (Uint32) PROF_getFrameWaitTime();
        // if (wait_time >= 1)
        //     SDL_Delay(wait_time - 1);
        // SDL_Delay(1);

        sea_time += dt * SDL_PI_F * 2.0f * WAVE_SPEED;
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
