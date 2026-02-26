#include "testbed_game.h"

#include "game_clock.h"
#include "miso_camera.h"
#include "miso_debug_ui.h"
#include "miso_render.h"
#include "profiler.h"
#include "renderer/renderer.h"
#include "tilemap/tilemap.h"
#include "vendored/nuklear/nuklear.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdarg.h>
#include <stddef.h>

#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080
#define TILE_SIZE 32
#define MAP_SIZE_X 70
#define MAP_SIZE_Y 50
#define MAX_BUILDINGS 512

typedef enum TileType_ {
    TILE_PLACEHOLDER_BUILDING = 48,
    TILE_PLACEHOLDER_TERRAIN = 18,
    TILE_PLACEHOLDER_SEA = 13,
    TILE_PLACEHOLDER_BOAT = 54
} TileType;

typedef struct TransformComponent_ {
    int x;
    int y;
} TransformComponent;

typedef struct BuildingComponent_ {
    int width;
    int length;
} BuildingComponent;

typedef struct RenderableComponent_ {
    int tile_index;
    int sprite_w;
    int sprite_h;
} RenderableComponent;

typedef struct WireframeMesh_ {
    float *line_vertices;
    int vertex_count;
} WireframeMesh;

struct TestbedGame {
    MisoEngine *engine;
    bool running;
    bool wireframe_mode;
    bool debug_mode;
    bool vsync;
    bool middle_dragging;

    int screen_width;
    int screen_height;
    float pixel_ratio;
    float mouse_x;
    float mouse_y;
    float frame_dt;
    SDL_Point hover_tile;

    GameClock game_clock;
    float wave_speed;
    float wave_amplitude;
    float wave_phase;

    MisoCameraId camera_id;
    float camera_x;
    float camera_y;
    float camera_zoom;

    Tileset *tileset;
    Tilemap *tilemap;

    int building_count;
    RenderableComponent renderables[MAX_BUILDINGS];
    TransformComponent transforms[MAX_BUILDINGS];
    BuildingComponent buildings[MAX_BUILDINGS];
    WireframeMesh wireframe_meshes[MAX_BUILDINGS];
    SpriteInstance *building_instances;
    int building_instances_capacity;
    float *wireframe_line_scratch;
    size_t wireframe_line_scratch_capacity_vertices;

    TTF_Font *profiler_font;
    MisoFontHandle hud_font;

    bool benchmark_mode;
    bool benchmark_debug_ui_enabled;
    bool benchmark_profiler_enabled;
    TestbedBenchDiagnosticMode benchmark_diagnostic_mode;
    TestbedBenchCameraState benchmark_camera_state;
    float benchmark_camera_x;
    float benchmark_camera_y;
    float benchmark_camera_zoom;
};

static int testbed_is_point_in_rect(const float x, const float y, const SDL_FRect *const SDL_RESTRICT rect) {
    return (x >= rect->x && x <= rect->x + rect->w && y >= rect->y && y <= rect->y + rect->h);
}

static void testbed_nk_labelf(struct nk_context *ctx, nk_flags align, const char *fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    SDL_vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    nk_label(ctx, buffer, align);
}

static const char *testbed_present_mode_name(const SDL_GPUPresentMode mode) {
    switch (mode) {
    case SDL_GPU_PRESENTMODE_VSYNC:
        return "VSYNC";
    case SDL_GPU_PRESENTMODE_MAILBOX:
        return "MAILBOX";
    case SDL_GPU_PRESENTMODE_IMMEDIATE:
        return "IMMEDIATE";
    default:
        return "UNKNOWN";
    }
}

static float testbed_bytes_to_mib(const uint32_t bytes) {
    return (float)bytes / (1024.0f * 1024.0f);
}

static float testbed_usage_percent(const uint32_t used, const uint32_t capacity) {
    if (capacity == 0U) {
        return 0.0f;
    }
    return ((float)used * 100.0f) / (float)capacity;
}

const char *testbed_bench_camera_state_name(const TestbedBenchCameraState state) {
    switch (state) {
    case TESTBED_BENCH_CAMERA_ZOOM_OUT_CENTER:
        return "zoom_out_center";
    case TESTBED_BENCH_CAMERA_ZOOM_IN_CENTER:
        return "zoom_in_center";
    case TESTBED_BENCH_CAMERA_ZOOM_IN_OFFMAP:
        return "zoom_in_offmap";
    default:
        return "zoom_out_center";
    }
}

const char *testbed_bench_diagnostic_mode_name(const TestbedBenchDiagnosticMode mode) {
    switch (mode) {
    case TESTBED_BENCH_DIAGNOSTIC_DEFAULT:
        return "default";
    case TESTBED_BENCH_DIAGNOSTIC_WORLD_ONLY:
        return "world-only";
    case TESTBED_BENCH_DIAGNOSTIC_UI_ONLY:
        return "ui-only";
    case TESTBED_BENCH_DIAGNOSTIC_WIRE_ONLY:
        return "wire-only";
    case TESTBED_BENCH_DIAGNOSTIC_NO_DRAW:
        return "no-draw";
    case TESTBED_BENCH_DIAGNOSTIC_UPLOAD_SUPPRESSED:
        return "upload-suppressed";
    default:
        return "default";
    }
}

static void testbed_sync_window_metrics(TestbedGame *const game) {
    if (!game || !game->engine) {
        return;
    }

    int new_width, new_height;
    miso_get_window_size_pixels(game->engine, &new_width, &new_height);

    float new_density = miso_get_window_pixel_density(game->engine);
    if (new_density <= 0.0f) {
        new_density = 1.0f;
    }

    const bool size_changed = (new_width != game->screen_width) || (new_height != game->screen_height);
    const bool density_changed = SDL_fabsf(new_density - game->pixel_ratio) > 0.001f;

    game->screen_width = new_width;
    game->screen_height = new_height;
    game->pixel_ratio = new_density;

    if (size_changed || density_changed) {
        miso_camera_set_viewport(game->engine, game->camera_id, 0, 0, game->screen_width, game->screen_height);
    }
}

static void testbed_apply_benchmark_camera_preset(TestbedGame *const game) {
    if (!game || !game->tilemap || game->camera_id == 0) {
        return;
    }

    const int center_x = game->tilemap->width / 2;
    const int center_y = game->tilemap->height / 2;

    float world_x = 0.0f;
    float world_y = 0.0f;
    Tilemap_TileToWorld(game->tilemap, center_x, center_y, &world_x, &world_y);

    float zoom = 1.0f;
    switch (game->benchmark_camera_state) {
    case TESTBED_BENCH_CAMERA_ZOOM_OUT_CENTER:
        zoom = 0.5f;
        break;
    case TESTBED_BENCH_CAMERA_ZOOM_IN_CENTER:
        zoom = 3.0f;
        break;
    case TESTBED_BENCH_CAMERA_ZOOM_IN_OFFMAP:
        zoom = 3.0f;
        world_x += 5000.0f;
        world_y -= 5000.0f;
        break;
    default:
        break;
    }

    game->benchmark_camera_x = world_x;
    game->benchmark_camera_y = world_y;
    game->benchmark_camera_zoom = zoom;

    game->camera_x = world_x;
    game->camera_y = world_y;
    game->camera_zoom = zoom;
    miso_camera_set_position(game->engine, game->camera_id, world_x, world_y);
    miso_camera_set_zoom(game->engine, game->camera_id, zoom);
}

