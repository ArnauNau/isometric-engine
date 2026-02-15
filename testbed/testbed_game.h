#ifndef TESTBED_GAME_H
#define TESTBED_GAME_H

#include "miso_engine.h"

typedef enum TestbedBenchCameraState {
    TESTBED_BENCH_CAMERA_ZOOM_OUT_CENTER = 0,
    TESTBED_BENCH_CAMERA_ZOOM_IN_CENTER,
    TESTBED_BENCH_CAMERA_ZOOM_IN_OFFMAP
} TestbedBenchCameraState;

typedef enum TestbedBenchDiagnosticMode {
    TESTBED_BENCH_DIAGNOSTIC_DEFAULT = 0,
    TESTBED_BENCH_DIAGNOSTIC_WORLD_ONLY,
    TESTBED_BENCH_DIAGNOSTIC_UI_ONLY,
    TESTBED_BENCH_DIAGNOSTIC_WIRE_ONLY,
    TESTBED_BENCH_DIAGNOSTIC_NO_DRAW,
    TESTBED_BENCH_DIAGNOSTIC_UPLOAD_SUPPRESSED
} TestbedBenchDiagnosticMode;

typedef struct TestbedGame TestbedGame;

MisoResult testbed_game_create(MisoEngine *engine, TestbedGame **out_game);
void testbed_game_destroy(TestbedGame *game);

void testbed_game_enable_benchmark_mode(TestbedGame *game, bool enabled);
void testbed_game_set_benchmark_debug_ui(TestbedGame *game, bool enabled);
void testbed_game_set_benchmark_profiler(TestbedGame *game, bool enabled);
void testbed_game_set_benchmark_wireframe(TestbedGame *game, bool enabled);
void testbed_game_set_benchmark_diagnostic_mode(TestbedGame *game, TestbedBenchDiagnosticMode mode);
void testbed_game_set_benchmark_camera_state(TestbedGame *game, TestbedBenchCameraState camera_state);
void testbed_game_set_benchmark_upload_suppressed(TestbedGame *game, bool enabled);
void testbed_game_reset_benchmark_scene(TestbedGame *game, int spawn_count);

void testbed_game_frame_begin(TestbedGame *game, float real_dt_seconds);
void testbed_game_frame_end_events(TestbedGame *game);
void testbed_game_frame_end(const TestbedGame *game);

bool testbed_game_is_running(const TestbedGame *game);
const MisoGameHooks *testbed_game_hooks(void);

const char *testbed_bench_camera_state_name(TestbedBenchCameraState state);
const char *testbed_bench_diagnostic_mode_name(TestbedBenchDiagnosticMode mode);

#endif
