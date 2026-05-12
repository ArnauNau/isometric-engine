#include "internal/miso__engine_internal.h"
#include "miso_camera.h"
#include "miso_iso.h"
#include "miso_tile_scene.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct PickFixture {
    MisoEngine engine;
    MisoTileScene *tile_scene;
    MisoCameraId camera_id;
    int logical_width;
    int logical_height;
    float pixel_ratio;
} PickFixture;

typedef struct TileCoord {
    int x;
    int y;
} TileCoord;

static const TileCoord g_sample_tiles[] = {
    {0, 0},
    {1, 0},
    {0, 1},
    {3, 2},
    {5, 4},
};

static int failf(const char *fmt, ...) {
    va_list args = nullptr;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    return 1;
}

static bool pick_fixture_init(PickFixture *const fixture,
                              const int logical_width,
                              const int logical_height,
                              const float pixel_ratio) {
    if (!fixture || logical_width <= 0 || logical_height <= 0 || pixel_ratio <= 0.0f) {
        return false;
    }

    SDL_memset(fixture, 0, sizeof(*fixture));
    fixture->logical_width = logical_width;
    fixture->logical_height = logical_height;
    fixture->pixel_ratio = pixel_ratio;

    fixture->engine.config.window_width = (int)SDL_lroundf((float)logical_width * pixel_ratio);
    fixture->engine.config.window_height = (int)SDL_lroundf((float)logical_height * pixel_ratio);

    fixture->camera_id = miso_camera_create(&fixture->engine);
    if (fixture->camera_id == 0) {
        return false;
    }

    miso_camera_set_viewport(&fixture->engine,
                             fixture->camera_id,
                             0,
                             0,
                             fixture->engine.config.window_width,
                             fixture->engine.config.window_height);

    const MisoIsoMapDesc desc = {
        .width_tiles = 10,
        .height_tiles = 10,
        .tile_w_px = 32,
        .tile_h_px = 32,
    };
    const MisoTileSceneDesc scene_desc = {
        .map = desc,
    };
    fixture->tile_scene = miso_tile_scene_create(&fixture->engine, &scene_desc);
    if (!fixture->tile_scene) {
        return false;
    }

    return true;
}

static void pick_fixture_shutdown(PickFixture *const fixture) {
    if (!fixture) {
        return;
    }

    miso_tile_scene_destroy(fixture->tile_scene);
    SDL_free(fixture->engine.cameras);
    SDL_memset(fixture, 0, sizeof(*fixture));
}

static void tile_center_world(
    const PickFixture *const fixture, const int tile_x, const int tile_y, float *const world_x, float *const world_y) {
    const MisoIsoMapDesc *const desc = miso_tile_scene_get_desc(fixture->tile_scene);
    miso_iso_tile_to_world(desc, tile_x, tile_y, world_x, world_y);

    *world_x += (float)desc->tile_w_px * 0.5f;
    *world_y += ((float)desc->tile_h_px * 0.5f) * 0.5f;
}

static SDL_Point screen_pixel_for_tile_center(const PickFixture *const fixture, const int tile_x, const int tile_y) {
    float world_x = 0.0f;
    float world_y = 0.0f;
    tile_center_world(fixture, tile_x, tile_y, &world_x, &world_y);

    const MisoVec2 screen = miso_camera_world_to_screen(&fixture->engine, fixture->camera_id, world_x, world_y);
    return (SDL_Point){.x = (int)SDL_lroundf(screen.x), .y = (int)SDL_lroundf(screen.y)};
}

static SDL_Point
testbed_pick_from_logical_mouse(const PickFixture *const fixture, const int logical_x, const int logical_y) {
    const float pixel_x = (float)logical_x * fixture->pixel_ratio;
    const float pixel_y = (float)logical_y * fixture->pixel_ratio;
    const MisoVec2 world_position = miso_camera_screen_to_world(
        &fixture->engine, fixture->camera_id, (int)SDL_lroundf(pixel_x), (int)SDL_lroundf(pixel_y));
    const MisoIsoMapDesc *const desc = miso_tile_scene_get_desc(fixture->tile_scene);
    int tile_x = 0;
    int tile_y = 0;
    miso_iso_world_to_tile_floor(desc, world_position.x, world_position.y, &tile_x, &tile_y);
    return (SDL_Point){.x = tile_x, .y = tile_y};
}

