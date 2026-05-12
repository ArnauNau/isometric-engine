#include "testbed_game.h"

#include "game_clock.h"
#include "miso_camera.h"
#include "miso_debug_ui.h"
#include "miso_iso.h"
#include "miso_profiler.h"
#include "miso_render.h"
#include "miso_render_diagnostics.h"
#include "miso_text.h"
#include "miso_tile_scene.h"
#include "vendored/nuklear/nuklear.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stddef.h>

#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080
#define TILE_SIZE 32
#define MAP_SIZE_X 70
#define MAP_SIZE_Y 50
#define MAX_BUILDINGS 512
#define MAX_AGENTS 16777216
#define TESTBED_HUD_TEXT_COUNT 4

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

typedef enum TestbedBoatDirection {
    TESTBED_BOAT_DIR_SE = 0,
    TESTBED_BOAT_DIR_SW,
    TESTBED_BOAT_DIR_NW,
    TESTBED_BOAT_DIR_NE,
    TESTBED_BOAT_DIR_COUNT
} TestbedBoatDirection;

typedef struct TestbedAgent {
    int x;
    int y;
    TestbedBoatDirection direction;
    MisoTileObjectId object_id;
} TestbedAgent;

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

    MisoTextureHandle tile_texture;
    MisoTileScene *tile_scene;
    MisoTilemap *terrain;
    MisoTileOverlay *tile_tint_overlay;
    bool boat_placement_overlay_enabled;
    TestbedAgentMode agent_mode;

    int building_count;
    RenderableComponent renderables[MAX_BUILDINGS];
    TransformComponent transforms[MAX_BUILDINGS];
    BuildingComponent buildings[MAX_BUILDINGS];
    WireframeMesh wireframe_meshes[MAX_BUILDINGS];
    float *wireframe_line_scratch;
    size_t wireframe_line_scratch_capacity_vertices;
    int agent_count;
    TestbedAgent agents[MAX_AGENTS];
    MisoSpriteInstance *agent_render_instances;
    size_t agent_render_instance_capacity;
    TestbedAgentFrameMetrics agent_metrics;

    MisoFontHandle hud_font;
    MisoTextHandle hud_texts[TESTBED_HUD_TEXT_COUNT];
    MisoProfilerCategoryId profiler_render_map;
    MisoProfilerCategoryId profiler_render_wireframes;

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

static const char *testbed_present_mode_name(const MisoRenderPresentMode mode) {
    switch (mode) {
    case MISO_RENDER_PRESENT_VSYNC:
        return "VSYNC";
    case MISO_RENDER_PRESENT_MAILBOX:
        return "MAILBOX";
    case MISO_RENDER_PRESENT_IMMEDIATE:
        return "IMMEDIATE";
    default:
        return "UNKNOWN";
    }
}

static const char *testbed_vsync_acquire_mode_name(const MisoRenderVSyncAcquireMode mode) {
    switch (mode) {
    case MISO_RENDER_VSYNC_ACQUIRE_BLOCKING:
        return "BLOCKING";
    case MISO_RENDER_VSYNC_ACQUIRE_PASSTHROUGH:
        return "PASSTHROUGH";
    default:
        return "UNKNOWN";
    }
}

const char *testbed_agent_mode_name(const TestbedAgentMode mode) {
    switch (mode) {
    case TESTBED_AGENT_MODE_STATIC:
        return "static";
    case TESTBED_AGENT_MODE_MOVE:
        return "move";
    case TESTBED_AGENT_MODE_MOVE_NO_DRAW:
        return "move-no-draw";
    case TESTBED_AGENT_MODE_LEGACY_MOVE:
        return "legacy-move";
    case TESTBED_AGENT_MODE_LEGACY_MOVE_NO_DRAW:
        return "legacy-move-no-draw";
    case TESTBED_AGENT_MODE_RENDER_ONLY:
        return "render-only";
    default:
        return "unknown";
    }
}

static bool testbed_agent_mode_no_draw(const TestbedAgentMode mode) {
    return mode == TESTBED_AGENT_MODE_MOVE_NO_DRAW || mode == TESTBED_AGENT_MODE_LEGACY_MOVE_NO_DRAW;
}

static bool testbed_agent_mode_uses_legacy_movement(const TestbedAgentMode mode) {
    return mode == TESTBED_AGENT_MODE_LEGACY_MOVE || mode == TESTBED_AGENT_MODE_LEGACY_MOVE_NO_DRAW;
}

static float testbed_elapsed_ms(const Uint64 start, const Uint64 end) {
    const Uint64 freq = SDL_GetPerformanceFrequency();
    if (freq == 0U || end <= start) {
        return 0.0f;
    }
    return (float)(((double)(end - start) * 1000.0) / (double)freq);
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

static void testbed_sync_benchmark_debug_mode(TestbedGame *const game) {
    if (!game) {
        return;
    }

    if (!game->benchmark_mode) {
        return;
    }

    game->debug_mode = game->benchmark_debug_ui_enabled || game->benchmark_profiler_enabled;
}

static const char *testbed_get_resource_path(char *const buffer, const char *const relative_path) {
    if (!buffer || !relative_path) {
        return NULL;
    }

    SDL_snprintf(buffer, 512, "%s../../../../%s", SDL_GetBasePath(), relative_path);
    return buffer;
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
        miso_camera_set_viewport_normalized(game->engine, game->camera_id, 0.0f, 0.0f, 1.0f, 1.0f);
    }
}

static const MisoIsoMapDesc *testbed_map_desc(const TestbedGame *const game) {
    return game && game->tile_scene ? miso_tile_scene_get_desc(game->tile_scene) : nullptr;
}