static bool testbed_should_render_world(const TestbedGame *const game) {
    if (!game || !game->benchmark_mode) {
        return true;
    }

    switch (game->benchmark_diagnostic_mode) {
    case TESTBED_BENCH_DIAGNOSTIC_UI_ONLY:
    case TESTBED_BENCH_DIAGNOSTIC_WIRE_ONLY:
    case TESTBED_BENCH_DIAGNOSTIC_NO_DRAW:
        return false;
    default:
        return true;
    }
}

static bool testbed_should_render_ui(const TestbedGame *const game) {
    if (!game || !game->benchmark_mode) {
        return true;
    }

    switch (game->benchmark_diagnostic_mode) {
    case TESTBED_BENCH_DIAGNOSTIC_WORLD_ONLY:
    case TESTBED_BENCH_DIAGNOSTIC_WIRE_ONLY:
    case TESTBED_BENCH_DIAGNOSTIC_NO_DRAW:
    case TESTBED_BENCH_DIAGNOSTIC_UPLOAD_SUPPRESSED:
        return false;
    default:
        return true;
    }
}

static bool testbed_should_render_wire(const TestbedGame *const game) {
    if (!game) {
        return false;
    }
    if (!game->benchmark_mode) {
        return game->wireframe_mode;
    }

    switch (game->benchmark_diagnostic_mode) {
    case TESTBED_BENCH_DIAGNOSTIC_WIRE_ONLY:
        return true;
    case TESTBED_BENCH_DIAGNOSTIC_WORLD_ONLY:
    case TESTBED_BENCH_DIAGNOSTIC_UI_ONLY:
    case TESTBED_BENCH_DIAGNOSTIC_NO_DRAW:
        return false;
    default:
        return game->wireframe_mode;
    }
}

static bool testbed_ensure_building_instance_capacity(TestbedGame *const game, const int needed_instances) {
    if (!game || needed_instances <= 0) {
        return false;
    }
    if (needed_instances <= game->building_instances_capacity) {
        return true;
    }

    int new_capacity = game->building_instances_capacity > 0 ? game->building_instances_capacity : 512;
    while (new_capacity < needed_instances) {
        new_capacity *= 2;
    }

    SpriteInstance *new_instances =
        SDL_realloc(game->building_instances, sizeof(SpriteInstance) * (size_t)new_capacity);
    if (!new_instances) {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_RENDER, "testbed: failed to grow building instance scratch to %d", needed_instances);
        return false;
    }

    game->building_instances = new_instances;
    game->building_instances_capacity = new_capacity;
    return true;
}

static bool testbed_ensure_wireframe_line_capacity(TestbedGame *game, const size_t needed_vertices) {
    if (!game || needed_vertices == 0U) {
        return false;
    }
    if (needed_vertices <= game->wireframe_line_scratch_capacity_vertices) {
        return true;
    }

    size_t new_capacity =
        game->wireframe_line_scratch_capacity_vertices > 0U ? game->wireframe_line_scratch_capacity_vertices : 2048U;
    while (new_capacity < needed_vertices) {
        new_capacity *= 2U;
    }

    float *new_scratch = SDL_realloc(game->wireframe_line_scratch, sizeof(float) * new_capacity * 3U);
    if (!new_scratch) {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_RENDER, "testbed: failed to grow wireframe scratch to %zu vertices", needed_vertices);
        return false;
    }

    game->wireframe_line_scratch = new_scratch;
    game->wireframe_line_scratch_capacity_vertices = new_capacity;
    return true;
}

static bool testbed_wireframe_push_line(float *line_vertices,
                                        const int vertex_capacity,
                                        int *const vertex_count,
                                        const float x1,
                                        const float y1,
                                        const float z1,
                                        const float x2,
                                        const float y2,
                                        const float z2) {
    if (!line_vertices || !vertex_count || *vertex_count + 2 > vertex_capacity) {
        return false;
    }

    const int base = *vertex_count * 3;
    line_vertices[base + 0] = x1;
    line_vertices[base + 1] = y1;
    line_vertices[base + 2] = z1;
    line_vertices[base + 3] = x2;
    line_vertices[base + 4] = y2;
    line_vertices[base + 5] = z2;
    *vertex_count += 2;
    return true;
}

static void testbed_render_tile_highlight(const TestbedGame *const game,
                                          const int tile_x,
                                          const int tile_y,
                                          const SDL_FColor color) {
    if (!game->tilemap || tile_x < 0 || tile_y < 0 || tile_x >= game->tilemap->width ||
        tile_y >= game->tilemap->height) {
        return;
    }

    float world_x = 0.0f;
    float world_y = 0.0f;
    Tilemap_TileToWorld(game->tilemap, tile_x, tile_y, &world_x, &world_y);

    float iso_w = 0.0f;
    float iso_h = 0.0f;
    Tileset_GetIsoDimensions(game->tilemap->tileset, &iso_w, &iso_h);

    const float top_x = world_x + iso_w / 2.0f;
    const float top_y = world_y;
    const float right_x = world_x + iso_w;
    const float right_y = world_y + iso_h / 2.0f;
    const float bottom_x = world_x + iso_w / 2.0f;
    const float bottom_y = world_y + iso_h;
    const float left_x = world_x;
    const float left_y = world_y + iso_h / 2.0f;

    const float depth = Tilemap_GetTileDepth(game->tilemap, tile_x, tile_y) - 0.002f;

    Renderer_DrawLine(top_x, top_y, depth, right_x, right_y, depth, color);
    Renderer_DrawLine(right_x, right_y, depth, bottom_x, bottom_y, depth, color);
    Renderer_DrawLine(bottom_x, bottom_y, depth, left_x, left_y, depth, color);
    Renderer_DrawLine(left_x, left_y, depth, top_x, top_y, depth, color);

    constexpr float beacon_height = 200.0f;
    Renderer_DrawLine(top_x, top_y, depth, top_x, top_y - beacon_height, depth, color);
}