static bool scene_pick_from_screen_pixels(const PickFixture *const fixture,
                                          int const screen_x,
                                          int const screen_y,
                                          SDL_Point *const out_tile) {
    const MisoVec2 world_position =
        miso_camera_screen_to_world(&fixture->engine, fixture->camera_id, screen_x, screen_y);
    float tile_x = 0.0f;
    float tile_y = 0.0f;
    if (!miso_tile_scene_world_to_tile(fixture->tile_scene, world_position.x, world_position.y, &tile_x, &tile_y)) {
        return false;
    }

    if (out_tile) {
        *out_tile = (SDL_Point){.x = (int)SDL_floorf(tile_x), .y = (int)SDL_floorf(tile_y)};
    }
    return true;
}

static int run_testbed_hidpi_case(void) {
    PickFixture fixture;
    if (!pick_fixture_init(&fixture, 1920, 1080, 2.0f)) {
        return failf("failed to initialize pick fixture");
    }

    for (size_t i = 0; i < SDL_arraysize(g_sample_tiles); i++) {
        const TileCoord sample = g_sample_tiles[i];
        const SDL_Point screen_px = screen_pixel_for_tile_center(&fixture, sample.x, sample.y);
        const int logical_x = (int)SDL_lroundf((float)screen_px.x / fixture.pixel_ratio);
        const int logical_y = (int)SDL_lroundf((float)screen_px.y / fixture.pixel_ratio);
        const SDL_Point picked = testbed_pick_from_logical_mouse(&fixture, logical_x, logical_y);

        if (picked.x != sample.x || picked.y != sample.y) {
            pick_fixture_shutdown(&fixture);
            return failf("testbed path picked (%d, %d) for tile (%d, %d)", picked.x, picked.y, sample.x, sample.y);
        }
    }

    pick_fixture_shutdown(&fixture);
    return 0;
}

static int run_world_pixel_parity_case(void) {
    PickFixture fixture;
    if (!pick_fixture_init(&fixture, 1920, 1080, 2.0f)) {
        return failf("failed to initialize pick fixture");
    }

    for (size_t i = 0; i < SDL_arraysize(g_sample_tiles); i++) {
        const TileCoord sample = g_sample_tiles[i];
        const SDL_Point screen_px = screen_pixel_for_tile_center(&fixture, sample.x, sample.y);
        SDL_Point picked = {-1, -1};

        if (!scene_pick_from_screen_pixels(&fixture, screen_px.x, screen_px.y, &picked)) {
            pick_fixture_shutdown(&fixture);
            return failf("scene pixel path reported out-of-bounds for tile (%d, %d)", sample.x, sample.y);
        }

        if (picked.x != sample.x || picked.y != sample.y) {
            pick_fixture_shutdown(&fixture);
            return failf("scene pixel path picked (%d, %d) for tile (%d, %d)", picked.x, picked.y, sample.x, sample.y);
        }
    }

    pick_fixture_shutdown(&fixture);
    return 0;
}

static int run_cafe_hidpi_case(void) {
    PickFixture fixture;
    if (!pick_fixture_init(&fixture, 1920, 1080, 2.0f)) {
        return failf("failed to initialize pick fixture");
    }

    for (size_t i = 0; i < SDL_arraysize(g_sample_tiles); i++) {
        const TileCoord sample = g_sample_tiles[i];
        const SDL_Point screen_px = screen_pixel_for_tile_center(&fixture, sample.x, sample.y);
        const int logical_x = (int)SDL_lroundf((float)screen_px.x / fixture.pixel_ratio);
        const int logical_y = (int)SDL_lroundf((float)screen_px.y / fixture.pixel_ratio);
        const int normalized_px_x = (int)SDL_lroundf((float)logical_x * fixture.pixel_ratio);
        const int normalized_px_y = (int)SDL_lroundf((float)logical_y * fixture.pixel_ratio);
        SDL_Point picked = {-1, -1};

        if (!scene_pick_from_screen_pixels(&fixture, normalized_px_x, normalized_px_y, &picked)) {
            pick_fixture_shutdown(&fixture);
            return failf("cafe hidpi path reported out-of-bounds for tile (%d, %d)", sample.x, sample.y);
        }

        if (picked.x != sample.x || picked.y != sample.y) {
            pick_fixture_shutdown(&fixture);
            return failf("cafe hidpi path picked (%d, %d) for tile (%d, %d)", picked.x, picked.y, sample.x, sample.y);
        }
    }

    pick_fixture_shutdown(&fixture);
    return 0;
}