static bool testbed_tile_in_bounds(const TestbedGame *const game, const int tx, const int ty) {
    const MisoIsoMapDesc *const desc = testbed_map_desc(game);
    return desc && tx >= 0 && ty >= 0 && tx < desc->width_tiles && ty < desc->height_tiles;
}

static bool testbed_tile_to_world_top_left(
    const TestbedGame *const game, const int tx, const int ty, float *const out_world_x, float *const out_world_y) {
    const MisoIsoMapDesc *const desc = testbed_map_desc(game);
    if (!desc || !out_world_x || !out_world_y) {
        return false;
    }

    miso_iso_tile_to_world(desc, tx, ty, out_world_x, out_world_y);
    return true;
}

static float testbed_tile_depth(const TestbedGame *const game, const float tx, const float ty) {
    return game && game->tile_scene ? miso_tile_scene_depth_at_tile(game->tile_scene, tx, ty) : 0.0f;
}

static bool testbed_is_tile_free(const TestbedGame *const game, const int tx, const int ty) {
    if (!game || !game->tile_scene || !game->terrain || !testbed_tile_in_bounds(game, tx, ty)) {
        return false;
    }

    const MisoTilePlacementQuery query = {
        .tile_x = tx,
        .tile_y = ty,
        .required_tile_flags = MISO_TILE_FLAG_WATER,
        .forbidden_tile_flags = MISO_TILE_FLAG_BLOCKS_AGENTS,
        .footprint = {.width = 1, .height = 1, .anchor_x = 0, .anchor_y = 0},
        .occupied_mask = MISO_TILE_OCCUPANCY_OBJECT | MISO_TILE_OCCUPANCY_AGENT,
    };
    return miso_tile_scene_check_placement(game->tile_scene, game->terrain, &query) == MISO_TILE_PLACE_OK;
}

static void testbed_update_boat_placement_overlay(TestbedGame *const game) {
    const MisoIsoMapDesc *const desc = testbed_map_desc(game);
    if (!game || !game->tile_scene || !game->terrain || !game->tile_tint_overlay || !desc) {
        return;
    }

    if (!game->boat_placement_overlay_enabled) {
        miso_tile_overlay_clear(game->tile_tint_overlay, 0x00000000U);
        return;
    }

    const MisoTileFootprint footprint = {.width = 1, .height = 1, .anchor_x = 0, .anchor_y = 0};
    for (int y = 0; y < desc->height_tiles; y++) {
        for (int x = 0; x < desc->width_tiles; x++) {
            const MisoTilePlacementQuery query = {
                .tile_x = x,
                .tile_y = y,
                .required_tile_flags = MISO_TILE_FLAG_WATER,
                .forbidden_tile_flags = MISO_TILE_FLAG_BLOCKS_AGENTS,
                .footprint = footprint,
                .occupied_mask = MISO_TILE_OCCUPANCY_OBJECT | MISO_TILE_OCCUPANCY_AGENT,
            };
            const MisoTilePlacementProblemMask result =
                miso_tile_scene_check_placement(game->tile_scene, game->terrain, &query);
            const uint32_t color = result == MISO_TILE_PLACE_OK ? 0x00FF00B0U : 0xFF0000A0U;
            (void)miso_tile_overlay_set_tile_rgba8(game->tile_tint_overlay, x, y, color);
        }
    }
}