static WireframeMesh testbed_build_wireframe_mesh(const float iso_x,
                                                  const float iso_y,
                                                  const float iso_w,
                                                  const float iso_h,
                                                  const int bw,
                                                  const int bl,
                                                  const int sw,
                                                  const int sh,
                                                  const float depth) {
    const float half_w = iso_w * 0.5f;
    const float half_h = iso_h * 0.5f;
    const float tile_h = iso_h * 2.0f;
    const float base_y = iso_y + tile_h * (float)sh;

    const int max_lines = (bw + bl) + bw * (sh + 1) + bl * (sh + 1) + (bw + 1) * (bl + 1);
    const int vertex_capacity = max_lines * 2;
    float *line_vertices = SDL_malloc(sizeof(float) * (size_t)vertex_capacity * 3U);
    if (!line_vertices) {
        return (WireframeMesh){0};
    }

    int vertex_count = 0;

    for (int x = 0; x < bw; x++) {
        const float vx = iso_x + half_w * (float)x;
        const float vb = base_y - half_h * (float)(bw - x);
        const float vt = vb - iso_h * (float)sh;
        if (!testbed_wireframe_push_line(line_vertices, vertex_capacity, &vertex_count, vx, vb, depth, vx, vt, depth)) {
            goto fail;
        }
        for (int y = 0; y <= sh; y++) {
            const float dy = vb - iso_h * (float)y;
            if (!testbed_wireframe_push_line(
                    line_vertices, vertex_capacity, &vertex_count, vx, dy, depth, vx + half_w, dy + half_h, depth)) {
                goto fail;
            }
        }
    }

    for (int x = 0; x <= bl; x++) {
        const float vx = iso_x + iso_w * (float)(bw + x) * 0.5f;
        const float vb = base_y - iso_h * 0.5f * (float)x;
        const float vt = vb - iso_h * (float)sh;
        if (!testbed_wireframe_push_line(line_vertices, vertex_capacity, &vertex_count, vx, vb, depth, vx, vt, depth)) {
            goto fail;
        }

        if (x < bl) {
            for (int y = 0; y <= sh; y++) {
                const float dy = vb - iso_h * (float)y;
                if (!testbed_wireframe_push_line(line_vertices,
                                                 vertex_capacity,
                                                 &vertex_count,
                                                 vx,
                                                 dy,
                                                 depth,
                                                 vx + half_w,
                                                 dy - half_h,
                                                 depth)) {
                    goto fail;
                }
            }
        }
    }

    const SDL_FPoint roof_fl = {iso_x, base_y - half_h * (float)bw - iso_h * (float)sh};
    const SDL_FPoint roof_fr = {iso_x + half_w * (float)bw,
                                base_y - half_h * (float)bw - iso_h * (float)sh + half_h * (float)bw};
    const SDL_FPoint roof_bl = {iso_x + half_w * (float)bl, roof_fl.y - half_h * (float)bl};
    const SDL_FPoint dv_len = {half_w, -half_h};
    const SDL_FPoint dv_width = {half_w, half_h};

    for (int i = 1; i <= bl; i++) {
        const SDL_FPoint p0 = {roof_fl.x + dv_len.x * (float)i, roof_fl.y + dv_len.y * (float)i};
        const SDL_FPoint p1 = {roof_fr.x + dv_len.x * (float)i, roof_fr.y + dv_len.y * (float)i};
        if (!testbed_wireframe_push_line(
                line_vertices, vertex_capacity, &vertex_count, p0.x, p0.y, depth, p1.x, p1.y, depth)) {
            goto fail;
        }
    }

    for (int j = 0; j < bw; j++) {
        const SDL_FPoint p0 = {roof_fl.x + dv_width.x * (float)j, roof_fl.y + dv_width.y * (float)j};
        const SDL_FPoint p1 = {roof_bl.x + dv_width.x * (float)j, roof_bl.y + dv_width.y * (float)j};
        if (!testbed_wireframe_push_line(
                line_vertices, vertex_capacity, &vertex_count, p0.x, p0.y, depth, p1.x, p1.y, depth)) {
            goto fail;
        }
    }

    (void)sw;
    return (WireframeMesh){.line_vertices = line_vertices, .vertex_count = vertex_count};

fail:
    SDL_free(line_vertices);
    return (WireframeMesh){0};
}

static void testbed_render_buildings(TestbedGame *const game) {
    PROF_start(PROFILER_RENDER_BUILDINGS);

    if (!game || !game->tilemap) {
        PROF_stop(PROFILER_RENDER_BUILDINGS);
        return;
    }

    const int needed_instances = game->building_count + 1;
    if (!testbed_ensure_building_instance_capacity(game, needed_instances)) {
        PROF_stop(PROFILER_RENDER_BUILDINGS);
        return;
    }

    const int tile_w = (int)game->tilemap->tileset->tile_width;
    const int tile_h = (int)game->tilemap->tileset->tile_height;
    const float iso_w = (float)tile_w;
    const float iso_h = (float)tile_h / 2.0f;
    const float start_x = (float)(game->tilemap->height - 1) * iso_w / 2.0f;
    constexpr float start_y = 0.0f;

    SpriteInstance *instances = game->building_instances;
    int instance_count = 0;

    const float tex_w = (float)(game->tilemap->tileset->columns * game->tilemap->tileset->tile_width);
    const float tex_h = (float)(game->tilemap->tileset->rows * game->tilemap->tileset->tile_height);

    for (int entity = 0; entity < game->building_count; entity++) {
        const int bw = game->renderables[entity].sprite_w;
        const int bh = game->renderables[entity].sprite_h;
        const int mx = game->transforms[entity].x;
        const int my = game->transforms[entity].y;

        float iso_x = start_x + (float)(mx - my) * (iso_w / 2.0f);
        float iso_y = start_y + (float)(mx + my) * (iso_h / 2.0f);

        iso_y -= (float)tile_h;
        iso_y -= (float)bh * iso_h;
        iso_x -= (float)(game->buildings[entity].width - 1) * 0.5f * iso_w;

        const int col = game->renderables[entity].tile_index % (int)game->tilemap->tileset->columns;
        const int row = game->renderables[entity].tile_index / (int)game->tilemap->tileset->columns;
        const float u = (float)(col * tile_w) / tex_w;
        const float v = (float)(row * tile_h) / tex_h;
        const float uw = (float)(bw * tile_w) / tex_w;
        const float vh = (float)(bh * tile_h) / tex_h;

        const float depth = 1.0f - (float)(mx + my) / (float)(game->tilemap->width + game->tilemap->height) - 0.001f;

        instances[instance_count++] = (SpriteInstance){.x = iso_x,
                                                       .y = iso_y,
                                                       .z = depth,
                                                       .flags = 0.0f,
                                                       .w = (float)(bw * tile_w),
                                                       .h = (float)(bh * tile_h),
                                                       .tile_x = 0.0f,
                                                       .tile_y = 0.0f,
                                                       .u = u,
                                                       .v = v,
                                                       .uw = uw,
                                                       .vh = vh};
    }

    Renderer_DrawSprites(game->tilemap->tileset->texture, instances, instance_count);

    PROF_stop(PROFILER_RENDER_BUILDINGS);
}

static void testbed_spawn_boat(TestbedGame *const game, const int x, const int y) {
    if (!game->tilemap || game->building_count >= MAX_BUILDINGS) {
        return;
    }

    constexpr int b_w = 1;
    constexpr int b_l = 3;

    game->renderables[game->building_count].tile_index = TILE_PLACEHOLDER_BOAT;
    game->renderables[game->building_count].sprite_w = 2;
    game->renderables[game->building_count].sprite_h = 3;

    game->transforms[game->building_count].x = x;
    game->transforms[game->building_count].y = y;
    game->buildings[game->building_count].width = b_w;
    game->buildings[game->building_count].length = b_l;

    for (int dy = 0; dy > -b_l; dy--) {
        for (int dx = 0; dx > -b_w; dx--) {
            Tilemap_SetOccupied(game->tilemap, game->hover_tile.x + dx, game->hover_tile.y + dy, true);
            Tilemap_SetTile(game->tilemap, game->hover_tile.x + dx, game->hover_tile.y + dy, TILE_PLACEHOLDER_TERRAIN);
            Tilemap_SetFlags(game->tilemap, game->hover_tile.x + dx, game->hover_tile.y + dy, TILE_FLAG_NONE);
        }
    }

    constexpr float b_h_ = 3.0f;
    constexpr float b_w_ = 1.0f;
    const float tile_w = (float)game->tilemap->tileset->tile_width;
    const float tile_h = (float)game->tilemap->tileset->tile_height;
    const float iso_w = tile_w;
    const float iso_h = tile_h * 0.5f;
    const float start_x = (float)(game->tilemap->height - 1) * iso_w * 0.5f;
    constexpr float start_y = 0.0f;
    const float iso_x = start_x + (float)(x - y) * iso_w * 0.5f - (b_w_ - 1.0f) * iso_w * 0.5f;
    const float iso_y = start_y + (float)(x + y) * iso_h * 0.5f - tile_h - b_h_ * iso_h;
    const float wire_depth = 1.0f - (float)(x + y) / (float)(game->tilemap->width + game->tilemap->height) - 0.0015f;
    game->wireframe_meshes[game->building_count] =
        testbed_build_wireframe_mesh(iso_x, iso_y, iso_w, iso_h, 1, 3, 2, 3, wire_depth);

    game->building_count++;
}