static int run_resize_dispatch_dedupe_case(void) {
    MisoEngine engine = {0};

    if (!miso__engine_should_dispatch_resize_event(&engine, 1280, 720)) {
        return failf("first resize event was suppressed");
    }
    if (miso__engine_should_dispatch_resize_event(&engine, 1280, 720)) {
        return failf("duplicate resize event was not suppressed");
    }
    if (!miso__engine_should_dispatch_resize_event(&engine, 1920, 1080)) {
        return failf("changed resize event was suppressed");
    }
    if (miso__engine_should_dispatch_resize_event(&engine, 1920, 1080)) {
        return failf("second duplicate resize event was not suppressed");
    }
    if (!miso__engine_should_dispatch_resize_event(&engine, 0, -5)) {
        return failf("sanitized resize event was suppressed");
    }
    if (miso__engine_should_dispatch_resize_event(&engine, 1, 1)) {
        return failf("sanitized duplicate resize event was not suppressed");
    }

    return 0;
}

static int run_normalized_viewport_case(void) {
    MisoEngine engine = {0};
    engine.config.window_width = 1920;
    engine.config.window_height = 1080;

    const MisoCameraId camera_id = miso_camera_create(&engine);
    if (camera_id == 0) {
        return failf("failed to create camera");
    }

    miso_camera_set_viewport_normalized(&engine, camera_id, 0.0f, 0.0f, 1.0f, 1.0f);
    MisoCameraState *camera = miso__camera_get_mut(&engine, camera_id);
    if (!camera || camera->viewport.x != 0 || camera->viewport.y != 0 || camera->viewport.w != 1920 ||
        camera->viewport.h != 1080) {
        SDL_free(engine.cameras);
        return failf("full normalized viewport did not resolve to full window");
    }

    miso_camera_set_viewport_normalized(&engine, camera_id, 0.5f, 0.0f, 0.5f, 1.0f);
    camera = miso__camera_get_mut(&engine, camera_id);
    if (!camera || camera->viewport.x != 960 || camera->viewport.y != 0 || camera->viewport.w != 960 ||
        camera->viewport.h != 1080) {
        SDL_free(engine.cameras);
        return failf("right-half normalized viewport did not resolve correctly");
    }

    miso__camera_resolve_normalized_viewports(&engine, 1280, 720);
    camera = miso__camera_get_mut(&engine, camera_id);
    if (!camera || camera->viewport.x != 640 || camera->viewport.y != 0 || camera->viewport.w != 640 ||
        camera->viewport.h != 720) {
        SDL_free(engine.cameras);
        return failf("normalized viewport did not update after resize");
    }

    miso_camera_set_viewport(&engine, camera_id, 10, 20, 300, 200);
    miso__camera_resolve_normalized_viewports(&engine, 640, 360);
    camera = miso__camera_get_mut(&engine, camera_id);
    if (!camera || camera->viewport.x != 10 || camera->viewport.y != 20 || camera->viewport.w != 300 ||
        camera->viewport.h != 200) {
        SDL_free(engine.cameras);
        return failf("pixel viewport was changed by normalized viewport resolver");
    }

    SDL_free(engine.cameras);
    return 0;
}

int main(const int argc, const char *const *const argv) {
    if (argc != 3 || SDL_strcmp(argv[1], "--case") != 0) {
        fprintf(stderr,
                "usage: %s --case "
                "<testbed-hidpi|world-pixel-parity|cafe-hidpi|resize-dispatch-dedupe|normalized-viewport>\n",
                argv[0]);
        return 2;
    }

    if (SDL_strcmp(argv[2], "testbed-hidpi") == 0) {
        return run_testbed_hidpi_case();
    }
    if (SDL_strcmp(argv[2], "world-pixel-parity") == 0) {
        return run_world_pixel_parity_case();
    }
    if (SDL_strcmp(argv[2], "cafe-hidpi") == 0) {
        return run_cafe_hidpi_case();
    }
    if (SDL_strcmp(argv[2], "resize-dispatch-dedupe") == 0) {
        return run_resize_dispatch_dedupe_case();
    }
    if (SDL_strcmp(argv[2], "normalized-viewport") == 0) {
        return run_normalized_viewport_case();
    }

    fprintf(stderr, "unknown test case: %s\n", argv[2]);
    return 2;
}
