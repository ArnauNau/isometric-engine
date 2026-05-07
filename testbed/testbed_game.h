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

typedef enum TestbedAgentMode {
    TESTBED_AGENT_MODE_STATIC = 0,
    TESTBED_AGENT_MODE_MOVE,
    TESTBED_AGENT_MODE_MOVE_NO_DRAW,
    TESTBED_AGENT_MODE_LEGACY_MOVE,
    TESTBED_AGENT_MODE_LEGACY_MOVE_NO_DRAW,
    TESTBED_AGENT_MODE_RENDER_ONLY
} TestbedAgentMode;

typedef struct TestbedGame TestbedGame;

typedef struct TestbedGameConfig {
    int map_size;
} TestbedGameConfig;

typedef struct TestbedAgentFrameMetrics {
    int agent_count;
    int agents_spawned;
    int active_objects;
    int ticks_this_frame;
    uint64_t move_attempts;
    uint64_t move_successes;
    uint64_t turns;
    uint64_t move_failures;
    uint64_t remove_scan_steps;
    float agent_sim_ms;
    float movement_api_ms;
    float fixed_tick_ms;
    float sim_backlog_alpha;
} TestbedAgentFrameMetrics;

MisoResult testbed_game_create(MisoEngine *engine, const TestbedGameConfig *config, TestbedGame **out_game);
void testbed_game_destroy(TestbedGame *game);

void testbed_game_enable_benchmark_mode(TestbedGame *game, bool enabled);
void testbed_game_set_benchmark_debug_ui(TestbedGame *game, bool enabled);
void testbed_game_set_benchmark_profiler(TestbedGame *game, bool enabled);
void testbed_game_set_benchmark_wireframe(TestbedGame *game, bool enabled);
void testbed_game_set_benchmark_diagnostic_mode(TestbedGame *game, TestbedBenchDiagnosticMode mode);
void testbed_game_set_benchmark_camera_state(TestbedGame *game, TestbedBenchCameraState camera_state);
void testbed_game_set_benchmark_upload_suppressed(TestbedGame *game, bool enabled);
void testbed_game_set_agent_mode(TestbedGame *game, TestbedAgentMode mode);
void testbed_game_reset_benchmark_scene(TestbedGame *game, int spawn_count);
void testbed_game_get_agent_metrics(const TestbedGame *game, TestbedAgentFrameMetrics *out_metrics);

void testbed_game_frame_begin(TestbedGame *game, float real_dt_seconds);
void testbed_game_frame_end_events(TestbedGame *game);
void testbed_game_frame_end(TestbedGame *game);

bool testbed_game_is_running(const TestbedGame *game);
const MisoGameHooks *testbed_game_hooks(void);

const char *testbed_bench_camera_state_name(TestbedBenchCameraState state);
const char *testbed_bench_diagnostic_mode_name(TestbedBenchDiagnosticMode mode);
const char *testbed_agent_mode_name(TestbedAgentMode mode);

#endif