static void testbed_spawn_boats(TestbedGame *const game, const int amount) {
    if (!game->tilemap) {
        return;
    }

    int count = 0;
    for (int y = game->tilemap->height - 1; y >= 2; y -= 3) {
        for (int x = 0; x < game->tilemap->width; x++) {
            if (unlikely(count >= amount || game->building_count >= MAX_BUILDINGS)) {
                return;
            }

            if (Tilemap_IsTileFree(game->tilemap, x, y) && Tilemap_IsTileFree(game->tilemap, x, y - 1) &&
                Tilemap_IsTileFree(game->tilemap, x, y - 2)) {

                for (int dy = 0; dy < 3; dy++) {
                    Tilemap_SetTile(game->tilemap, x, y - dy, TILE_PLACEHOLDER_TERRAIN);
                    Tilemap_SetOccupied(game->tilemap, x, y - dy, true);
                    Tilemap_SetFlags(game->tilemap, x, y - dy, TILE_FLAG_NONE);
                }

                game->renderables[game->building_count].tile_index = TILE_PLACEHOLDER_BOAT;
                game->renderables[game->building_count].sprite_w = 2;
                game->renderables[game->building_count].sprite_h = 3;

                game->transforms[game->building_count].x = x;
                game->transforms[game->building_count].y = y;
                game->buildings[game->building_count].width = 1;
                game->buildings[game->building_count].length = 3;

                constexpr float b_h_ = 3.0f;
                constexpr float b_w_ = 1.0f;
                const float tile_w = (float)game->tilemap->tileset->tile_width;
                const float tile_h = (float)game->tilemap->tileset->tile_height;
                const float iso_w = tile_w;
                const float iso_h = tile_h * 0.5f;
                const float start_x = (float)(game->tilemap->height - 1) * iso_w * 0.5f;
                constexpr float start_y = 0.0f;
                const float iso_x = start_x + (float)(x - y) * iso_w * 0.5f - (b_w_ - 1.0f) * iso_w * 0.5f;
                const float iso_y = start_y + (float)(x + y) * iso_h * 0.5f - tile_h - b_h_ * iso_h;
                const float wire_depth =
                    1.0f - (float)(x + y) / (float)(game->tilemap->width + game->tilemap->height) - 0.0015f;
                game->wireframe_meshes[game->building_count] =
                    testbed_build_wireframe_mesh(iso_x, iso_y, iso_w, iso_h, 1, 3, 2, 3, wire_depth);

                game->building_count++;
                count++;
            }
        }
    }
}

static void testbed_refresh_hover_tile(TestbedGame *const game) {
    if (!game->tilemap || game->camera_id == 0) {
        return;
    }

    const MisoVec2 world_position =
        miso_camera_screen_to_world(game->engine, game->camera_id, (int)game->mouse_x, (int)game->mouse_y);
    game->hover_tile = Tilemap_ScreenToTile(game->tilemap, world_position.x, world_position.y);
}

static void testbed_game_on_event(void *const ctx, const MisoEvent *const event) {
    TestbedGame *const game = (TestbedGame *)ctx;
    if (!game || !event) {
        return;
    }

    if (!game->benchmark_mode || game->benchmark_debug_ui_enabled) {
        PROF_stop(PROFILER_EVENT_HANDLING);
        PROF_start(PROFILER_NUKLEAR);
        const bool consumed = miso_debug_ui_feed_event(event);
        PROF_stop(PROFILER_NUKLEAR);
        PROF_start(PROFILER_EVENT_HANDLING);
        if (consumed) {
            return;
        }
    }

    switch (event->type) {
    case MISO_EVENT_QUIT:
        game->running = false;
        break;

    case MISO_EVENT_KEY:
        if (game->benchmark_mode) {
            if (event->data.key.down && event->data.key.keycode == SDLK_ESCAPE) {
                game->running = false;
            }
            break;
        }
        if (!event->data.key.down) {
            break;
        }
        switch (event->data.key.keycode) {
        case SDLK_ESCAPE:
            game->running = false;
            break;
        case SDLK_APOSTROPHE:
            game->wireframe_mode = !game->wireframe_mode;
            break;
        case SDLK_P:
            game->debug_mode = !game->debug_mode;
            break;
        case SDLK_PLUS:
        case SDLK_KP_PLUS:
        case SDLK_EQUALS:
            if (event->data.key.repeat) {
                break;
            }
            if (event->data.key.keycode == SDLK_EQUALS && (event->data.key.modifiers & MISO_KEYMOD_SHIFT) == 0U) {
                break;
            }
            testbed_spawn_boats(game, 50);
            break;
        case SDLK_Z:
            game->camera_x = 0.0f;
            game->camera_y = 0.0f;
            game->camera_zoom = 1.0f;
            miso_camera_set_position(game->engine, game->camera_id, game->camera_x, game->camera_y);
            miso_camera_set_zoom(game->engine, game->camera_id, game->camera_zoom);
            break;
        case SDLK_W:
        case SDLK_S:
        case SDLK_A:
        case SDLK_D: {
            float dx = 0.0f;
            float dy = 0.0f;
            if (event->data.key.keycode == SDLK_W) {
                dy = -1.0f;
            } else if (event->data.key.keycode == SDLK_S) {
                dy = 1.0f;
            } else if (event->data.key.keycode == SDLK_A) {
                dx = -1.0f;
            } else if (event->data.key.keycode == SDLK_D) {
                dx = 1.0f;
            }

            const float speed = 500.0f / game->camera_zoom;
            const float pan_x = dx * speed * game->frame_dt;
            const float pan_y = dy * speed * game->frame_dt;
            miso_camera_pan(game->engine, game->camera_id, pan_x, pan_y);
            game->camera_x += pan_x;
            game->camera_y += pan_y;
            break;
        }
        case SDLK_V:
            game->vsync = !game->vsync;
            Renderer_SetVSync(game->vsync);
            break;
        default:
            break;
        }
        break;

    case MISO_EVENT_MOUSE_BUTTON:
        if (game->benchmark_mode) {
            break;
        }
        if (event->data.mouse_button.button == MISO_MOUSE_BUTTON_LEFT && event->data.mouse_button.down) {
            if (game->hover_tile.x >= 0 && game->hover_tile.y >= 0 && game->tilemap &&
                game->hover_tile.x < game->tilemap->width && game->hover_tile.y < game->tilemap->height &&
                game->building_count < MAX_BUILDINGS &&
                Tilemap_IsTileFree(game->tilemap, game->hover_tile.x, game->hover_tile.y)) {
                testbed_spawn_boat(game, game->hover_tile.x, game->hover_tile.y);
            }
        }
        if (event->data.mouse_button.button == MISO_MOUSE_BUTTON_MIDDLE) {
            game->middle_dragging = event->data.mouse_button.down;
        }
        break;

    case MISO_EVENT_MOUSE_MOVE:
        if (game->benchmark_mode) {
            break;
        }
        game->mouse_x = (float)event->data.mouse_move.x * game->pixel_ratio;
        game->mouse_y = (float)event->data.mouse_move.y * game->pixel_ratio;

        if (game->middle_dragging) {
            const float dx_world = -((float)event->data.mouse_move.dx * game->pixel_ratio) / game->camera_zoom;
            const float dy_world = -((float)event->data.mouse_move.dy * game->pixel_ratio) / game->camera_zoom;
            miso_camera_pan(game->engine, game->camera_id, dx_world, dy_world);
            game->camera_x += dx_world;
            game->camera_y += dy_world;
        }

        testbed_refresh_hover_tile(game);
        break;

    case MISO_EVENT_MOUSE_WHEEL:
        if (game->benchmark_mode) {
            break;
        }
        if (event->data.mouse_wheel.y != 0.0f) {
            miso_camera_zoom_at_screen(
                game->engine, game->camera_id, event->data.mouse_wheel.y, game->mouse_x, game->mouse_y);
            game->camera_zoom = miso_camera_get_zoom(game->engine, game->camera_id);
            const MisoVec2 cam_pos = miso_camera_get_position(game->engine, game->camera_id);
            game->camera_x = cam_pos.x;
            game->camera_y = cam_pos.y;
        }
        break;

    case MISO_EVENT_WINDOW_RESIZED:
        testbed_sync_window_metrics(game);
        break;

    default:
        break;
    }
}

