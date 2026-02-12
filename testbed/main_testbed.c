#include "logger.h"
#include "miso_engine.h"
#include "testbed/testbed_game.h"

#include <SDL3/SDL.h>

int main(void) {
    LOG_init();

    const MisoConfig config = {
        .window_width = 1920,
        .window_height = 1080,
        .window_title = "miso testbed",
        .enable_vsync = true,
        .sim_tick_hz = 20,
        .max_sim_steps_per_frame = 8,
    };

    MisoEngine *engine = nullptr;
    if (miso_create(&config, &engine) != MISO_OK || !engine) {
        SDL_Log("Failed to create engine");
        return 1;
    }

    TestbedGame *game = nullptr;
    if (testbed_game_create(engine, &game) != MISO_OK || !game) {
        SDL_Log("Failed to create testbed game");
        miso_destroy(engine);
        return 1;
    }

    if (miso_game_register(engine, testbed_game_hooks(), game) != MISO_OK) {
        SDL_Log("Failed to register testbed hooks");
        testbed_game_destroy(game);
        miso_destroy(engine);
        return 1;
    }

    while (testbed_game_is_running(game)) {
        if (!miso_begin_frame(engine)) {
            break;
        }

        testbed_game_frame_begin(game, miso_get_real_delta_seconds(engine));

        MisoEvent event;
        while (miso_poll_event(engine, &event)) {
        }
        testbed_game_frame_end_events(game);

        miso_run_simulation_ticks(engine, nullptr, NULL);
        miso_end_frame(engine);
        testbed_game_frame_end(game);
    }

    testbed_game_destroy(game);
    miso_destroy(engine);
    return 0;
}