static void testbed_apply_benchmark_camera_preset(TestbedGame *const game) {
    const MisoIsoMapDesc *const desc = testbed_map_desc(game);
    if (!game || !desc || game->camera_id == 0) {
        return;
    }

    const int center_x = desc->width_tiles / 2;
    const int center_y = desc->height_tiles / 2;

    float world_x = 0.0f;
    float world_y = 0.0f;
    testbed_tile_to_world_top_left(game, center_x, center_y, &world_x, &world_y);

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
        return game ? !testbed_agent_mode_no_draw(game->agent_mode) : true;
    }

    if (testbed_agent_mode_no_draw(game->agent_mode)) {
        return false;
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
    if (testbed_agent_mode_no_draw(game->agent_mode)) {
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
    if (!testbed_tile_in_bounds(game, tile_x, tile_y)) {
        return;
    }

    float world_x = 0.0f;
    float world_y = 0.0f;
    testbed_tile_to_world_top_left(game, tile_x, tile_y, &world_x, &world_y);

    const MisoIsoMapDesc *const desc = testbed_map_desc(game);
    const float iso_w = (float)desc->tile_w_px;
    const float iso_h = (float)desc->tile_h_px * 0.5f;

    const float top_x = world_x + iso_w / 2.0f;
    const float top_y = world_y;
    const float right_x = world_x + iso_w;
    const float right_y = world_y + iso_h / 2.0f;
    const float bottom_x = world_x + iso_w / 2.0f;
    const float bottom_y = world_y + iso_h;
    const float left_x = world_x;
    const float left_y = world_y + iso_h / 2.0f;

    const float depth = testbed_tile_depth(game, (float)tile_x, (float)tile_y) - 0.002f;
    constexpr float beacon_height = 200.0f;
    float line_vertices[30] = {0.0f};
    int vertex_count = 0;
    const int line_capacity = (int)(SDL_arraysize(line_vertices) / 3U);
    testbed_wireframe_push_line(
        line_vertices, line_capacity, &vertex_count, top_x, top_y, depth, right_x, right_y, depth);
    testbed_wireframe_push_line(
        line_vertices, line_capacity, &vertex_count, right_x, right_y, depth, bottom_x, bottom_y, depth);
    testbed_wireframe_push_line(
        line_vertices, line_capacity, &vertex_count, bottom_x, bottom_y, depth, left_x, left_y, depth);
    testbed_wireframe_push_line(
        line_vertices, line_capacity, &vertex_count, left_x, left_y, depth, top_x, top_y, depth);
    testbed_wireframe_push_line(
        line_vertices, line_capacity, &vertex_count, top_x, top_y, depth, top_x, top_y - beacon_height, depth);
    const uint32_t rgba8 = ((uint32_t)(color.r * 255.0f) << 24) | ((uint32_t)(color.g * 255.0f) << 16) |
                           ((uint32_t)(color.b * 255.0f) << 8) | (uint32_t)(color.a * 255.0f);
    miso_render_submit_world_lines(game->engine, line_vertices, vertex_count, rgba8);
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

static bool testbed_ensure_agent_render_capacity(TestbedGame *const game, const size_t capacity) {
    if (!game || capacity == 0U || capacity <= game->agent_render_instance_capacity) {
        return true;
    }

    size_t new_capacity = game->agent_render_instance_capacity > 0U ? game->agent_render_instance_capacity : 256U;
    while (new_capacity < capacity) {
        new_capacity *= 2U;
    }

    MisoSpriteInstance *const new_instances =
        SDL_realloc(game->agent_render_instances, sizeof(MisoSpriteInstance) * new_capacity);
    if (!new_instances) {
        return false;
    }

    game->agent_render_instances = new_instances;
    game->agent_render_instance_capacity = new_capacity;
    return true;
}

static void testbed_render_agent_instances(TestbedGame *const game) {
    const MisoIsoMapDesc *const desc = testbed_map_desc(game);
    if (!game || !desc || game->agent_count <= 0 ||
        !testbed_ensure_agent_render_capacity(game, (size_t)game->agent_count)) {
        return;
    }

    const float tile_w = (float)desc->tile_w_px;
    const float tile_h = (float)desc->tile_h_px;
    const float iso_w = tile_w;
    const float iso_h = tile_h * 0.5f;
    const float start_x = (float)(desc->height_tiles - 1) * iso_w * 0.5f;
    constexpr float source_w_cells = 2.0f;
    constexpr float source_h_cells = 3.0f;
    const float sprite_w = source_w_cells * tile_w;
    const float sprite_h = source_h_cells * tile_h;
    constexpr uint32_t atlas_columns = 8U;
    constexpr uint32_t atlas_rows = 12U;
    const float tex_w = (float)(atlas_columns * (uint32_t)TILE_SIZE);
    const float tex_h = (float)(atlas_rows * (uint32_t)TILE_SIZE);
    const uint32_t col = TILE_PLACEHOLDER_BOAT % atlas_columns;
    const uint32_t row = TILE_PLACEHOLDER_BOAT / atlas_columns;

    for (int i = 0; i < game->agent_count; i++) {
        const TestbedAgent *const agent = &game->agents[i];
        float world_x = start_x + (float)(agent->x - agent->y) * (iso_w * 0.5f);
        float world_y = (float)(agent->x + agent->y) * (iso_h * 0.5f);
        world_y -= tile_h;
        world_y -= source_h_cells * iso_h;

        game->agent_render_instances[i] = (MisoSpriteInstance){
            .x = world_x,
            .y = world_y,
            .z = testbed_tile_depth(game, (float)agent->x, (float)agent->y) - 0.001f,
            .flags = 0.0f,
            .w = sprite_w,
            .h = sprite_h,
            .tile_x = (float)agent->x,
            .tile_y = (float)agent->y,
            .u = ((float)col * tile_w) / tex_w,
            .v = ((float)row * tile_h) / tex_h,
            .uw = sprite_w / tex_w,
            .vh = sprite_h / tex_h,
        };
    }

    miso_render_submit_sprites(game->engine, game->tile_texture, game->agent_render_instances, game->agent_count);
}

static void testbed_boat_direction_delta(const TestbedBoatDirection direction, int *const out_dx, int *const out_dy) {
    int dx = 0;
    int dy = 0;
    switch (direction) {
    case TESTBED_BOAT_DIR_SE:
        dx = 1;
        dy = 0;
        break;
    case TESTBED_BOAT_DIR_SW:
        dx = 0;
        dy = 1;
        break;
    case TESTBED_BOAT_DIR_NW:
        dx = -1;
        dy = 0;
        break;
    case TESTBED_BOAT_DIR_NE:
        dx = 0;
        dy = -1;
        break;
    default:
        break;
    }
    if (out_dx) {
        *out_dx = dx;
    }
    if (out_dy) {
        *out_dy = dy;
    }
}

static TestbedBoatDirection testbed_boat_turn_right(const TestbedBoatDirection direction) {
    return (TestbedBoatDirection)(((int)direction + 1) % (int)TESTBED_BOAT_DIR_COUNT);
}

static bool testbed_place_boat_object(
    TestbedGame *const game, const int x, const int y, const Uint64 game_ref, MisoTileObjectId *const out_id) {
    if (!game || !game->tile_scene || !game->terrain || !testbed_is_tile_free(game, x, y)) {
        return false;
    }

    const MisoTileObjectDesc object = {
        .type_id = TILE_PLACEHOLDER_BOAT,
        .tile_x = x,
        .tile_y = y,
        .footprint = {.width = 1, .height = 1, .anchor_x = 0, .anchor_y = 0},
        .visual_id = TILE_PLACEHOLDER_BOAT,
        .occupancy_mask = MISO_TILE_OCCUPANCY_AGENT,
        .pickable = true,
        .game_ref = game_ref,
    };
    return miso_tile_scene_place_object(game->tile_scene, game->terrain, &object, out_id) == MISO_OK;
}

static void testbed_spawn_boat(TestbedGame *const game, const int x, const int y) {
    const MisoIsoMapDesc *const desc = testbed_map_desc(game);
    if (!game || !game->tile_scene || !game->terrain || !desc || game->agent_count >= MAX_AGENTS) {
        return;
    }

    MisoTileObjectId object_id = 0;
    const int agent_index = game->agent_count;
    if (game->agent_mode != TESTBED_AGENT_MODE_RENDER_ONLY &&
        !testbed_place_boat_object(game, x, y, (Uint64)(agent_index + 1), &object_id)) {
        return;
    }

    game->agents[agent_index] = (TestbedAgent){
        .x = x,
        .y = y,
        .direction = TESTBED_BOAT_DIR_SE,
        .object_id = object_id,
    };
    game->agent_count++;

    if (game->building_count >= MAX_BUILDINGS) {
        return;
    }

    game->renderables[game->building_count].tile_index = TILE_PLACEHOLDER_BOAT;
    game->renderables[game->building_count].sprite_w = 2;
    game->renderables[game->building_count].sprite_h = 3;
    game->transforms[game->building_count].x = x;
    game->transforms[game->building_count].y = y;
    game->buildings[game->building_count].width = 1;
    game->buildings[game->building_count].length = 1;

    constexpr float b_h_ = 3.0f;
    constexpr float b_w_ = 1.0f;
    const float tile_w = (float)desc->tile_w_px;
    const float tile_h = (float)desc->tile_h_px;
    const float iso_w = tile_w;
    const float iso_h = tile_h * 0.5f;
    const float start_x = (float)(desc->height_tiles - 1) * iso_w * 0.5f;
    constexpr float start_y = 0.0f;
    const float iso_x = start_x + (float)(x - y) * iso_w * 0.5f - (b_w_ - 1.0f) * iso_w * 0.5f;
    const float iso_y = start_y + (float)(x + y) * iso_h * 0.5f - tile_h - b_h_ * iso_h;
    const float wire_depth = testbed_tile_depth(game, (float)x, (float)y) - 0.0015f;
    game->wireframe_meshes[game->building_count] =
        testbed_build_wireframe_mesh(iso_x, iso_y, iso_w, iso_h, 1, 1, 2, 3, wire_depth);

    game->building_count++;
}

static void testbed_spawn_boats(TestbedGame *const game, const int amount) {
    const MisoIsoMapDesc *const desc = testbed_map_desc(game);
    if (!game || !game->terrain || !desc) {
        return;
    }

    int count = 0;
    bool changed = false;
    for (int y = desc->height_tiles - 1; y >= 0; y--) {
        for (int x = 0; x < desc->width_tiles; x++) {
            if (count >= amount || game->agent_count >= MAX_AGENTS) {
                goto done;
            }

            if (testbed_is_tile_free(game, x, y)) {
                const int previous_agent_count = game->agent_count;
                testbed_spawn_boat(game, x, y);
                if (game->agent_count == previous_agent_count) {
                    continue;
                }

                count++;
                changed = true;
            }
        }
    }

done:
    if (changed) {
        testbed_update_boat_placement_overlay(game);
    }
}

static void testbed_refresh_hover_tile(TestbedGame *const game) {
    if (!game || !game->tile_scene || game->camera_id == 0) {
        return;
    }

    const MisoVec2 world_position =
        miso_camera_screen_to_world(game->engine, game->camera_id, (int)game->mouse_x, (int)game->mouse_y);
    float tile_x = 0.0f;
    float tile_y = 0.0f;
    if (!miso_tile_scene_world_to_tile(game->tile_scene, world_position.x, world_position.y, &tile_x, &tile_y)) {
        return;
    }
    game->hover_tile = (SDL_Point){(int)SDL_floorf(tile_x), (int)SDL_floorf(tile_y)};
}

static void testbed_game_on_event(void *const ctx, const MisoEvent *const event) {
    TestbedGame *const game = (TestbedGame *)ctx;
    if (!game || !event) {
        return;
    }

    if (!game->benchmark_mode || game->benchmark_debug_ui_enabled) {
        miso_profiler_end(game->engine, MISO_PROFILER_ENGINE_EVENTS);
        miso_profiler_begin(game->engine, MISO_PROFILER_ENGINE_DEBUG_UI);
        const bool consumed = miso_debug_ui_feed_event(event);
        miso_profiler_end(game->engine, MISO_PROFILER_ENGINE_DEBUG_UI);
        miso_profiler_begin(game->engine, MISO_PROFILER_ENGINE_EVENTS);
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
        case SDLK_B:
            if (event->data.key.repeat) {
                break;
            }
            game->boat_placement_overlay_enabled = !game->boat_placement_overlay_enabled;
            testbed_update_boat_placement_overlay(game);
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
            miso_render_set_vsync(game->engine, game->vsync);
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
            if (game->agent_count < MAX_AGENTS && testbed_is_tile_free(game, game->hover_tile.x, game->hover_tile.y)) {
                testbed_spawn_boat(game, game->hover_tile.x, game->hover_tile.y);
                testbed_update_boat_placement_overlay(game);
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

static void testbed_agent_try_step(TestbedGame *const game, TestbedAgent *const agent, const int agent_index) {
    if (!game || !agent) {
        return;
    }

    int dx = 0;
    int dy = 0;
    testbed_boat_direction_delta(agent->direction, &dx, &dy);
    const int next_x = agent->x + dx;
    const int next_y = agent->y + dy;

    game->agent_metrics.move_attempts++;

    if (!testbed_is_tile_free(game, next_x, next_y)) {
        agent->direction = testbed_boat_turn_right(agent->direction);
        game->agent_metrics.turns++;
        return;
    }

    if (game->agent_mode == TESTBED_AGENT_MODE_RENDER_ONLY) {
        agent->x = next_x;
        agent->y = next_y;
        game->agent_metrics.move_successes++;
        return;
    }

    const Uint64 api_start = SDL_GetPerformanceCounter();
    if (!testbed_agent_mode_uses_legacy_movement(game->agent_mode)) {
        const MisoTileMoveQuery move_query = {
            .object_id = agent->object_id,
            .tile_x = next_x,
            .tile_y = next_y,
            .required_tile_flags = MISO_TILE_FLAG_WATER,
            .forbidden_tile_flags = MISO_TILE_FLAG_BLOCKS_AGENTS,
            .blocked_occupancy_mask = MISO_TILE_OCCUPANCY_OBJECT | MISO_TILE_OCCUPANCY_AGENT,
        };
        if (miso_tile_scene_move_object(game->tile_scene, game->terrain, &move_query, nullptr) == MISO_OK) {
            game->agent_metrics.movement_api_ms += testbed_elapsed_ms(api_start, SDL_GetPerformanceCounter());
            agent->x = next_x;
            agent->y = next_y;
            game->agent_metrics.move_successes++;
            return;
        }
    } else {
        MisoTileObjectId new_object_id = 0;
        const MisoResult remove_result = miso_tile_scene_remove_object(game->tile_scene, agent->object_id);
        if (remove_result == MISO_OK) {
            if (testbed_place_boat_object(game, next_x, next_y, (Uint64)(agent_index + 1), &new_object_id)) {
                game->agent_metrics.movement_api_ms += testbed_elapsed_ms(api_start, SDL_GetPerformanceCounter());
                agent->x = next_x;
                agent->y = next_y;
                agent->object_id = new_object_id;
                game->agent_metrics.move_successes++;
                return;
            }

            (void)testbed_place_boat_object(game, agent->x, agent->y, (Uint64)(agent_index + 1), &new_object_id);
            if (new_object_id != 0) {
                agent->object_id = new_object_id;
            }
        }
    }

    game->agent_metrics.movement_api_ms += testbed_elapsed_ms(api_start, SDL_GetPerformanceCounter());
    game->agent_metrics.move_failures++;
    agent->direction = testbed_boat_turn_right(agent->direction);
    game->agent_metrics.turns++;
}

static void testbed_game_on_sim_tick(void *const ctx, const float fixed_dt_seconds) {
    (void)fixed_dt_seconds;
    TestbedGame *const game = (TestbedGame *)ctx;
    if (!game) {
        return;
    }

    game->agent_metrics.ticks_this_frame++;

    if (game->agent_mode == TESTBED_AGENT_MODE_STATIC || game->agent_count <= 0) {
        return;
    }

    const Uint64 sim_start = SDL_GetPerformanceCounter();
    for (int i = 0; i < game->agent_count; i++) {
        testbed_agent_try_step(game, &game->agents[i], i);
    }
    game->agent_metrics.agent_sim_ms += testbed_elapsed_ms(sim_start, SDL_GetPerformanceCounter());

    if (game->boat_placement_overlay_enabled && game->agent_mode != TESTBED_AGENT_MODE_RENDER_ONLY) {
        testbed_update_boat_placement_overlay(game);
    }
}

static void
testbed_render_hud_line(TestbedGame *game, const int slot, const float x, const float y, const char *const text) {
    if (!game || !text ||
        !testbed_is_point_in_rect(x, y, &(SDL_FRect){0, 0, (float)game->screen_width, (float)game->screen_height})) {
        return;
    }
    if (slot < 0 || slot >= TESTBED_HUD_TEXT_COUNT) {
        return;
    }

    constexpr float pad_x = 6.0f;
    constexpr float pad_y = 4.0f;
    constexpr float line_h = 28.0f;
    const MisoTextHandle text_handle = game->hud_texts[slot];
    if (!miso_text_is_valid(game->engine, text_handle)) {
        return;
    }

    (void)miso_text_set_string(game->engine, text_handle, text);
    MisoTextMetrics metrics = {0};
    if (!miso_text_get_metrics(game->engine, text_handle, &metrics)) {
        metrics.width = 0.0f;
        metrics.height = 0.0f;
    }
    const float box_w = metrics.width + pad_x * 2.0f;
    const float box_h = SDL_max(line_h, metrics.height + pad_y * 2.0f);

    miso_render_submit_ui_rect(game->engine, x, y, box_w, box_h, 0x000000AAu);
    miso_render_submit_ui_text_handle(game->engine, text_handle, x + pad_x, y + pad_y, 0xFFFFFFFFu);
}

static void testbed_game_on_render_world(void *const ctx, const MisoEngine *const engine) {
    TestbedGame *const game = (TestbedGame *)ctx;
    if (!game || !game->tile_scene) {
        return;
    }

    miso_render_begin_world(engine, game->camera_id);
    miso_render_set_water_params(
        engine, game->game_clock.total, game->wave_speed, game->wave_amplitude, game->wave_phase);

    if (testbed_should_render_world(game)) {
        miso_profiler_begin(game->engine, game->profiler_render_map);
        if (game->agent_mode == TESTBED_AGENT_MODE_RENDER_ONLY) {
            miso_tile_scene_render_terrain(engine, game->tile_scene, game->camera_id);
        } else {
            miso_tile_scene_render(engine, game->tile_scene, game->camera_id);
        }
        miso_profiler_end(game->engine, game->profiler_render_map);

        if (game->agent_mode == TESTBED_AGENT_MODE_RENDER_ONLY) {
            testbed_render_agent_instances(game);
        }
        testbed_render_tile_highlight(
            game, game->hover_tile.x, game->hover_tile.y, (SDL_FColor){0.0f, 1.0f, 1.0f, 1.0f});

        miso_render_diag_submit_ui_texture_handle_debug(
            game->engine, game->tile_texture, 50.0f, (float)game->screen_height - 384.0f - 50.0f, 192.0f, 384.0f);
    }

    if (testbed_should_render_wire(game)) {
        miso_profiler_begin(game->engine, game->profiler_render_wireframes);
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
                miso_render_submit_world_lines(
                    game->engine, game->wireframe_line_scratch, (int)offset_vertices, 0x00FFFFFFu);
            }
        }
        miso_profiler_end(game->engine, game->profiler_render_wireframes);
    }

    miso_render_end_world(engine);
}

static void testbed_game_on_render_ui(void *const ctx, const MisoEngine *const engine) {
    TestbedGame *const game = (TestbedGame *)ctx;
    if (!game || !testbed_should_render_ui(game)) {
        return;
    }

    miso_render_begin_ui(engine);

    float ui_y_pos = 20.0f;

    char hover_tile_info[64];
    SDL_snprintf(hover_tile_info, sizeof(hover_tile_info), "Tile: (%d, %d)", game->hover_tile.x, game->hover_tile.y);
    testbed_render_hud_line(game, 0, 10.0f, ui_y_pos, hover_tile_info);
    ui_y_pos += 34.0f;

    char camera_info[128];
    SDL_snprintf(camera_info,
                 sizeof(camera_info),
                 "Camera Pos: (%5.1f, %5.1f) | Zoom: %4.2f",
                 game->camera_x,
                 game->camera_y,
                 game->camera_zoom);
    testbed_render_hud_line(game, 1, 10.0f, ui_y_pos, camera_info);
    ui_y_pos += 34.0f;

    char fps_str[128];
    if (game->debug_mode) {
        float min = 0.0f;
        float max = 0.0f;
        float avg = 0.0f;
        miso_profiler_get_fps(game->engine, &min, &avg, &max);
        SDL_snprintf(fps_str, sizeof(fps_str), "FPS: min %4.0f | avg %4.0f | max %4.0f", min, avg, max);
    } else {
        const float fps = game->frame_dt > 0.0f ? (1.0f / game->frame_dt) : 0.0f;
        SDL_snprintf(fps_str, sizeof(fps_str), "FPS %4.0f", fps);
    }
    testbed_render_hud_line(game, 2, 10.0f, ui_y_pos, fps_str);
    ui_y_pos += 34.0f;

    if (game->debug_mode && (!game->benchmark_mode || game->benchmark_profiler_enabled)) {
        miso_profiler_overlay_render(game->engine, (SDL_FPoint){10.0f, ui_y_pos});
    }

    char mouse_pos_info[64];
    SDL_snprintf(mouse_pos_info, sizeof(mouse_pos_info), "%.1f, %.1f", game->mouse_x, game->mouse_y);
    testbed_render_hud_line(game, 3, game->mouse_x + 15.0f, game->mouse_y + 15.0f, mouse_pos_info);

    miso_render_end_ui(engine);
}

static void testbed_game_on_render_debug(void *const ctx, const MisoEngine *const engine) {
    TestbedGame *const game = (TestbedGame *)ctx;
    if (!game || (game->benchmark_mode && !game->benchmark_debug_ui_enabled)) {
        return;
    }

    miso_profiler_begin((MisoEngine *)engine, MISO_PROFILER_ENGINE_DEBUG_UI);
    miso_debug_ui_prepare_render(engine);

    struct nk_context *nk = miso_debug_ui_get_context();
    const float ui_s = miso_debug_ui_get_scale();
    if (nk &&
        nk_begin(nk,
                 "Debug",
                 nk_rect(50 * ui_s, 400 * ui_s, 300 * ui_s, 430 * ui_s),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(nk, 25 * ui_s, 1);
        testbed_nk_labelf(nk, NK_TEXT_LEFT, "Agent mode: %s", testbed_agent_mode_name(game->agent_mode));
        testbed_nk_labelf(nk, NK_TEXT_LEFT, "Agents: %d", game->agent_count);
        testbed_nk_labelf(nk, NK_TEXT_LEFT, "Hover: (%d, %d)", game->hover_tile.x, game->hover_tile.y);
        testbed_nk_labelf(nk,
                          NK_TEXT_LEFT,
                          "Agent sim: %.3f ms | moves %llu/%llu | turns %llu",
                          (double)game->agent_metrics.agent_sim_ms,
                          (unsigned long long)game->agent_metrics.move_successes,
                          (unsigned long long)game->agent_metrics.move_attempts,
                          (unsigned long long)game->agent_metrics.turns);

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
        if (miso_render_diag_get_frame_stats(engine, &stats)) {
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
            testbed_nk_labelf(nk,
                              NK_TEXT_LEFT,
                              "Present mode: %s",
                              testbed_present_mode_name(miso_render_diag_get_present_mode(engine)));
            const MisoRenderVSyncAcquireMode vsync_acquire_mode = miso_render_diag_get_vsync_acquire_mode(engine);
            testbed_nk_labelf(
                nk, NK_TEXT_LEFT, "VSYNC acquire mode: %s", testbed_vsync_acquire_mode_name(vsync_acquire_mode));
            const char *const vsync_acquire_items[] = {"BLOCKING", "PASSTHROUGH"};
            int vsync_acquire_index = vsync_acquire_mode == MISO_RENDER_VSYNC_ACQUIRE_PASSTHROUGH ? 1 : 0;
            nk_layout_row_dynamic(nk, 24 * ui_s, 2);
            nk_label(nk, "Set VSYNC acquire", NK_TEXT_LEFT);
            const int selected_vsync_acquire_index = nk_combo(
                nk, vsync_acquire_items, 2, vsync_acquire_index, (int)(20 * ui_s), nk_vec2(150 * ui_s, 96 * ui_s));
            if (selected_vsync_acquire_index != vsync_acquire_index) {
                miso_render_tune_set_vsync_acquire_mode(engine,
                                                        selected_vsync_acquire_index == 1
                                                            ? MISO_RENDER_VSYNC_ACQUIRE_PASSTHROUGH
                                                            : MISO_RENDER_VSYNC_ACQUIRE_BLOCKING);
            }
            const uint32_t allowed_frames_in_flight = miso_render_diag_get_allowed_frames_in_flight(engine);
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
                !miso_render_tune_set_allowed_frames_in_flight(engine, (uint32_t)(selected_frames_in_flight + 1))) {
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
    miso_profiler_end((MisoEngine *)engine, MISO_PROFILER_ENGINE_DEBUG_UI);
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
    const MisoIsoMapDesc *const desc = testbed_map_desc(game);
    if (!game || !game->terrain || !desc) {
        return;
    }

    for (int y = 0; y < desc->height_tiles; y++) {
        for (int x = 0; x < desc->width_tiles; x++) {
            const int i = y * desc->width_tiles + x;
            int tile_index = (i % 2) ? 0 : 36;
            uint32_t flags = MISO_TILE_FLAG_NONE;

            if (i > (desc->width_tiles * 10)) {
                tile_index = TILE_PLACEHOLDER_SEA;
                flags = MISO_TILE_FLAG_WATER;
            }

            miso_tilemap_set_tile(game->terrain, x, y, (uint32_t)tile_index);
            miso_tilemap_set_flags(game->terrain, x, y, flags);
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
    game->agent_count = 0;
    game->agent_metrics = (TestbedAgentFrameMetrics){0};
}

static void testbed_reset_demo_scene(TestbedGame *const game, const int spawn_count) {
    if (!game || !game->tile_scene || !game->terrain) {
        return;
    }

    testbed_clear_buildings(game);
    miso_tile_scene_clear_objects(game->tile_scene);
    miso_tile_scene_reset_stats(game->tile_scene);
    testbed_populate_demo_map(game);

    game->hover_tile = (SDL_Point){-1, -1};
    if (spawn_count > 0) {
        testbed_spawn_boats(game, spawn_count);
    }
    testbed_update_boat_placement_overlay(game);
}

MisoResult testbed_game_create(MisoEngine *engine, const TestbedGameConfig *const config, TestbedGame **out_game) {
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
    game->agent_mode = TESTBED_AGENT_MODE_STATIC;

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

    for (int i = 0; i < TESTBED_HUD_TEXT_COUNT; i++) {
        if (miso_text_create(engine, game->hud_font, "", &game->hud_texts[i]) != MISO_OK) {
            for (int j = 0; j < i; j++) {
                miso_text_destroy(engine, game->hud_texts[j]);
                game->hud_texts[j] = 0;
            }
            miso_render_destroy_font(engine, game->hud_font);
            miso_debug_ui_shutdown();
            SDL_free(game);
            return MISO_ERR_OUT_OF_MEMORY;
        }
    }

    (void)miso_profiler_register_game_category_child(
        engine, MISO_PROFILER_ENGINE_RENDER_WORLD, "testbed.render_map", 0xD8E65AFFu, &game->profiler_render_map);
    (void)miso_profiler_register_game_category_child(engine,
                                                     MISO_PROFILER_ENGINE_RENDER_WORLD,
                                                     "testbed.render_wireframes",
                                                     0x5AE6C8FFu,
                                                     &game->profiler_render_wireframes);
    miso_profiler_overlay_init(engine, game->hud_font);

    char resource_path[512] = {0};
    const char *const tileset_path = testbed_get_resource_path(resource_path, "isometric-sheet.png");
    if (miso_render_load_texture(engine, tileset_path, &game->tile_texture) != MISO_OK) {
        testbed_game_destroy(game);
        return MISO_ERR_IO;
    }

    const int map_width = config && config->map_size > 0 ? config->map_size : MAP_SIZE_X;
    const int map_height = config && config->map_size > 0 ? config->map_size : MAP_SIZE_Y;
    const MisoTileSceneDesc tile_scene_desc = {
        .map =
            {
                .width_tiles = map_width,
                .height_tiles = map_height,
                .tile_w_px = TILE_SIZE,
                .tile_h_px = TILE_SIZE,
            },
    };
    game->tile_scene = miso_tile_scene_create(engine, &tile_scene_desc);
    if (!game->tile_scene) {
        testbed_game_destroy(game);
        return MISO_ERR_OUT_OF_MEMORY;
    }

    const MisoTilemapDesc terrain_desc = {
        .terrain_sprite =
            {
                .atlas =
                    {
                        .texture = game->tile_texture,
                    },
            },
    };
    game->terrain = miso_tile_scene_create_tilemap(game->tile_scene, &terrain_desc);
    if (!game->terrain) {
        testbed_game_destroy(game);
        return MISO_ERR_OUT_OF_MEMORY;
    }
    const MisoTileOverlayDesc tint_overlay_desc = {
        .clear_rgba8 = 0x00000000u,
    };
    game->tile_tint_overlay = miso_tile_scene_create_overlay(game->tile_scene, &tint_overlay_desc);
    if (!game->tile_tint_overlay) {
        testbed_game_destroy(game);
        return MISO_ERR_OUT_OF_MEMORY;
    }
    miso_tilemap_set_tint_overlay(game->terrain, game->tile_tint_overlay, 1.0f);

    const MisoTileObjectVisualDesc boat_visual = {
        .visual_id = TILE_PLACEHOLDER_BOAT,
        .sprite =
            {
                .atlas =
                    {
                        .texture = game->tile_texture,
                    },
                .source_w_cells = 2,
                .source_h_cells = 3,
                .render_w_px = TILE_SIZE * 2,
                .render_h_px = TILE_SIZE * 3,
                .origin_y_px = TILE_SIZE + (TILE_SIZE * 3) / 2,
            },
        .atlas_tile_id = TILE_PLACEHOLDER_BOAT,
    };
    if (miso_tile_scene_set_object_visual(game->tile_scene, &boat_visual) != MISO_OK) {
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

    miso_render_tune_set_upload_suppressed(game->engine, false);

    for (int i = 0; i < game->building_count; i++) {
        SDL_free(game->wireframe_meshes[i].line_vertices);
        game->wireframe_meshes[i].line_vertices = nullptr;
        game->wireframe_meshes[i].vertex_count = 0;
    }

    SDL_free(game->wireframe_line_scratch);
    game->wireframe_line_scratch = nullptr;
    game->wireframe_line_scratch_capacity_vertices = 0U;
    SDL_free(game->agent_render_instances);
    game->agent_render_instances = nullptr;
    game->agent_render_instance_capacity = 0U;

    if (game->tile_scene) {
        miso_tile_scene_destroy(game->tile_scene);
        game->tile_scene = nullptr;
    }
    game->terrain = nullptr;
    game->tile_tint_overlay = nullptr;
    if (game->tile_texture != 0) {
        miso_render_destroy_texture(game->engine, game->tile_texture);
        game->tile_texture = 0;
    }

    miso_profiler_overlay_shutdown(game->engine);
    for (int i = 0; i < TESTBED_HUD_TEXT_COUNT; i++) {
        miso_text_destroy(game->engine, game->hud_texts[i]);
        game->hud_texts[i] = 0;
    }

    miso_render_destroy_font(game->engine, game->hud_font);
    miso_debug_ui_shutdown();
    SDL_free(game);
}

void testbed_game_frame_begin(TestbedGame *game, const float real_dt_seconds) {
    if (!game) {
        return;
    }

    game->agent_metrics = (TestbedAgentFrameMetrics){
        .agent_count = game->agent_count,
        .agents_spawned = game->agent_count,
        .active_objects = game->agent_count,
    };
    if (game->tile_scene) {
        miso_tile_scene_reset_stats(game->tile_scene);
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
    miso_profiler_begin(game->engine, MISO_PROFILER_ENGINE_EVENTS);
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
    miso_profiler_end(game->engine, MISO_PROFILER_ENGINE_EVENTS);
}

void testbed_game_frame_end(TestbedGame *const game) {
    if (!game) {
        return;
    }

    game->agent_metrics.sim_backlog_alpha = miso_get_interpolation_alpha(game->engine);

    MisoProfilerSnapshot snapshot = {0};
    if (miso_profiler_get_snapshot(game->engine, &snapshot) && snapshot.count > 0 && snapshot.newest >= 0) {
        game->agent_metrics.fixed_tick_ms =
            snapshot.frames[snapshot.newest].category_duration_ms[MISO_PROFILER_ENGINE_FIXED_TICKS];
    }

    MisoTileSceneStats scene_stats = {0};
    if (miso_tile_scene_get_stats(game->tile_scene, &scene_stats)) {
        game->agent_metrics.active_objects = (int)scene_stats.active_object_count;
        game->agent_metrics.remove_scan_steps = scene_stats.remove_scan_steps;
    } else if (game->agent_mode == TESTBED_AGENT_MODE_RENDER_ONLY) {
        game->agent_metrics.active_objects = 0;
    }
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
        game->debug_mode = false;
        miso_render_tune_set_upload_suppressed(game->engine, false);
        return;
    }

    testbed_sync_benchmark_debug_mode(game);
}

void testbed_game_set_benchmark_debug_ui(TestbedGame *const game, const bool enabled) {
    if (!game) {
        return;
    }
    game->benchmark_debug_ui_enabled = enabled;
    testbed_sync_benchmark_debug_mode(game);
}

void testbed_game_set_benchmark_profiler(TestbedGame *const game, const bool enabled) {
    if (!game) {
        return;
    }
    game->benchmark_profiler_enabled = enabled;
    testbed_sync_benchmark_debug_mode(game);
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
    miso_render_tune_set_upload_suppressed(game->engine, enabled);
}

void testbed_game_set_agent_mode(TestbedGame *const game, const TestbedAgentMode mode) {
    if (!game) {
        return;
    }
    game->agent_mode = mode;
}

void testbed_game_reset_benchmark_scene(TestbedGame *const game, const int spawn_count) {
    if (!game) {
        return;
    }
    testbed_reset_demo_scene(game, spawn_count);
    testbed_apply_benchmark_camera_preset(game);
}

void testbed_game_get_agent_metrics(const TestbedGame *const game, TestbedAgentFrameMetrics *const out_metrics) {
    if (!game || !out_metrics) {
        return;
    }
    *out_metrics = game->agent_metrics;
}

bool testbed_game_is_running(const TestbedGame *const game) {
    return game && game->running;
}

const MisoGameHooks *testbed_game_hooks(void) {
    return &g_testbed_game_hooks;
}