static void testbed_game_on_sim_tick(void *const ctx, const float fixed_dt_seconds) {
    (void)ctx;
    (void)fixed_dt_seconds;
}

static void testbed_render_hud_line(TestbedGame *game, const float x, const float y, const char *const text) {
    if (!game || !text ||
        !testbed_is_point_in_rect(x, y, &(SDL_FRect){0, 0, (float)game->screen_width, (float)game->screen_height})) {
        return;
    }

    constexpr float pad_x = 6.0f;
    constexpr float pad_y = 4.0f;
    constexpr float line_h = 28.0f;
    constexpr float estimate_char_w = 9.0f;
    const float box_w = estimate_char_w * (float)SDL_strlen(text) + pad_x * 2.0f;

    miso_render_submit_ui_rect(game->engine, x, y, box_w, line_h, 0x000000AAu);
    miso_render_submit_ui_text(game->engine, game->hud_font, text, x + pad_x, y + pad_y, 0xFFFFFFFFu);
}

static void testbed_game_on_render_world(void *const ctx, const MisoEngine *const engine) {
    TestbedGame *const game = (TestbedGame *)ctx;
    if (!game || !game->tilemap) {
        return;
    }

    miso_render_begin_world(engine, game->camera_id);
    miso_render_set_water_params(
        engine, game->game_clock.total, game->wave_speed, game->wave_amplitude, game->wave_phase);

    if (testbed_should_render_world(game)) {
        PROF_start(PROFILER_RENDER_MAP);
        Tilemap_Render(game->tilemap);
        PROF_stop(PROFILER_RENDER_MAP);

        testbed_render_buildings(game);
        testbed_render_tile_highlight(
            game, game->hover_tile.x, game->hover_tile.y, (SDL_FColor){0.0f, 1.0f, 1.0f, 1.0f});

        Renderer_DrawTextureDebug(
            game->tilemap->tileset->texture, 50.0f, (float)game->screen_height - 384.0f - 50.0f, 192.0f, 384.0f);
    }

    if (testbed_should_render_wire(game)) {
        PROF_start(PROFILER_RENDER_WIREFRAMES);
        size_t total_vertex_count = 0;
        for (int i = 0; i < game->building_count; i++) {
            total_vertex_count += (size_t)game->wireframe_meshes[i].vertex_count;
        }

        if (total_vertex_count > 0 && testbed_ensure_wireframe_line_capacity(game, total_vertex_count)) {
            size_t offset_vertices = 0;
            for (int i = 0; i < game->building_count; i++) {
                const WireframeMesh *mesh = &game->wireframe_meshes[i];
                if (!mesh->line_vertices || mesh->vertex_count <= 0) {
                    continue;
                }
                const size_t copy_floats = (size_t)mesh->vertex_count * 3U;
                SDL_memcpy(game->wireframe_line_scratch + (offset_vertices * 3U),
                           mesh->line_vertices,
                           sizeof(float) * copy_floats);
                offset_vertices += (size_t)mesh->vertex_count;
            }

            if (offset_vertices > 0) {
                Renderer_DrawLineBatch(
                    game->wireframe_line_scratch, (int)offset_vertices, (SDL_FColor){0.0f, 1.0f, 1.0f, 1.0f});
            }
        }
        PROF_stop(PROFILER_RENDER_WIREFRAMES);
    }

    miso_render_end_world(engine);
}

static void testbed_game_on_render_ui(void *const ctx, const MisoEngine *const engine) {
    TestbedGame *const game = (TestbedGame *)ctx;
    if (!game || !testbed_should_render_ui(game)) {
        return;
    }

    PROF_start(PROFILER_RENDER_UI);
    miso_render_begin_ui(engine);

    float ui_y_pos = 20.0f;

    char hover_tile_info[64];
    SDL_snprintf(hover_tile_info, sizeof(hover_tile_info), "Tile: (%d, %d)", game->hover_tile.x, game->hover_tile.y);
    testbed_render_hud_line(game, 10.0f, ui_y_pos, hover_tile_info);
    ui_y_pos += 34.0f;

    char camera_info[128];
    SDL_snprintf(camera_info,
                 sizeof(camera_info),
                 "Camera Pos: (%5.1f, %5.1f) | Zoom: %4.2f",
                 game->camera_x,
                 game->camera_y,
                 game->camera_zoom);
    testbed_render_hud_line(game, 10.0f, ui_y_pos, camera_info);
    ui_y_pos += 34.0f;

    char fps_str[128];
    if (game->debug_mode) {
        float min = 0.0f;
        float max = 0.0f;
        float avg = 0.0f;
        PROF_getFPS(&min, &avg, &max);
        SDL_snprintf(fps_str, sizeof(fps_str), "FPS: min %4.0f | avg %4.0f | max %4.0f", min, avg, max);
    } else {
        const float fps = game->frame_dt > 0.0f ? (1.0f / game->frame_dt) : 0.0f;
        SDL_snprintf(fps_str, sizeof(fps_str), "FPS %4.0f", fps);
    }
    testbed_render_hud_line(game, 10.0f, ui_y_pos, fps_str);
    ui_y_pos += 34.0f;

    if (game->debug_mode && (!game->benchmark_mode || game->benchmark_profiler_enabled)) {
        PROF_render((SDL_FPoint){10.0f, ui_y_pos});
    }

    char mouse_pos_info[64];
    SDL_snprintf(mouse_pos_info, sizeof(mouse_pos_info), "%.1f, %.1f", game->mouse_x, game->mouse_y);
    testbed_render_hud_line(game, game->mouse_x + 15.0f, game->mouse_y + 15.0f, mouse_pos_info);

    miso_render_end_ui(engine);
    PROF_stop(PROFILER_RENDER_UI);
}

