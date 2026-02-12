#ifndef TESTBED_GAME_H
#define TESTBED_GAME_H

#include "miso_engine.h"

typedef struct TestbedGame TestbedGame;

MisoResult testbed_game_create(MisoEngine *engine, TestbedGame **out_game);
void testbed_game_destroy(TestbedGame *game);

void testbed_game_frame_begin(TestbedGame *game, float real_dt_seconds);
void testbed_game_frame_end_events(TestbedGame *game);
void testbed_game_frame_end(TestbedGame *game);

bool testbed_game_is_running(const TestbedGame *game);
const MisoGameHooks *testbed_game_hooks(void);

#endif