static void testbed_game_on_render_debug(void *const ctx, const MisoEngine *const engine) {
    TestbedGame *const game = (TestbedGame *)ctx;
    if (!game || (game->benchmark_mode && !game->benchmark_debug_ui_enabled)) {
        return;
    }

    PROF_start(PROFILER_NUKLEAR);
    miso_debug_ui_prepare_render(engine);

    struct nk_context *nk = miso_debug_ui_get_context();
    const float ui_s = miso_debug_ui_get_scale();
    if (nk &&
        nk_begin(nk,
                 "Debug",
                 nk_rect(50 * ui_s, 400 * ui_s, 300 * ui_s, 430 * ui_s),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(nk, 25 * ui_s, 1);
        testbed_nk_labelf(nk, NK_TEXT_LEFT, "Buildings: %d", game->building_count);
        testbed_nk_labelf(nk, NK_TEXT_LEFT, "Hover: (%d, %d)", game->hover_tile.x, game->hover_tile.y);

        nk_layout_row_dynamic(nk, 30 * ui_s, 2);
        if (nk_button_label(nk, "Spawn 50")) {
            testbed_spawn_boats(game, 50);
        }
        if (nk_button_label(nk, "Toggle Wire")) {
            game->wireframe_mode = !game->wireframe_mode;
        }

        nk_layout_row_dynamic(nk, 25 * ui_s, 1);
        nk_bool dbg = game->debug_mode;
        nk_checkbox_label(nk, "Debug Mode", &dbg);
        game->debug_mode = dbg;

        nk_layout_row_dynamic(nk, 20 * ui_s, 1);
        nk_label(nk, "--- Sea Waves ---", NK_TEXT_CENTERED);
        nk_layout_row_dynamic(nk, 20 * ui_s, 1);
        testbed_nk_labelf(nk, NK_TEXT_LEFT, "Speed: %.2f", game->wave_speed);
        nk_slider_float(nk, 0.0f, &game->wave_speed, 2.0f, 0.01f);
        testbed_nk_labelf(nk, NK_TEXT_LEFT, "Amplitude: %.2f", game->wave_amplitude);
        nk_slider_float(nk, 0.0f, &game->wave_amplitude, 2.0f, 0.01f);
        testbed_nk_labelf(nk, NK_TEXT_LEFT, "Phase: %.3f", game->wave_phase);
        nk_slider_float(nk, 0.0f, &game->wave_phase, 1.0f, 0.005f);

        MisoRenderFrameStats stats = {0};
        if (miso_render_get_frame_stats(engine, &stats)) {
            const MisoRenderQueueStats *queues = stats.queues;
            const MisoRenderStreamStats *streams = stats.streams;

            nk_layout_row_dynamic(nk, 20 * ui_s, 1);
            nk_label(nk, "--- Renderer ---", NK_TEXT_CENTERED);
            testbed_nk_labelf(
                nk, NK_TEXT_LEFT, "Pass begin/end: %u / %u", stats.passes.begin_calls, stats.passes.end_calls);
            testbed_nk_labelf(
                nk, NK_TEXT_LEFT, "World/UI passes: %u / %u", stats.passes.world_passes, stats.passes.ui_passes);
            testbed_nk_labelf(nk,
                              NK_TEXT_LEFT,
                              "Sprite cmds/draws: %u / %u",
                              queues[MISO_RENDER_STATS_QUEUE_SPRITE].cmd_count,
                              queues[MISO_RENDER_STATS_QUEUE_SPRITE].draw_calls);
            testbed_nk_labelf(nk,
                              NK_TEXT_LEFT,
                              "UI text cmds/draws: %u / %u",
                              queues[MISO_RENDER_STATS_QUEUE_UI_TEXT].cmd_count,
                              queues[MISO_RENDER_STATS_QUEUE_UI_TEXT].draw_calls);
            testbed_nk_labelf(
                nk, NK_TEXT_LEFT, "Present mode: %s", testbed_present_mode_name(Renderer_GetPresentMode()));
            const Uint32 allowed_frames_in_flight = Renderer_GetAllowedFramesInFlight();
            testbed_nk_labelf(nk, NK_TEXT_LEFT, "Frames in flight: %u", allowed_frames_in_flight);
            const char *const frames_in_flight_items[] = {"1", "2", "3"};
            int frames_in_flight_index = 0;
            if (allowed_frames_in_flight >= 1U && allowed_frames_in_flight <= 3U) {
                frames_in_flight_index = (int)allowed_frames_in_flight - 1;
            }
            nk_layout_row_dynamic(nk, 24 * ui_s, 2);
            nk_label(nk, "Set frames in flight", NK_TEXT_LEFT);
            const int selected_frames_in_flight = nk_combo(nk,
                                                           frames_in_flight_items,
                                                           3,
                                                           frames_in_flight_index,
                                                           (int)(20 * ui_s),
                                                           nk_vec2(90 * ui_s, 120 * ui_s));
            if (selected_frames_in_flight != frames_in_flight_index &&
                !Renderer_SetAllowedFramesInFlight((Uint32)(selected_frames_in_flight + 1))) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Failed to set frames in flight to %d from debug UI",
                            selected_frames_in_flight + 1);
            }
            nk_layout_row_dynamic(nk, 20 * ui_s, 1);
            testbed_nk_labelf(nk, NK_TEXT_LEFT, "Pixel density: %.2f", game->pixel_ratio);
            testbed_nk_labelf(nk, NK_TEXT_LEFT, "Frame CPU: %.3f ms", stats.timing.frame_cpu_ms);
            testbed_nk_labelf(nk, NK_TEXT_LEFT, "Acquire swapchain: %.3f ms", stats.timing.acquire_swapchain_ms);
            testbed_nk_labelf(nk, NK_TEXT_LEFT, "Record commands: %.3f ms", stats.timing.record_commands_ms);
            testbed_nk_labelf(nk, NK_TEXT_LEFT, "Submit cmd buffer: %.3f ms", stats.timing.submit_ms);
            testbed_nk_labelf(nk,
                              NK_TEXT_LEFT,
                              "Draw calls world/ui/line: %u / %u / %u",
                              stats.draw_calls_world,
                              stats.draw_calls_ui,
                              stats.draw_calls_lines);
            testbed_nk_labelf(nk,
                              NK_TEXT_LEFT,
                              "Uploads total: %.2f MiB (sprite %.2f | world %.2f | line %.2f | ui %.2f | text %.2f)",
                              testbed_bytes_to_mib(stats.uploaded_bytes_total),
                              testbed_bytes_to_mib(stats.uploaded_bytes_sprite),
                              testbed_bytes_to_mib(stats.uploaded_bytes_world_geo),
                              testbed_bytes_to_mib(stats.uploaded_bytes_line),
                              testbed_bytes_to_mib(stats.uploaded_bytes_ui_geo),
                              testbed_bytes_to_mib(stats.uploaded_bytes_ui_text));
            testbed_nk_labelf(nk,
                              NK_TEXT_LEFT,
                              "Submitted instances/line verts: %u / %u",
                              stats.instances_submitted,
                              stats.line_vertices_submitted);
            testbed_nk_labelf(nk,
                              NK_TEXT_LEFT,
                              "Sprite stream: %.2f / %.2f MiB (%.1f%%) peak %.2f MiB",
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_SPRITE].used_bytes),
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_SPRITE].capacity_bytes),
                              testbed_usage_percent(streams[MISO_RENDER_STATS_STREAM_SPRITE].used_bytes,
                                                    streams[MISO_RENDER_STATS_STREAM_SPRITE].capacity_bytes),
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_SPRITE].peak_bytes));
            testbed_nk_labelf(nk,
                              NK_TEXT_LEFT,
                              "World geom stream: %.2f / %.2f MiB (%.1f%%) peak %.2f MiB",
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_WORLD_GEOMETRY].used_bytes),
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_WORLD_GEOMETRY].capacity_bytes),
                              testbed_usage_percent(streams[MISO_RENDER_STATS_STREAM_WORLD_GEOMETRY].used_bytes,
                                                    streams[MISO_RENDER_STATS_STREAM_WORLD_GEOMETRY].capacity_bytes),
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_WORLD_GEOMETRY].peak_bytes));
            testbed_nk_labelf(nk,
                              NK_TEXT_LEFT,
                              "Line stream: %.2f / %.2f MiB (%.1f%%) peak %.2f MiB",
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_LINE].used_bytes),
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_LINE].capacity_bytes),
                              testbed_usage_percent(streams[MISO_RENDER_STATS_STREAM_LINE].used_bytes,
                                                    streams[MISO_RENDER_STATS_STREAM_LINE].capacity_bytes),
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_LINE].peak_bytes));
            testbed_nk_labelf(nk,
                              NK_TEXT_LEFT,
                              "UI geom stream: %.2f / %.2f MiB (%.1f%%) peak %.2f MiB",
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_UI_GEOMETRY].used_bytes),
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_UI_GEOMETRY].capacity_bytes),
                              testbed_usage_percent(streams[MISO_RENDER_STATS_STREAM_UI_GEOMETRY].used_bytes,
                                                    streams[MISO_RENDER_STATS_STREAM_UI_GEOMETRY].capacity_bytes),
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_UI_GEOMETRY].peak_bytes));
            testbed_nk_labelf(nk,
                              NK_TEXT_LEFT,
                              "UI text vert stream: %.2f / %.2f MiB (%.1f%%) peak %.2f MiB",
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_UI_TEXT_VERT].used_bytes),
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_UI_TEXT_VERT].capacity_bytes),
                              testbed_usage_percent(streams[MISO_RENDER_STATS_STREAM_UI_TEXT_VERT].used_bytes,
                                                    streams[MISO_RENDER_STATS_STREAM_UI_TEXT_VERT].capacity_bytes),
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_UI_TEXT_VERT].peak_bytes));
            testbed_nk_labelf(nk,
                              NK_TEXT_LEFT,
                              "UI text idx stream: %.2f / %.2f MiB (%.1f%%) peak %.2f MiB",
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_UI_TEXT_INDEX].used_bytes),
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_UI_TEXT_INDEX].capacity_bytes),
                              testbed_usage_percent(streams[MISO_RENDER_STATS_STREAM_UI_TEXT_INDEX].used_bytes,
                                                    streams[MISO_RENDER_STATS_STREAM_UI_TEXT_INDEX].capacity_bytes),
                              testbed_bytes_to_mib(streams[MISO_RENDER_STATS_STREAM_UI_TEXT_INDEX].peak_bytes));
            testbed_nk_labelf(nk,
                              NK_TEXT_LEFT,
                              "Overflow sprite/world/line/ui/ui_text_v/ui_text_i: %u/%u/%u/%u/%u/%u",
                              streams[MISO_RENDER_STATS_STREAM_SPRITE].overflow_count,
                              streams[MISO_RENDER_STATS_STREAM_WORLD_GEOMETRY].overflow_count,
                              streams[MISO_RENDER_STATS_STREAM_LINE].overflow_count,
                              streams[MISO_RENDER_STATS_STREAM_UI_GEOMETRY].overflow_count,
                              streams[MISO_RENDER_STATS_STREAM_UI_TEXT_VERT].overflow_count,
                              streams[MISO_RENDER_STATS_STREAM_UI_TEXT_INDEX].overflow_count);
        }
    }
    nk_end(nk);
    miso_debug_ui_render(engine);
    PROF_stop(PROFILER_NUKLEAR);
}

static MisoResult testbed_game_on_save(const void *const game_ctx,
                                       MisoByteBuffer *const out_payload,
                                       uint32_t *const out_payload_version) {
    (void)game_ctx;
    (void)out_payload;
    (void)out_payload_version;
    return MISO_ERR_UNSUPPORTED;
}

static MisoResult testbed_game_on_load(void *const game_ctx,
                                       const uint8_t *const payload,
                                       const size_t payload_size,
                                       const uint32_t payload_version) {
    (void)game_ctx;
    (void)payload;
    (void)payload_size;
    (void)payload_version;
    return MISO_ERR_UNSUPPORTED;
}

static void testbed_game_on_reset(void *const game_ctx) {
    (void)game_ctx;
}

static uint64_t testbed_game_state_hash(const void *const game_ctx) {
    const TestbedGame *const game = (const TestbedGame *)game_ctx;
    if (!game) {
        return 0;
    }
    uint64_t hash = 1469598103934665603ULL;
    const uint8_t *const bytes = (const uint8_t *)game;
    for (size_t i = 0; i < sizeof(*game); i++) {
        hash ^= (uint64_t)bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static const MisoGameHooks g_testbed_game_hooks = {
    .on_event = testbed_game_on_event,
    .on_sim_tick = testbed_game_on_sim_tick,
    .on_render_world = testbed_game_on_render_world,
    .on_render_ui = testbed_game_on_render_ui,
    .on_render_debug = testbed_game_on_render_debug,
    .on_save = testbed_game_on_save,
    .on_load = testbed_game_on_load,
    .on_reset = testbed_game_on_reset,
    .on_state_hash = testbed_game_state_hash,
};

static void testbed_populate_demo_map(const TestbedGame *const game) {
    for (int y = 0; y < game->tilemap->height; y++) {
        for (int x = 0; x < game->tilemap->width; x++) {
            const int i = y * game->tilemap->width + x;
            int tile_index = (i % 2) ? 0 : 36;

            if (i > (game->tilemap->width * game->tilemap->height) -
                        (game->tilemap->width / 2) * (game->tilemap->height / 2)) {
                tile_index = TILE_PLACEHOLDER_SEA;
                Tilemap_SetFlags(game->tilemap, x, y, TILE_FLAG_WATER);
            }

            Tilemap_SetTile(game->tilemap, x, y, tile_index);
        }
    }
}

static void testbed_clear_buildings(TestbedGame *const game) {
    if (!game) {
        return;
    }

    for (int i = 0; i < game->building_count; i++) {
        SDL_free(game->wireframe_meshes[i].line_vertices);
        game->wireframe_meshes[i].line_vertices = nullptr;
        game->wireframe_meshes[i].vertex_count = 0;
    }
    game->building_count = 0;
}

static void testbed_reset_demo_scene(TestbedGame *const game, const int spawn_count) {
    if (!game || !game->tilemap) {
        return;
    }

    testbed_clear_buildings(game);

    const size_t tile_count = (size_t)game->tilemap->width * (size_t)game->tilemap->height;
    SDL_memset(game->tilemap->occupied, false, tile_count * sizeof(bool));
    SDL_memset(game->tilemap->flags, 0, tile_count * sizeof(uint8_t));
    testbed_populate_demo_map(game);

    game->hover_tile = (SDL_Point){-1, -1};
    if (spawn_count > 0) {
        testbed_spawn_boats(game, spawn_count);
    }
}

MisoResult testbed_game_create(MisoEngine *engine, TestbedGame **out_game) {
    if (!engine || !out_game) {
        return MISO_ERR_INVALID_ARG;
    }

    TestbedGame *game = SDL_calloc(1, sizeof(TestbedGame));
    if (!game) {
        return MISO_ERR_OUT_OF_MEMORY;
    }

    game->engine = engine;
    game->running = true;
    game->vsync = true;
    game->screen_width = WINDOW_WIDTH;
    game->screen_height = WINDOW_HEIGHT;
    game->pixel_ratio = 1.0f;
    game->hover_tile = (SDL_Point){-1, -1};
    game->game_clock = GameClock_create();
    game->wave_speed = 0.2f;
    game->wave_amplitude = 0.5f;
    game->wave_phase = 0.1f;
    game->benchmark_mode = false;
    game->benchmark_debug_ui_enabled = true;
    game->benchmark_profiler_enabled = true;
    game->benchmark_diagnostic_mode = TESTBED_BENCH_DIAGNOSTIC_DEFAULT;
    game->benchmark_camera_state = TESTBED_BENCH_CAMERA_ZOOM_IN_CENTER;

    if (!testbed_ensure_building_instance_capacity(game, MAX_BUILDINGS + 1)) {
        SDL_free(game);
        return MISO_ERR_OUT_OF_MEMORY;
    }

    game->camera_id = miso_camera_create(engine);
    if (game->camera_id == 0) {
        SDL_free(game);
        return MISO_ERR_INIT;
    }
    testbed_sync_window_metrics(game);
    game->camera_x = 10.0f;
    game->camera_y = 10.0f;
    game->camera_zoom = 2.0f;
    miso_camera_set_position(engine, game->camera_id, game->camera_x, game->camera_y);
    miso_camera_set_zoom(engine, game->camera_id, game->camera_zoom);
    game->benchmark_camera_x = game->camera_x;
    game->benchmark_camera_y = game->camera_y;
    game->benchmark_camera_zoom = game->camera_zoom;

    if (miso_debug_ui_init(engine, "/Users/arnau/Library/Fonts/JetBrainsMono-Regular.ttf", 14.0f) != MISO_OK) {
        SDL_Log("Warning: failed to init debug UI");
    }

    const MisoResult font_result =
        miso_render_load_font(engine, "/Users/arnau/Library/Fonts/JetBrainsMono-Regular.ttf", 24.0f, &game->hud_font);
    if (font_result != MISO_OK) {
        miso_debug_ui_shutdown();
        SDL_free(game);
        return font_result;
    }

    game->profiler_font = TTF_OpenFont("/Users/arnau/Library/Fonts/JetBrainsMono-Regular.ttf", 24.0f);
    if (!game->profiler_font) {
        miso_render_destroy_font(engine, game->hud_font);
        miso_debug_ui_shutdown();
        SDL_free(game);
        return MISO_ERR_IO;
    }

    TTF_TextEngine *text_engine = Renderer_GetTextEngine();
    if (text_engine) {
        PROF_initUI(text_engine, game->profiler_font);
    }

    char resource_path[512] = {0};
    game->tileset = Tileset_Load(getResourcePath(resource_path, "isometric-sheet.png"), TILE_SIZE, TILE_SIZE);
    if (!game->tileset) {
        testbed_game_destroy(game);
        return MISO_ERR_IO;
    }

    game->tilemap = Tilemap_Create(MAP_SIZE_X, MAP_SIZE_Y, game->tileset);
    if (!game->tilemap) {
        testbed_game_destroy(game);
        return MISO_ERR_OUT_OF_MEMORY;
    }

    testbed_populate_demo_map(game);
    *out_game = game;
    return MISO_OK;
}

void testbed_game_destroy(TestbedGame *game) {
    if (!game) {
        return;
    }

    Renderer_SetUploadSuppressed(false);

    for (int i = 0; i < game->building_count; i++) {
        SDL_free(game->wireframe_meshes[i].line_vertices);
        game->wireframe_meshes[i].line_vertices = nullptr;
        game->wireframe_meshes[i].vertex_count = 0;
    }

    SDL_free(game->wireframe_line_scratch);
    game->wireframe_line_scratch = nullptr;
    game->wireframe_line_scratch_capacity_vertices = 0U;

    SDL_free(game->building_instances);
    game->building_instances = nullptr;
    game->building_instances_capacity = 0;

    if (game->tilemap) {
        Tilemap_Destroy(game->tilemap);
    }
    if (game->tileset) {
        Tileset_Destroy(game->tileset);
    }

    PROF_deinitUI();
    if (game->profiler_font) {
        TTF_CloseFont(game->profiler_font);
    }

    miso_render_destroy_font(game->engine, game->hud_font);
    miso_debug_ui_shutdown();
    SDL_free(game);
}

void testbed_game_frame_begin(TestbedGame *game, const float real_dt_seconds) {
    if (!game) {
        return;
    }

    testbed_sync_window_metrics(game);

    if (game->benchmark_mode) {
        miso_camera_set_position(game->engine, game->camera_id, game->benchmark_camera_x, game->benchmark_camera_y);
        miso_camera_set_zoom(game->engine, game->camera_id, game->benchmark_camera_zoom);
        game->camera_x = game->benchmark_camera_x;
        game->camera_y = game->benchmark_camera_y;
        game->camera_zoom = game->benchmark_camera_zoom;
    }

    game->frame_dt = real_dt_seconds;
    GameClock_update(&game->game_clock, real_dt_seconds);
    PROF_frameStart();
    PROF_start(PROFILER_EVENT_HANDLING);
    if (!game->benchmark_mode || game->benchmark_debug_ui_enabled) {
        miso_debug_ui_begin_input();
    }
}

void testbed_game_frame_end_events(TestbedGame *game) {
    if (!game) {
        return;
    }
    if (!game->benchmark_mode || game->benchmark_debug_ui_enabled) {
        miso_debug_ui_end_input();
    }
    PROF_stop(PROFILER_EVENT_HANDLING);
}

void testbed_game_frame_end(const TestbedGame *const game) {
    if (game && game->engine) {
        MisoRenderFrameStats stats = {0};
        if (miso_render_get_frame_stats(game->engine, &stats)) {
            PROF_setDuration(PROFILER_WAIT_FRAME, stats.timing.acquire_swapchain_ms);
            PROF_setDuration(PROFILER_GPU, stats.timing.submit_ms);
        }
    }
    PROF_frameEnd();
}

void testbed_game_enable_benchmark_mode(TestbedGame *const game, const bool enabled) {
    if (!game) {
        return;
    }
    game->benchmark_mode = enabled;
    if (!enabled) {
        game->benchmark_diagnostic_mode = TESTBED_BENCH_DIAGNOSTIC_DEFAULT;
        game->benchmark_debug_ui_enabled = true;
        game->benchmark_profiler_enabled = true;
        Renderer_SetUploadSuppressed(false);
    }
}

void testbed_game_set_benchmark_debug_ui(TestbedGame *const game, const bool enabled) {
    if (!game) {
        return;
    }
    game->benchmark_debug_ui_enabled = enabled;
}

void testbed_game_set_benchmark_profiler(TestbedGame *const game, const bool enabled) {
    if (!game) {
        return;
    }
    game->benchmark_profiler_enabled = enabled;
    game->debug_mode = enabled;
}

void testbed_game_set_benchmark_wireframe(TestbedGame *const game, const bool enabled) {
    if (!game) {
        return;
    }
    game->wireframe_mode = enabled;
}

void testbed_game_set_benchmark_diagnostic_mode(TestbedGame *const game, const TestbedBenchDiagnosticMode mode) {
    if (!game) {
        return;
    }
    game->benchmark_diagnostic_mode = mode;
}

void testbed_game_set_benchmark_camera_state(TestbedGame *const game, const TestbedBenchCameraState camera_state) {
    if (!game) {
        return;
    }
    game->benchmark_camera_state = camera_state;
    testbed_apply_benchmark_camera_preset(game);
}

void testbed_game_set_benchmark_upload_suppressed(TestbedGame *const game, const bool enabled) {
    if (!game) {
        return;
    }
    Renderer_SetUploadSuppressed(enabled);
}

void testbed_game_reset_benchmark_scene(TestbedGame *const game, const int spawn_count) {
    if (!game) {
        return;
    }
    testbed_reset_demo_scene(game, spawn_count);
    testbed_apply_benchmark_camera_preset(game);
}

bool testbed_game_is_running(const TestbedGame *const game) {
    return game && game->running;
}

const MisoGameHooks *testbed_game_hooks(void) {
    return &g_testbed_game_hooks;
}
