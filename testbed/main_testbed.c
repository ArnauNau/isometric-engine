#include "logger.h"
#include "miso_engine.h"
#include "miso_render.h"
#include "renderer/renderer.h"
#include "testbed/testbed_game.h"

#include <SDL3/SDL.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define BENCH_SCHEMA_VERSION "1"
#define BENCH_DEFAULT_OUTPUT_DIR "bench"
#define BENCH_DEFAULT_WARMUP_S 2.0f
#define BENCH_DEFAULT_SAMPLE_S 10.0f
#define BENCH_DEFAULT_REPETITIONS 3
#define BENCH_DEFAULT_ALLOWED_FRAMES_IN_FLIGHT 1
#define BENCH_BASELINE_SPAWN_COUNT 256
#define BENCH_STRESS_SPAWN_COUNT 512
#define BENCH_ACQUIRE_BURST_THRESHOLD_MS 8.0f
#define BENCH_MAX_SCENARIOS 64
#define BENCH_GATE_FPS_DROP_TOLERANCE_PCT 5.0f
#define BENCH_GATE_FRAME_P95_INCREASE_TOLERANCE_PCT 10.0f
#define BENCH_GATE_ACQUIRE_P95_INCREASE_TOLERANCE_PCT 10.0f
#define BENCH_GATE_UPLOAD_AVG_INCREASE_TOLERANCE_PCT 10.0f
#define BENCH_MAX_SAMPLE_FPS 10000U

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct BenchCliOptions {
    bool bench;
    bool suite;
    SDL_GPUPresentMode present_mode;
    TestbedBenchCameraState camera_state;
    TestbedBenchDiagnosticMode diagnostic_mode;
    bool wireframe_enabled;
    bool debug_ui_enabled;
    bool profiler_enabled;
    int spawn_count;
    float warmup_s;
    float sample_s;
    int repetitions;
    int repeat_index;
    int allowed_frames_in_flight;
    const char *output_dir;
    const char *baseline_metrics_csv;
    bool help;
} BenchCliOptions;

typedef struct BenchScenario {
    SDL_GPUPresentMode present_mode;
    TestbedBenchCameraState camera_state;
    TestbedBenchDiagnosticMode diagnostic_mode;
    bool wireframe_enabled;
    bool debug_ui_enabled;
    bool profiler_enabled;
    int spawn_count;
    int allowed_frames_in_flight;
    bool gates_enabled;
    char name[256];
} BenchScenario;

typedef struct BenchSampleStore {
    float *frame_cpu_ms;
    float *acquire_ms;
    uint32_t count;
    uint32_t capacity;
} BenchSampleStore;

typedef struct BenchRunSummary {
    uint32_t sampled_frames;
    double fps_mean;
    float fps_min;
    float fps_max;
    float frame_p50_ms;
    float frame_p95_ms;
    float frame_p99_ms;
    float acquire_p50_ms;
    float acquire_p95_ms;
    float acquire_p99_ms;
    float upload_avg_mib;
    float upload_peak_mib;
    float render_pass_mean;
    uint32_t render_pass_max;
    float longest_acquire_burst_ms;
    float acquire_burst_frame_ratio;
    bool gates_enabled;
    bool gate_a_pass;
    bool gate_b_pass;
    bool gate_c_pass;
    bool gate_d_pass;
    bool gate_e_pass;
    bool gates_pass;
    uint32_t gate_a_violations;
    uint32_t gate_b_violations;
    uint32_t gate_c_violations;
    uint32_t gate_d_violations;
    uint32_t gate_e_burst_frames;
    uint32_t gate_e_long_burst_limit_ms;
    float gate_e_burst_ratio_limit;
    char frames_csv_path[PATH_MAX];
    char summary_json_path[PATH_MAX];
} BenchRunSummary;

typedef struct BenchScenarioResult {
    BenchScenario scenario;
    BenchRunSummary *runs;
    int run_count;
    float median_fps_mean;
    float median_frame_p95_ms;
    float median_acquire_p95_ms;
    float median_upload_avg_mib;
    char bottleneck_signal[64];
    float bottleneck_score;
    int gate_failed_runs;
} BenchScenarioResult;

typedef struct BenchBaselineMetric {
    char scenario_name[256];
    float fps_mean;
    float frame_p95_ms;
    float acquire_p95_ms;
    float upload_avg_mib;
} BenchBaselineMetric;

typedef struct BenchComparisonEntry {
    char scenario_name[256];
    bool has_baseline;
    bool regressed;
    float fps_delta_pct;
    float frame_p95_delta_pct;
    float acquire_p95_delta_pct;
    float upload_avg_delta_pct;
} BenchComparisonEntry;

typedef struct BenchBaselineComparison {
    bool enabled;
    bool pass;
    int matched_count;
    int missing_count;
    int regression_count;
    char baseline_path[PATH_MAX];
    BenchComparisonEntry *entries;
    int entry_count;
} BenchBaselineComparison;

static volatile sig_atomic_t g_bench_abort_requested = 0;

static void bench_signal_handler(int signo) {
    (void)signo;
    g_bench_abort_requested = 1;
}

static void bench_install_signal_handlers(void) {
    g_bench_abort_requested = 0;
    signal(SIGINT, bench_signal_handler);
    signal(SIGTERM, bench_signal_handler);
}

static const char *bench_build_type_name(void) {
#ifdef MISO_DEBUG
    return "Debug";
#else
    return "Release";
#endif
}

static const char *bench_present_mode_name(const SDL_GPUPresentMode mode) {
    switch (mode) {
    case SDL_GPU_PRESENTMODE_IMMEDIATE:
        return "IMMEDIATE";
    case SDL_GPU_PRESENTMODE_MAILBOX:
        return "MAILBOX";
    case SDL_GPU_PRESENTMODE_VSYNC:
        return "VSYNC";
    default:
        return "UNKNOWN";
    }
}

static const char *bench_present_mode_slug(const SDL_GPUPresentMode mode) {
    switch (mode) {
    case SDL_GPU_PRESENTMODE_IMMEDIATE:
        return "immediate";
    case SDL_GPU_PRESENTMODE_MAILBOX:
        return "mailbox";
    case SDL_GPU_PRESENTMODE_VSYNC:
        return "vsync";
    default:
        return "unknown";
    }
}

static bool bench_parse_present_mode(const char *const value, SDL_GPUPresentMode *const out_mode) {
    if (!value || !out_mode) {
        return false;
    }

    if (SDL_strcasecmp(value, "immediate") == 0) {
        *out_mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
        return true;
    }
    if (SDL_strcasecmp(value, "mailbox") == 0) {
        *out_mode = SDL_GPU_PRESENTMODE_MAILBOX;
        return true;
    }
    if (SDL_strcasecmp(value, "vsync") == 0) {
        *out_mode = SDL_GPU_PRESENTMODE_VSYNC;
        return true;
    }
    return false;
}

static bool bench_parse_camera_state(const char *const value, TestbedBenchCameraState *const out_camera_state) {
    if (!value || !out_camera_state) {
        return false;
    }

    if (SDL_strcasecmp(value, "zoom_out_center") == 0) {
        *out_camera_state = TESTBED_BENCH_CAMERA_ZOOM_OUT_CENTER;
        return true;
    }
    if (SDL_strcasecmp(value, "zoom_in_center") == 0) {
        *out_camera_state = TESTBED_BENCH_CAMERA_ZOOM_IN_CENTER;
        return true;
    }
    if (SDL_strcasecmp(value, "zoom_in_offmap") == 0) {
        *out_camera_state = TESTBED_BENCH_CAMERA_ZOOM_IN_OFFMAP;
        return true;
    }
    return false;
}

static bool bench_parse_diagnostic_mode(const char *const value,
                                        TestbedBenchDiagnosticMode *const out_diagnostic_mode) {
    if (!value || !out_diagnostic_mode) {
        return false;
    }

    if (SDL_strcasecmp(value, "default") == 0) {
        *out_diagnostic_mode = TESTBED_BENCH_DIAGNOSTIC_DEFAULT;
        return true;
    }
    if (SDL_strcasecmp(value, "world-only") == 0) {
        *out_diagnostic_mode = TESTBED_BENCH_DIAGNOSTIC_WORLD_ONLY;
        return true;
    }
    if (SDL_strcasecmp(value, "ui-only") == 0) {
        *out_diagnostic_mode = TESTBED_BENCH_DIAGNOSTIC_UI_ONLY;
        return true;
    }
    if (SDL_strcasecmp(value, "wire-only") == 0) {
        *out_diagnostic_mode = TESTBED_BENCH_DIAGNOSTIC_WIRE_ONLY;
        return true;
    }
    if (SDL_strcasecmp(value, "no-draw") == 0) {
        *out_diagnostic_mode = TESTBED_BENCH_DIAGNOSTIC_NO_DRAW;
        return true;
    }
    if (SDL_strcasecmp(value, "upload-suppressed") == 0) {
        *out_diagnostic_mode = TESTBED_BENCH_DIAGNOSTIC_UPLOAD_SUPPRESSED;
        return true;
    }
    return false;
}

static bool bench_parse_bool_toggle(const char *const value, bool *const out_value) {
    if (!value || !out_value) {
        return false;
    }

    if (SDL_strcasecmp(value, "on") == 0 || SDL_strcasecmp(value, "true") == 0 || SDL_strcmp(value, "1") == 0) {
        *out_value = true;
        return true;
    }
    if (SDL_strcasecmp(value, "off") == 0 || SDL_strcasecmp(value, "false") == 0 || SDL_strcmp(value, "0") == 0) {
        *out_value = false;
        return true;
    }
    return false;
}

static bool bench_parse_int(const char *const value, int *const out_value) {
    if (!value || !out_value) {
        return false;
    }

    char *end = NULL;
    const long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0') {
        return false;
    }

    *out_value = (int)parsed;
    return true;
}

static bool bench_parse_float(const char *const value, float *const out_value) {
    if (!value || !out_value) {
        return false;
    }

    char *end = NULL;
    const float parsed = strtof(value, &end);
    if (end == value || *end != '\0') {
        return false;
    }

    *out_value = parsed;
    return true;
}

static void bench_print_usage(const char *const argv0) {
    SDL_Log("Usage: %s [--bench] [options]", argv0);
    SDL_Log("Benchmark options:");
    SDL_Log("  --bench                       Enable benchmark mode");
    SDL_Log("  --bench-suite                 Run full suite: 3 present x 3 camera x 2 wire = 18 scenarios");
    SDL_Log("  --present-mode <mode>         immediate|mailbox|vsync");
    SDL_Log("  --camera-state <state>        zoom_out_center|zoom_in_center|zoom_in_offmap");
    SDL_Log("  --wireframe <on|off>          Enable or disable wireframe");
    SDL_Log("  --spawn-profile <profile>     baseline|stress");
    SDL_Log("  --spawn-count <count>         Explicit spawn count override");
    SDL_Log("  --diagnostic <mode>           default|world-only|ui-only|wire-only|no-draw|upload-suppressed");
    SDL_Log("  --debug-ui <on|off>           Enable or disable debug UI rendering in benchmark runs");
    SDL_Log("  --profiler <on|off>           Enable or disable profiler rendering in benchmark runs");
    SDL_Log("  --warmup-s <seconds>          Warmup duration before sampling (default: %.1f)", BENCH_DEFAULT_WARMUP_S);
    SDL_Log("  --sample-s <seconds>          Sampling duration (default: %.1f)", BENCH_DEFAULT_SAMPLE_S);
    SDL_Log("  --repetitions <count>         Repetitions per scenario (default: %d)", BENCH_DEFAULT_REPETITIONS);
    SDL_Log("  --frames-in-flight <n>        Allowed swapchain frames in flight (1-3, default: %d)",
            BENCH_DEFAULT_ALLOWED_FRAMES_IN_FLIGHT);
    SDL_Log("  --repeat-index <index>        Run only one repetition index (0-based)");
    SDL_Log("  --output-dir <path>           Root output dir (default: %s)", BENCH_DEFAULT_OUTPUT_DIR);
    SDL_Log("  --baseline <suite_metrics.csv> Compare against prior suite metrics");
    SDL_Log("  --help                        Show this help");
}

static BenchCliOptions bench_default_cli_options(void) {
    BenchCliOptions options = {
        .bench = false,
        .suite = false,
        .present_mode = SDL_GPU_PRESENTMODE_IMMEDIATE,
        .camera_state = TESTBED_BENCH_CAMERA_ZOOM_IN_CENTER,
        .diagnostic_mode = TESTBED_BENCH_DIAGNOSTIC_DEFAULT,
        .wireframe_enabled = false,
        .debug_ui_enabled = false,
        .profiler_enabled = false,
        .spawn_count = BENCH_BASELINE_SPAWN_COUNT,
        .warmup_s = BENCH_DEFAULT_WARMUP_S,
        .sample_s = BENCH_DEFAULT_SAMPLE_S,
        .repetitions = BENCH_DEFAULT_REPETITIONS,
        .repeat_index = -1,
        .allowed_frames_in_flight = BENCH_DEFAULT_ALLOWED_FRAMES_IN_FLIGHT,
        .output_dir = BENCH_DEFAULT_OUTPUT_DIR,
        .baseline_metrics_csv = NULL,
        .help = false,
    };
    return options;
}

static bool bench_parse_cli_options(const int argc, char **const argv, BenchCliOptions *const out_options) {
    if (!out_options) {
        return false;
    }

    BenchCliOptions options = bench_default_cli_options();

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!arg) {
            continue;
        }

        if (SDL_strcmp(arg, "--help") == 0 || SDL_strcmp(arg, "-h") == 0) {
            options.help = true;
            continue;
        }

        if (SDL_strcmp(arg, "--bench") == 0) {
            options.bench = true;
            continue;
        }

        if (SDL_strcmp(arg, "--bench-suite") == 0) {
            options.bench = true;
            options.suite = true;
            continue;
        }

        if (SDL_strcmp(arg, "--present-mode") == 0) {
            if (i + 1 >= argc || !bench_parse_present_mode(argv[++i], &options.present_mode)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Invalid --present-mode value. Expected: immediate|mailbox|vsync");
                return false;
            }
            continue;
        }

        if (SDL_strcmp(arg, "--camera-state") == 0) {
            if (i + 1 >= argc || !bench_parse_camera_state(argv[++i], &options.camera_state)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Invalid --camera-state value. Expected: zoom_out_center|zoom_in_center|zoom_in_offmap");
                return false;
            }
            continue;
        }

        if (SDL_strcmp(arg, "--wireframe") == 0) {
            if (i + 1 >= argc || !bench_parse_bool_toggle(argv[++i], &options.wireframe_enabled)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid --wireframe value. Expected: on|off");
                return false;
            }
            continue;
        }

        if (SDL_strcmp(arg, "--spawn-profile") == 0) {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Missing --spawn-profile value");
                return false;
            }
            const char *profile = argv[++i];
            if (SDL_strcasecmp(profile, "baseline") == 0) {
                options.spawn_count = BENCH_BASELINE_SPAWN_COUNT;
            } else if (SDL_strcasecmp(profile, "stress") == 0) {
                options.spawn_count = BENCH_STRESS_SPAWN_COUNT;
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid --spawn-profile value. Expected: baseline|stress");
                return false;
            }
            continue;
        }

        if (SDL_strcmp(arg, "--spawn-count") == 0) {
            if (i + 1 >= argc || !bench_parse_int(argv[++i], &options.spawn_count) || options.spawn_count < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid --spawn-count value");
                return false;
            }
            continue;
        }

        if (SDL_strcmp(arg, "--diagnostic") == 0) {
            if (i + 1 >= argc || !bench_parse_diagnostic_mode(argv[++i], &options.diagnostic_mode)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Invalid --diagnostic value. Expected: "
                             "default|world-only|ui-only|wire-only|no-draw|upload-suppressed");
                return false;
            }
            continue;
        }

        if (SDL_strcmp(arg, "--debug-ui") == 0) {
            if (i + 1 >= argc || !bench_parse_bool_toggle(argv[++i], &options.debug_ui_enabled)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid --debug-ui value. Expected: on|off");
                return false;
            }
            continue;
        }

        if (SDL_strcmp(arg, "--profiler") == 0) {
            if (i + 1 >= argc || !bench_parse_bool_toggle(argv[++i], &options.profiler_enabled)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid --profiler value. Expected: on|off");
                return false;
            }
            continue;
        }

        if (SDL_strcmp(arg, "--warmup-s") == 0) {
            if (i + 1 >= argc || !bench_parse_float(argv[++i], &options.warmup_s) || options.warmup_s < 0.0f) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid --warmup-s value");
                return false;
            }
            continue;
        }

        if (SDL_strcmp(arg, "--sample-s") == 0) {
            if (i + 1 >= argc || !bench_parse_float(argv[++i], &options.sample_s) || options.sample_s <= 0.0f) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid --sample-s value");
                return false;
            }
            continue;
        }

        if (SDL_strcmp(arg, "--repetitions") == 0) {
            if (i + 1 >= argc || !bench_parse_int(argv[++i], &options.repetitions) || options.repetitions <= 0) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid --repetitions value");
                return false;
            }
            continue;
        }

        if (SDL_strcmp(arg, "--frames-in-flight") == 0) {
            if (i + 1 >= argc || !bench_parse_int(argv[++i], &options.allowed_frames_in_flight) ||
                options.allowed_frames_in_flight < 1 || options.allowed_frames_in_flight > 3) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid --frames-in-flight value. Expected: 1|2|3");
                return false;
            }
            continue;
        }

        if (SDL_strcmp(arg, "--repeat-index") == 0) {
            if (i + 1 >= argc || !bench_parse_int(argv[++i], &options.repeat_index) || options.repeat_index < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid --repeat-index value");
                return false;
            }
            continue;
        }

        if (SDL_strcmp(arg, "--output-dir") == 0) {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Missing --output-dir value");
                return false;
            }
            options.output_dir = argv[++i];
            continue;
        }

        if (SDL_strcmp(arg, "--baseline") == 0) {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Missing --baseline value");
                return false;
            }
            options.baseline_metrics_csv = argv[++i];
            continue;
        }

        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown argument: %s", arg);
        return false;
    }

    *out_options = options;
    return true;
}

static bool bench_join_path(char *const out, const size_t out_size, const char *const a, const char *const b) {
    if (!out || out_size == 0 || !a || !b) {
        return false;
    }

    const size_t a_len = SDL_strlen(a);
    const bool needs_sep = a_len > 0 && a[a_len - 1] != '/';
    const int written = SDL_snprintf(out, out_size, "%s%s%s", a, needs_sep ? "/" : "", b);
    return written > 0 && (size_t)written < out_size;
}

static bool bench_ensure_directory_recursive(const char *const path) {
    if (!path || path[0] == '\0') {
        return false;
    }

    char tmp[PATH_MAX] = {0};
    if (SDL_strlcpy(tmp, path, sizeof(tmp)) >= sizeof(tmp)) {
        return false;
    }

    const size_t len = SDL_strlen(tmp);
    if (len == 0U) {
        return false;
    }

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (char *cursor = tmp + 1; *cursor != '\0'; cursor++) {
        if (*cursor != '/') {
            continue;
        }

        *cursor = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            return false;
        }
        *cursor = '/';
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return false;
    }

    return true;
}

static void bench_make_timestamp(char *const out, const size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }

    const time_t now = time(NULL);
    struct tm local_tm = {0};
    localtime_r(&now, &local_tm);
    SDL_snprintf(out,
                 out_size,
                 "%04d%02d%02d_%02d%02d%02d",
                 local_tm.tm_year + 1900,
                 local_tm.tm_mon + 1,
                 local_tm.tm_mday,
                 local_tm.tm_hour,
                 local_tm.tm_min,
                 local_tm.tm_sec);
}

static bool bench_sample_store_init(BenchSampleStore *const store, const uint32_t initial_capacity) {
    if (!store || initial_capacity == 0U) {
        return false;
    }

    SDL_memset(store, 0, sizeof(*store));
    store->frame_cpu_ms = SDL_malloc(sizeof(float) * initial_capacity);
    store->acquire_ms = SDL_malloc(sizeof(float) * initial_capacity);
    if (!store->frame_cpu_ms || !store->acquire_ms) {
        SDL_free(store->frame_cpu_ms);
        SDL_free(store->acquire_ms);
        SDL_memset(store, 0, sizeof(*store));
        return false;
    }

    store->capacity = initial_capacity;
    store->count = 0;
    return true;
}

static bool bench_sample_store_push(BenchSampleStore *const store, const float frame_cpu_ms, const float acquire_ms) {
    if (!store) {
        return false;
    }

    if (store->count >= store->capacity) {
        return false;
    }

    store->frame_cpu_ms[store->count] = frame_cpu_ms;
    store->acquire_ms[store->count] = acquire_ms;
    store->count++;
    return true;
}

static void bench_sample_store_destroy(BenchSampleStore *const store) {
    if (!store) {
        return;
    }

    SDL_free(store->frame_cpu_ms);
    SDL_free(store->acquire_ms);
    SDL_memset(store, 0, sizeof(*store));
}

static int bench_compare_float(const void *const a, const void *const b) {
    const float fa = *(const float *)a;
    const float fb = *(const float *)b;
    if (fa < fb) {
        return -1;
    }
    if (fa > fb) {
        return 1;
    }
    return 0;
}

static float bench_percentile_sorted(const float *const sorted, const uint32_t count, const float percentile) {
    if (!sorted || count == 0U) {
        return 0.0f;
    }

    if (count == 1U) {
        return sorted[0];
    }

    const double position = ((double)percentile / 100.0) * (double)(count - 1U);
    const uint32_t lo = (uint32_t)position;
    const uint32_t hi = lo + 1U < count ? lo + 1U : lo;
    const double frac = position - (double)lo;
    return (float)((double)sorted[lo] * (1.0 - frac) + (double)sorted[hi] * frac);
}

static bool bench_compute_percentiles(
    const float *const values, const uint32_t count, float *const out_p50, float *const out_p95, float *const out_p99) {
    if (!values || count == 0U || !out_p50 || !out_p95 || !out_p99) {
        return false;
    }

    float *sorted = SDL_malloc(sizeof(float) * count);
    if (!sorted) {
        return false;
    }

    SDL_memcpy(sorted, values, sizeof(float) * count);
    qsort(sorted, count, sizeof(float), bench_compare_float);

    *out_p50 = bench_percentile_sorted(sorted, count, 50.0f);
    *out_p95 = bench_percentile_sorted(sorted, count, 95.0f);
    *out_p99 = bench_percentile_sorted(sorted, count, 99.0f);

    SDL_free(sorted);
    return true;
}

static bool bench_write_csv_header(FILE *const csv_file) {
    if (!csv_file) {
        return false;
    }

    return fprintf(csv_file,
                   "frame_index,frame_cpu_ms,acquire_swapchain_ms,record_commands_ms,submit_ms,"
                   "render_pass_count,draw_calls_world,draw_calls_ui,draw_calls_lines,"
                   "uploaded_bytes_sprite,uploaded_bytes_world_geo,uploaded_bytes_ui_geo,"
                   "uploaded_bytes_ui_text,uploaded_bytes_line,uploaded_bytes_total,"
                   "instances_submitted,line_vertices_submitted,present_mode,camera_state,"
                   "wireframe_enabled,build_type\n") > 0;
}

static bool bench_write_csv_row(FILE *const csv_file,
                                const uint32_t frame_index,
                                const MisoRenderFrameStats *const stats,
                                const BenchScenario *const scenario) {
    if (!csv_file || !stats || !scenario) {
        return false;
    }

    return fprintf(csv_file,
                   "%u,%.6f,%.6f,%.6f,%.6f,"
                   "%u,%u,%u,%u,"
                   "%u,%u,%u,%u,%u,%u,"
                   "%u,%u,%s,%s,%s,%s\n",
                   frame_index,
                   stats->timing.frame_cpu_ms,
                   stats->timing.acquire_swapchain_ms,
                   stats->timing.record_commands_ms,
                   stats->timing.submit_ms,
                   stats->render_pass_count,
                   stats->draw_calls_world,
                   stats->draw_calls_ui,
                   stats->draw_calls_lines,
                   stats->uploaded_bytes_sprite,
                   stats->uploaded_bytes_world_geo,
                   stats->uploaded_bytes_ui_geo,
                   stats->uploaded_bytes_ui_text,
                   stats->uploaded_bytes_line,
                   stats->uploaded_bytes_total,
                   stats->instances_submitted,
                   stats->line_vertices_submitted,
                   bench_present_mode_name(scenario->present_mode),
                   testbed_bench_camera_state_name(scenario->camera_state),
                   scenario->wireframe_enabled ? "true" : "false",
                   bench_build_type_name()) > 0;
}

static const char *bench_classify_bottleneck_signal(const BenchScenarioResult *const result, float *const out_score) {
    if (!result) {
        return "unknown";
    }

    const float frame_p95 = result->median_frame_p95_ms;
    const float acquire_p95 = result->median_acquire_p95_ms;
    const float upload_avg = result->median_upload_avg_mib;
    const float acquire_ratio = frame_p95 > 0.0f ? acquire_p95 / frame_p95 : 0.0f;

    if (acquire_p95 >= BENCH_ACQUIRE_BURST_THRESHOLD_MS && acquire_ratio >= 0.55f) {
        if (out_score) {
            *out_score = acquire_p95;
        }
        return "acquire_backpressure";
    }

    if (upload_avg >= 0.50f) {
        if (out_score) {
            *out_score = upload_avg;
        }
        return "upload_churn";
    }

    if (frame_p95 >= 8.0f && acquire_ratio <= 0.30f) {
        if (out_score) {
            *out_score = frame_p95 - acquire_p95;
        }
        return "record_or_raster";
    }

    if (out_score) {
        *out_score = frame_p95;
    }
    return "mixed";
}

typedef struct BenchBottleneckRankEntry {
    const BenchScenarioResult *result;
    float score;
} BenchBottleneckRankEntry;

static int bench_compare_bottleneck_rank_desc(const void *const lhs, const void *const rhs) {
    const BenchBottleneckRankEntry *a = (const BenchBottleneckRankEntry *)lhs;
    const BenchBottleneckRankEntry *b = (const BenchBottleneckRankEntry *)rhs;
    if (a->score > b->score) {
        return -1;
    }
    if (a->score < b->score) {
        return 1;
    }
    return 0;
}

static void bench_scenario_make_name(BenchScenario *const scenario) {
    if (!scenario) {
        return;
    }

    SDL_snprintf(scenario->name,
                 sizeof(scenario->name),
                 "%s__fif_%d__%s__wire_%s__diag_%s__spawn_%d",
                 bench_present_mode_slug(scenario->present_mode),
                 scenario->allowed_frames_in_flight,
                 testbed_bench_camera_state_name(scenario->camera_state),
                 scenario->wireframe_enabled ? "on" : "off",
                 testbed_bench_diagnostic_mode_name(scenario->diagnostic_mode),
                 scenario->spawn_count);
}

static bool bench_setup_runtime(MisoEngine **const out_engine, TestbedGame **const out_game) {
    if (!out_engine || !out_game) {
        return false;
    }

    *out_engine = NULL;
    *out_game = NULL;

    const MisoConfig config = {
        .window_width = 1920,
        .window_height = 1080,
        .window_title = "miso testbed",
        .enable_vsync = true,
        .sim_tick_hz = 20,
        .max_sim_steps_per_frame = 8,
    };

    MisoEngine *engine = NULL;
    if (miso_create(&config, &engine) != MISO_OK || !engine) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create engine");
        return false;
    }

    TestbedGame *game = NULL;
    if (testbed_game_create(engine, &game) != MISO_OK || !game) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create testbed game");
        miso_destroy(engine);
        return false;
    }

    if (miso_game_register(engine, testbed_game_hooks(), game) != MISO_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to register testbed hooks");
        testbed_game_destroy(game);
        miso_destroy(engine);
        return false;
    }

    *out_engine = engine;
    *out_game = game;
    return true;
}

static void bench_teardown_runtime(MisoEngine *engine, TestbedGame *game) {
    if (game) {
        testbed_game_destroy(game);
    }
    if (engine) {
        miso_destroy(engine);
    }
}

static bool bench_write_run_summary_json(const BenchScenario *const scenario,
                                         const BenchRunSummary *const summary,
                                         const int run_index) {
    if (!scenario || !summary || summary->summary_json_path[0] == '\0') {
        return false;
    }

    FILE *json_file = fopen(summary->summary_json_path, "w");
    if (!json_file) {
        return false;
    }

    fprintf(json_file, "{\n");
    fprintf(json_file, "  \"schema_version\": \"%s\",\n", BENCH_SCHEMA_VERSION);
    fprintf(json_file, "  \"run_index\": %d,\n", run_index);
    fprintf(json_file, "  \"build_type\": \"%s\",\n", bench_build_type_name());
    fprintf(json_file, "  \"present_mode\": \"%s\",\n", bench_present_mode_name(scenario->present_mode));
    fprintf(json_file, "  \"camera_state\": \"%s\",\n", testbed_bench_camera_state_name(scenario->camera_state));
    fprintf(json_file, "  \"wireframe_enabled\": %s,\n", scenario->wireframe_enabled ? "true" : "false");
    fprintf(
        json_file, "  \"diagnostic_mode\": \"%s\",\n", testbed_bench_diagnostic_mode_name(scenario->diagnostic_mode));
    fprintf(json_file, "  \"allowed_frames_in_flight\": %d,\n", scenario->allowed_frames_in_flight);
    fprintf(json_file, "  \"spawn_count\": %d,\n", scenario->spawn_count);
    fprintf(json_file, "  \"sampled_frames\": %u,\n", summary->sampled_frames);

    fprintf(json_file,
            "  \"fps\": {\"mean\": %.6f, \"min\": %.6f, \"max\": %.6f},\n",
            summary->fps_mean,
            summary->fps_min,
            summary->fps_max);
    fprintf(json_file,
            "  \"frame_ms\": {\"p50\": %.6f, \"p95\": %.6f, \"p99\": %.6f},\n",
            summary->frame_p50_ms,
            summary->frame_p95_ms,
            summary->frame_p99_ms);
    fprintf(json_file,
            "  \"acquire_ms\": {\"p50\": %.6f, \"p95\": %.6f, \"p99\": %.6f},\n",
            summary->acquire_p50_ms,
            summary->acquire_p95_ms,
            summary->acquire_p99_ms);
    fprintf(json_file,
            "  \"upload_mib\": {\"avg\": %.6f, \"peak\": %.6f},\n",
            summary->upload_avg_mib,
            summary->upload_peak_mib);
    fprintf(json_file,
            "  \"render_pass\": {\"mean\": %.6f, \"max\": %u},\n",
            summary->render_pass_mean,
            summary->render_pass_max);
    fprintf(json_file,
            "  \"acquire_burst\": {\"threshold_ms\": %.2f, \"longest_ms\": %.6f, \"frame_ratio\": %.6f},\n",
            BENCH_ACQUIRE_BURST_THRESHOLD_MS,
            summary->longest_acquire_burst_ms,
            summary->acquire_burst_frame_ratio);

    fprintf(json_file, "  \"gates\": {\n");
    fprintf(json_file, "    \"enabled\": %s,\n", summary->gates_enabled ? "true" : "false");
    fprintf(json_file, "    \"pass\": %s,\n", summary->gates_pass ? "true" : "false");
    fprintf(json_file,
            "    \"gate_a\": {\"pass\": %s, \"violations\": %u},\n",
            summary->gate_a_pass ? "true" : "false",
            summary->gate_a_violations);
    fprintf(json_file,
            "    \"gate_b\": {\"pass\": %s, \"violations\": %u},\n",
            summary->gate_b_pass ? "true" : "false",
            summary->gate_b_violations);
    fprintf(json_file,
            "    \"gate_c\": {\"pass\": %s, \"violations\": %u},\n",
            summary->gate_c_pass ? "true" : "false",
            summary->gate_c_violations);
    fprintf(json_file,
            "    \"gate_d\": {\"pass\": %s, \"violations\": %u},\n",
            summary->gate_d_pass ? "true" : "false",
            summary->gate_d_violations);
    fprintf(json_file,
            "    \"gate_e\": {\"pass\": %s, \"burst_frames\": %u, \"long_burst_limit_ms\": %u, "
            "\"burst_ratio_limit\": %.3f}\n",
            summary->gate_e_pass ? "true" : "false",
            summary->gate_e_burst_frames,
            summary->gate_e_long_burst_limit_ms,
            summary->gate_e_burst_ratio_limit);
    fprintf(json_file, "  }\n");
    fprintf(json_file, "}\n");

    fclose(json_file);
    return true;
}

static bool bench_run_single_scenario(MisoEngine *const engine,
                                      TestbedGame *const game,
                                      const BenchScenario *const scenario,
                                      const int run_index,
                                      const int total_repetitions,
                                      const float warmup_s,
                                      const float sample_s,
                                      const char *const suite_dir,
                                      BenchRunSummary *const out_summary) {
    if (!engine || !game || !scenario || !suite_dir || !out_summary) {
        return false;
    }

    char scenario_dir[PATH_MAX] = {0};
    if (!bench_join_path(scenario_dir, sizeof(scenario_dir), suite_dir, scenario->name)) {
        return false;
    }
    if (!bench_ensure_directory_recursive(scenario_dir)) {
        return false;
    }

    char run_dir[PATH_MAX] = {0};
    if (total_repetitions > 1) {
        char run_name[64] = {0};
        SDL_snprintf(run_name, sizeof(run_name), "run_%02d", run_index);
        if (!bench_join_path(run_dir, sizeof(run_dir), scenario_dir, run_name)) {
            return false;
        }
    } else {
        if (SDL_strlcpy(run_dir, scenario_dir, sizeof(run_dir)) >= sizeof(run_dir)) {
            return false;
        }
    }

    if (!bench_ensure_directory_recursive(run_dir)) {
        return false;
    }

    BenchRunSummary summary = {0};
    if (!bench_join_path(summary.frames_csv_path, sizeof(summary.frames_csv_path), run_dir, "frames.csv")) {
        return false;
    }
    if (!bench_join_path(summary.summary_json_path, sizeof(summary.summary_json_path), run_dir, "summary.json")) {
        return false;
    }

    FILE *csv_file = fopen(summary.frames_csv_path, "w");
    if (!csv_file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open CSV output: %s", summary.frames_csv_path);
        return false;
    }

    if (!bench_write_csv_header(csv_file)) {
        fclose(csv_file);
        return false;
    }

    const uint32_t initial_capacity = (uint32_t)(sample_s * (float)BENCH_MAX_SAMPLE_FPS) + 2048U;
    BenchSampleStore samples = {0};
    if (!bench_sample_store_init(&samples, initial_capacity > 0U ? initial_capacity : 4096U)) {
        fclose(csv_file);
        return false;
    }

    testbed_game_enable_benchmark_mode(game, true);
    testbed_game_set_benchmark_debug_ui(game, scenario->debug_ui_enabled);
    testbed_game_set_benchmark_profiler(game, scenario->profiler_enabled);
    testbed_game_set_benchmark_wireframe(game, scenario->wireframe_enabled);
    testbed_game_set_benchmark_diagnostic_mode(game, scenario->diagnostic_mode);
    testbed_game_set_benchmark_camera_state(game, scenario->camera_state);
    testbed_game_set_benchmark_upload_suppressed(game, false);
    testbed_game_reset_benchmark_scene(game, scenario->spawn_count);

    if (!Renderer_SetAllowedFramesInFlight((Uint32)scenario->allowed_frames_in_flight)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to set allowed frames in flight to %d for scenario %s",
                     scenario->allowed_frames_in_flight,
                     scenario->name);
        return false;
    }

    Renderer_SetPresentMode(scenario->present_mode);

    const Uint64 bench_timer_frequency = SDL_GetPerformanceFrequency();
    Uint64 warmup_start_ticks = SDL_GetPerformanceCounter();
    Uint64 sample_start_ticks = 0U;
    bool sampling = false;

    uint32_t frame_index = 0;
    double fps_sum = 0.0;
    float fps_min = FLT_MAX;
    float fps_max = 0.0f;

    double upload_sum_mib = 0.0;
    float upload_peak_mib = 0.0f;
    double render_pass_sum = 0.0;
    uint32_t render_pass_max = 0U;

    float current_acquire_burst_ms = 0.0f;
    float longest_acquire_burst_ms = 0.0f;
    uint32_t acquire_burst_frames = 0U;

    uint32_t gate_a_violations = 0U;
    uint32_t gate_b_violations = 0U;
    uint32_t gate_c_violations = 0U;
    uint32_t gate_d_violations = 0U;
    bool sample_store_full = false;

    while (testbed_game_is_running(game) && !g_bench_abort_requested) {
        if (!miso_begin_frame(engine)) {
            break;
        }

        testbed_game_frame_begin(game, miso_get_real_delta_seconds(engine));

        MisoEvent event;
        while (miso_poll_event(engine, &event)) {
        }
        testbed_game_frame_end_events(game);

        miso_run_simulation_ticks(engine, NULL, NULL);
        miso_end_frame(engine);
        testbed_game_frame_end(game);

        if (!sampling) {
            const Uint64 now_ticks = SDL_GetPerformanceCounter();
            const double warmup_elapsed_s =
                bench_timer_frequency > 0U ? (double)(now_ticks - warmup_start_ticks) / (double)bench_timer_frequency
                                           : 0.0;
            if (warmup_elapsed_s >= warmup_s) {
                sampling = true;
                sample_start_ticks = now_ticks;
                if (scenario->diagnostic_mode == TESTBED_BENCH_DIAGNOSTIC_UPLOAD_SUPPRESSED) {
                    testbed_game_set_benchmark_upload_suppressed(game, true);
                }
            }
            continue;
        }

        MisoRenderFrameStats stats = {0};
        if (!miso_render_get_frame_stats(engine, &stats)) {
            continue;
        }

        if (!bench_write_csv_row(csv_file, frame_index, &stats, scenario)) {
            bench_sample_store_destroy(&samples);
            fclose(csv_file);
            return false;
        }

        const float frame_cpu_ms = stats.timing.frame_cpu_ms > 0.0f ? stats.timing.frame_cpu_ms : 0.0001f;
        const float acquire_ms = stats.timing.acquire_swapchain_ms;
        const float fps = 1000.0f / frame_cpu_ms;
        const float upload_mib = (float)stats.uploaded_bytes_total / (1024.0f * 1024.0f);

        if (!bench_sample_store_push(&samples, frame_cpu_ms, acquire_ms)) {
            sample_store_full = true;
            break;
        }

        fps_sum += (double)fps;
        if (fps < fps_min) {
            fps_min = fps;
        }
        if (fps > fps_max) {
            fps_max = fps;
        }

        upload_sum_mib += (double)upload_mib;
        if (upload_mib > upload_peak_mib) {
            upload_peak_mib = upload_mib;
        }

        render_pass_sum += (double)stats.render_pass_count;
        if (stats.render_pass_count > render_pass_max) {
            render_pass_max = stats.render_pass_count;
        }

        if (acquire_ms >= BENCH_ACQUIRE_BURST_THRESHOLD_MS) {
            current_acquire_burst_ms += frame_cpu_ms;
            acquire_burst_frames++;
        } else {
            if (current_acquire_burst_ms > longest_acquire_burst_ms) {
                longest_acquire_burst_ms = current_acquire_burst_ms;
            }
            current_acquire_burst_ms = 0.0f;
        }

        if (stats.render_pass_count > 2U || stats.passes.begin_calls != stats.passes.end_calls ||
            stats.passes.begin_calls > 2U || stats.passes.world_passes > 1U || stats.passes.ui_passes > 1U) {
            gate_a_violations++;
        }

        if (stats.texture_upload_count > 0U || stats.texture_upload_bytes > 0U) {
            gate_b_violations++;
        }

        if (stats.transient_buffer_creations > 0U) {
            gate_c_violations++;
        }

        bool overflow_found = false;
        for (uint32_t i = 0; i < MISO_RENDER_STATS_STREAM_COUNT; i++) {
            if (stats.streams[i].overflow_count > 0U) {
                overflow_found = true;
                break;
            }
        }
        if (overflow_found) {
            gate_d_violations++;
        }

        frame_index++;
        const Uint64 now_ticks = SDL_GetPerformanceCounter();
        const double sample_elapsed_s =
            bench_timer_frequency > 0U ? (double)(now_ticks - sample_start_ticks) / (double)bench_timer_frequency : 0.0;
        if (sample_elapsed_s >= sample_s) {
            break;
        }
    }

    if (current_acquire_burst_ms > longest_acquire_burst_ms) {
        longest_acquire_burst_ms = current_acquire_burst_ms;
    }

    if (sample_store_full) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Benchmark sample store reached capacity (%u samples); truncating run.",
                    samples.capacity);
    }

    testbed_game_set_benchmark_upload_suppressed(game, false);

    fclose(csv_file);

    summary.sampled_frames = samples.count;
    summary.fps_mean = samples.count > 0U ? fps_sum / (double)samples.count : 0.0;
    summary.fps_min = samples.count > 0U ? fps_min : 0.0f;
    summary.fps_max = samples.count > 0U ? fps_max : 0.0f;
    summary.upload_avg_mib = samples.count > 0U ? (float)(upload_sum_mib / (double)samples.count) : 0.0f;
    summary.upload_peak_mib = upload_peak_mib;
    summary.render_pass_mean = samples.count > 0U ? (float)(render_pass_sum / (double)samples.count) : 0.0f;
    summary.render_pass_max = render_pass_max;
    summary.longest_acquire_burst_ms = longest_acquire_burst_ms;
    summary.acquire_burst_frame_ratio = samples.count > 0U ? (float)acquire_burst_frames / (float)samples.count : 0.0f;

    if (samples.count > 0U) {
        bench_compute_percentiles(
            samples.frame_cpu_ms, samples.count, &summary.frame_p50_ms, &summary.frame_p95_ms, &summary.frame_p99_ms);
        bench_compute_percentiles(samples.acquire_ms,
                                  samples.count,
                                  &summary.acquire_p50_ms,
                                  &summary.acquire_p95_ms,
                                  &summary.acquire_p99_ms);
    }

    summary.gates_enabled = scenario->gates_enabled;
    summary.gate_a_violations = gate_a_violations;
    summary.gate_b_violations = gate_b_violations;
    summary.gate_c_violations = gate_c_violations;
    summary.gate_d_violations = gate_d_violations;
    summary.gate_e_burst_frames = acquire_burst_frames;
    summary.gate_e_long_burst_limit_ms = scenario->spawn_count >= BENCH_STRESS_SPAWN_COUNT ? 120U : 80U;
    summary.gate_e_burst_ratio_limit = 0.10f;

    if (summary.gates_enabled) {
        summary.gate_a_pass = summary.gate_a_violations == 0U;
        summary.gate_b_pass = summary.gate_b_violations == 0U;
        summary.gate_c_pass = summary.gate_c_violations == 0U;
        summary.gate_d_pass = summary.gate_d_violations == 0U;
        summary.gate_e_pass = summary.longest_acquire_burst_ms <= (float)summary.gate_e_long_burst_limit_ms &&
                              summary.acquire_burst_frame_ratio <= summary.gate_e_burst_ratio_limit;
        summary.gates_pass = summary.gate_a_pass && summary.gate_b_pass && summary.gate_c_pass && summary.gate_d_pass &&
                             summary.gate_e_pass;
    } else {
        summary.gate_a_pass = true;
        summary.gate_b_pass = true;
        summary.gate_c_pass = true;
        summary.gate_d_pass = true;
        summary.gate_e_pass = true;
        summary.gates_pass = true;
    }

    bench_sample_store_destroy(&samples);

    if (!bench_write_run_summary_json(scenario, &summary, run_index)) {
        return false;
    }

    *out_summary = summary;
    return true;
}

static bool bench_collect_suite_scenarios(const BenchCliOptions *const options,
                                          BenchScenario *const scenarios,
                                          int *const out_count) {
    if (!options || !scenarios || !out_count) {
        return false;
    }

    int count = 0;

    if (options->suite) {
        const SDL_GPUPresentMode present_modes[] = {
            SDL_GPU_PRESENTMODE_IMMEDIATE,
            SDL_GPU_PRESENTMODE_MAILBOX,
            SDL_GPU_PRESENTMODE_VSYNC,
        };
        const TestbedBenchCameraState camera_states[] = {
            TESTBED_BENCH_CAMERA_ZOOM_OUT_CENTER,
            TESTBED_BENCH_CAMERA_ZOOM_IN_CENTER,
            TESTBED_BENCH_CAMERA_ZOOM_IN_OFFMAP,
        };
        const bool wire_options[] = {false, true};

        for (size_t pm = 0; pm < SDL_arraysize(present_modes); pm++) {
            for (size_t cs = 0; cs < SDL_arraysize(camera_states); cs++) {
                for (size_t wire = 0; wire < SDL_arraysize(wire_options); wire++) {
                    if (count >= BENCH_MAX_SCENARIOS) {
                        return false;
                    }

                    BenchScenario scenario = {
                        .present_mode = present_modes[pm],
                        .camera_state = camera_states[cs],
                        .diagnostic_mode = options->diagnostic_mode,
                        .wireframe_enabled = wire_options[wire],
                        .debug_ui_enabled = options->debug_ui_enabled,
                        .profiler_enabled = options->profiler_enabled,
                        .spawn_count = options->spawn_count,
                        .allowed_frames_in_flight = options->allowed_frames_in_flight,
                        .gates_enabled = options->diagnostic_mode == TESTBED_BENCH_DIAGNOSTIC_DEFAULT,
                    };
                    bench_scenario_make_name(&scenario);
                    scenarios[count++] = scenario;
                }
            }
        }
    } else {
        BenchScenario scenario = {
            .present_mode = options->present_mode,
            .camera_state = options->camera_state,
            .diagnostic_mode = options->diagnostic_mode,
            .wireframe_enabled = options->wireframe_enabled,
            .debug_ui_enabled = options->debug_ui_enabled,
            .profiler_enabled = options->profiler_enabled,
            .spawn_count = options->spawn_count,
            .allowed_frames_in_flight = options->allowed_frames_in_flight,
            .gates_enabled = options->diagnostic_mode == TESTBED_BENCH_DIAGNOSTIC_DEFAULT,
        };
        bench_scenario_make_name(&scenario);
        scenarios[count++] = scenario;
    }

    *out_count = count;
    return true;
}

static float bench_median_float(const float *const values, const int count) {
    if (!values || count <= 0) {
        return 0.0f;
    }

    float *sorted = SDL_malloc(sizeof(float) * (size_t)count);
    if (!sorted) {
        return 0.0f;
    }

    SDL_memcpy(sorted, values, sizeof(float) * (size_t)count);
    qsort(sorted, (size_t)count, sizeof(float), bench_compare_float);

    float median = 0.0f;
    if ((count & 1) != 0) {
        median = sorted[count / 2];
    } else {
        median = (sorted[count / 2 - 1] + sorted[count / 2]) * 0.5f;
    }

    SDL_free(sorted);
    return median;
}

static bool bench_write_suite_metrics_csv(const char *const suite_dir,
                                          const BenchScenarioResult *const results,
                                          const int scenario_count) {
    char metrics_path[PATH_MAX] = {0};
    if (!bench_join_path(metrics_path, sizeof(metrics_path), suite_dir, "suite_metrics.csv")) {
        return false;
    }

    FILE *metrics = fopen(metrics_path, "w");
    if (!metrics) {
        return false;
    }

    fprintf(metrics, "scenario,fps_mean_median,frame_p95_median_ms,acquire_p95_median_ms,upload_avg_mib_median\n");
    for (int i = 0; i < scenario_count; i++) {
        const BenchScenarioResult *entry = &results[i];
        fprintf(metrics,
                "%s,%.6f,%.6f,%.6f,%.6f\n",
                entry->scenario.name,
                entry->median_fps_mean,
                entry->median_frame_p95_ms,
                entry->median_acquire_p95_ms,
                entry->median_upload_avg_mib);
    }

    fclose(metrics);
    return true;
}

static bool bench_read_baseline_metrics(const char *const baseline_csv,
                                        BenchBaselineMetric **const out_metrics,
                                        int *const out_count) {
    if (!baseline_csv || !out_metrics || !out_count) {
        return false;
    }

    *out_metrics = NULL;
    *out_count = 0;

    FILE *file = fopen(baseline_csv, "r");
    if (!file) {
        return false;
    }

    int capacity = 32;
    int count = 0;
    BenchBaselineMetric *metrics = SDL_malloc(sizeof(BenchBaselineMetric) * (size_t)capacity);
    if (!metrics) {
        fclose(file);
        return false;
    }

    char line[1024] = {0};
    bool first_line = true;
    while (fgets(line, sizeof(line), file)) {
        if (first_line) {
            first_line = false;
            continue;
        }

        BenchBaselineMetric entry = {0};
        if (sscanf(line,
                   "%255[^,],%f,%f,%f,%f",
                   entry.scenario_name,
                   &entry.fps_mean,
                   &entry.frame_p95_ms,
                   &entry.acquire_p95_ms,
                   &entry.upload_avg_mib) != 5) {
            continue;
        }

        if (count >= capacity) {
            capacity *= 2;
            BenchBaselineMetric *new_metrics = SDL_realloc(metrics, sizeof(BenchBaselineMetric) * (size_t)capacity);
            if (!new_metrics) {
                SDL_free(metrics);
                fclose(file);
                return false;
            }
            metrics = new_metrics;
        }

        metrics[count++] = entry;
    }

    fclose(file);

    *out_metrics = metrics;
    *out_count = count;
    return true;
}

static const BenchBaselineMetric *
bench_find_baseline_metric(const BenchBaselineMetric *const metrics, const int count, const char *const scenario_name) {
    if (!metrics || count <= 0 || !scenario_name) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        if (SDL_strcmp(metrics[i].scenario_name, scenario_name) == 0) {
            return &metrics[i];
        }
    }

    return NULL;
}

static BenchBaselineComparison bench_compare_with_baseline(const BenchScenarioResult *const results,
                                                           const int scenario_count,
                                                           const char *const baseline_metrics_csv) {
    BenchBaselineComparison comparison = {
        .enabled = false,
        .pass = true,
        .matched_count = 0,
        .missing_count = 0,
        .regression_count = 0,
        .entries = NULL,
        .entry_count = 0,
    };

    if (!baseline_metrics_csv || !results || scenario_count <= 0) {
        return comparison;
    }

    BenchBaselineMetric *baseline_metrics = NULL;
    int baseline_count = 0;
    if (!bench_read_baseline_metrics(baseline_metrics_csv, &baseline_metrics, &baseline_count)) {
        comparison.enabled = true;
        comparison.pass = false;
        SDL_strlcpy(comparison.baseline_path, baseline_metrics_csv, sizeof(comparison.baseline_path));
        return comparison;
    }

    comparison.enabled = true;
    SDL_strlcpy(comparison.baseline_path, baseline_metrics_csv, sizeof(comparison.baseline_path));

    comparison.entries = SDL_calloc((size_t)scenario_count, sizeof(BenchComparisonEntry));
    comparison.entry_count = scenario_count;

    for (int i = 0; i < scenario_count; i++) {
        BenchComparisonEntry *entry = &comparison.entries[i];
        SDL_strlcpy(entry->scenario_name, results[i].scenario.name, sizeof(entry->scenario_name));

        const BenchBaselineMetric *baseline =
            bench_find_baseline_metric(baseline_metrics, baseline_count, results[i].scenario.name);
        if (!baseline) {
            comparison.missing_count++;
            entry->has_baseline = false;
            continue;
        }

        comparison.matched_count++;
        entry->has_baseline = true;

        entry->fps_delta_pct = baseline->fps_mean > 0.0f
                                   ? ((results[i].median_fps_mean - baseline->fps_mean) / baseline->fps_mean) * 100.0f
                                   : 0.0f;
        entry->frame_p95_delta_pct =
            baseline->frame_p95_ms > 0.0f
                ? ((results[i].median_frame_p95_ms - baseline->frame_p95_ms) / baseline->frame_p95_ms) * 100.0f
                : 0.0f;
        entry->acquire_p95_delta_pct =
            baseline->acquire_p95_ms > 0.0f
                ? ((results[i].median_acquire_p95_ms - baseline->acquire_p95_ms) / baseline->acquire_p95_ms) * 100.0f
                : 0.0f;
        entry->upload_avg_delta_pct =
            baseline->upload_avg_mib > 0.0f
                ? ((results[i].median_upload_avg_mib - baseline->upload_avg_mib) / baseline->upload_avg_mib) * 100.0f
                : 0.0f;

        const bool fps_regressed = entry->fps_delta_pct < -BENCH_GATE_FPS_DROP_TOLERANCE_PCT;
        const bool frame_p95_regressed = entry->frame_p95_delta_pct > BENCH_GATE_FRAME_P95_INCREASE_TOLERANCE_PCT;
        const bool acquire_p95_regressed = entry->acquire_p95_delta_pct > BENCH_GATE_ACQUIRE_P95_INCREASE_TOLERANCE_PCT;
        const bool upload_regressed = entry->upload_avg_delta_pct > BENCH_GATE_UPLOAD_AVG_INCREASE_TOLERANCE_PCT;

        entry->regressed = fps_regressed || frame_p95_regressed || acquire_p95_regressed || upload_regressed;
        if (entry->regressed) {
            comparison.regression_count++;
        }
    }

    comparison.pass = comparison.regression_count == 0;

    SDL_free(baseline_metrics);
    return comparison;
}

static void bench_free_baseline_comparison(BenchBaselineComparison *const comparison) {
    if (!comparison) {
        return;
    }

    SDL_free(comparison->entries);
    comparison->entries = NULL;
    comparison->entry_count = 0;
}

static bool bench_write_suite_summary_json(const char *const suite_dir,
                                           const BenchScenarioResult *const results,
                                           const int scenario_count,
                                           const int total_run_count,
                                           const bool suite_gates_pass,
                                           const BenchBaselineComparison *const baseline_comparison) {
    char suite_summary_path[PATH_MAX] = {0};
    if (!bench_join_path(suite_summary_path, sizeof(suite_summary_path), suite_dir, "suite_summary.json")) {
        return false;
    }

    FILE *json_file = fopen(suite_summary_path, "w");
    if (!json_file) {
        return false;
    }

    fprintf(json_file, "{\n");
    fprintf(json_file, "  \"schema_version\": \"%s\",\n", BENCH_SCHEMA_VERSION);
    fprintf(json_file, "  \"build_type\": \"%s\",\n", bench_build_type_name());
    fprintf(json_file, "  \"scenario_count\": %d,\n", scenario_count);
    fprintf(json_file, "  \"total_runs\": %d,\n", total_run_count);
    fprintf(json_file, "  \"suite_gates_pass\": %s,\n", suite_gates_pass ? "true" : "false");
    fprintf(json_file, "  \"scenarios\": [\n");

    for (int i = 0; i < scenario_count; i++) {
        const BenchScenarioResult *entry = &results[i];
        fprintf(json_file, "    {\n");
        fprintf(json_file, "      \"name\": \"%s\",\n", entry->scenario.name);
        fprintf(json_file, "      \"present_mode\": \"%s\",\n", bench_present_mode_name(entry->scenario.present_mode));
        fprintf(json_file,
                "      \"camera_state\": \"%s\",\n",
                testbed_bench_camera_state_name(entry->scenario.camera_state));
        fprintf(json_file, "      \"wireframe_enabled\": %s,\n", entry->scenario.wireframe_enabled ? "true" : "false");
        fprintf(json_file,
                "      \"diagnostic_mode\": \"%s\",\n",
                testbed_bench_diagnostic_mode_name(entry->scenario.diagnostic_mode));
        fprintf(json_file, "      \"allowed_frames_in_flight\": %d,\n", entry->scenario.allowed_frames_in_flight);
        fprintf(json_file, "      \"spawn_count\": %d,\n", entry->scenario.spawn_count);
        fprintf(json_file, "      \"run_count\": %d,\n", entry->run_count);
        fprintf(json_file, "      \"gate_failed_runs\": %d,\n", entry->gate_failed_runs);
        fprintf(json_file, "      \"bottleneck_signal\": \"%s\",\n", entry->bottleneck_signal);
        fprintf(json_file, "      \"bottleneck_score\": %.6f,\n", entry->bottleneck_score);
        fprintf(json_file,
                "      \"median\": {\"fps_mean\": %.6f, \"frame_p95_ms\": %.6f, \"acquire_p95_ms\": %.6f, "
                "\"upload_avg_mib\": %.6f}\n",
                entry->median_fps_mean,
                entry->median_frame_p95_ms,
                entry->median_acquire_p95_ms,
                entry->median_upload_avg_mib);
        fprintf(json_file, "    }%s\n", (i + 1 < scenario_count) ? "," : "");
    }

    fprintf(json_file, "  ],\n");

    fprintf(json_file, "  \"top_bottleneck_signals\": [\n");
    BenchBottleneckRankEntry *rank_entries = SDL_calloc((size_t)scenario_count, sizeof(BenchBottleneckRankEntry));
    int rank_count = 0;
    if (rank_entries) {
        for (int i = 0; i < scenario_count; i++) {
            rank_entries[rank_count].result = &results[i];
            rank_entries[rank_count].score = results[i].bottleneck_score;
            rank_count++;
        }
        qsort(rank_entries, (size_t)rank_count, sizeof(BenchBottleneckRankEntry), bench_compare_bottleneck_rank_desc);
    }

    const int top_count = rank_count < 5 ? rank_count : 5;
    for (int i = 0; i < top_count; i++) {
        const BenchScenarioResult *entry = rank_entries[i].result;
        fprintf(json_file,
                "    {\"scenario\": \"%s\", \"signal\": \"%s\", \"score\": %.6f}%s\n",
                entry->scenario.name,
                entry->bottleneck_signal,
                entry->bottleneck_score,
                (i + 1 < top_count) ? "," : "");
    }
    fprintf(json_file, "  ]");
    SDL_free(rank_entries);

    if (baseline_comparison && baseline_comparison->enabled) {
        fprintf(json_file, ",\n  \"baseline_comparison\": {\n");
        fprintf(json_file, "    \"baseline_metrics_csv\": \"%s\",\n", baseline_comparison->baseline_path);
        fprintf(json_file, "    \"enabled\": true,\n");
        fprintf(json_file, "    \"pass\": %s,\n", baseline_comparison->pass ? "true" : "false");
        fprintf(json_file, "    \"matched_count\": %d,\n", baseline_comparison->matched_count);
        fprintf(json_file, "    \"missing_count\": %d,\n", baseline_comparison->missing_count);
        fprintf(json_file, "    \"regression_count\": %d,\n", baseline_comparison->regression_count);
        fprintf(json_file,
                "    \"tolerances\": {\"fps_drop_pct\": %.2f, \"frame_p95_increase_pct\": %.2f, "
                "\"acquire_p95_increase_pct\": %.2f, \"upload_avg_increase_pct\": %.2f},\n",
                BENCH_GATE_FPS_DROP_TOLERANCE_PCT,
                BENCH_GATE_FRAME_P95_INCREASE_TOLERANCE_PCT,
                BENCH_GATE_ACQUIRE_P95_INCREASE_TOLERANCE_PCT,
                BENCH_GATE_UPLOAD_AVG_INCREASE_TOLERANCE_PCT);

        fprintf(json_file, "    \"entries\": [\n");
        for (int i = 0; i < baseline_comparison->entry_count; i++) {
            const BenchComparisonEntry *entry = &baseline_comparison->entries[i];
            fprintf(json_file,
                    "      {\"scenario\": \"%s\", \"has_baseline\": %s, \"regressed\": %s, "
                    "\"fps_delta_pct\": %.6f, \"frame_p95_delta_pct\": %.6f, "
                    "\"acquire_p95_delta_pct\": %.6f, \"upload_avg_delta_pct\": %.6f}%s\n",
                    entry->scenario_name,
                    entry->has_baseline ? "true" : "false",
                    entry->regressed ? "true" : "false",
                    entry->fps_delta_pct,
                    entry->frame_p95_delta_pct,
                    entry->acquire_p95_delta_pct,
                    entry->upload_avg_delta_pct,
                    (i + 1 < baseline_comparison->entry_count) ? "," : "");
        }
        fprintf(json_file, "    ]\n");
        fprintf(json_file, "  }\n");
        fprintf(json_file, "}\n");
    } else {
        fprintf(json_file, ",\n  \"baseline_comparison\": {\n");
        fprintf(json_file, "    \"enabled\": false,\n");
        fprintf(json_file, "    \"baseline_metrics_csv\": \"\",\n");
        fprintf(json_file, "    \"pass\": true,\n");
        fprintf(json_file, "    \"matched_count\": 0,\n");
        fprintf(json_file, "    \"missing_count\": 0,\n");
        fprintf(json_file, "    \"regression_count\": 0,\n");
        fprintf(json_file, "    \"entries\": []\n");
        fprintf(json_file, "  }\n");
        fprintf(json_file, "}\n");
    }

    fclose(json_file);
    return true;
}

static int bench_run_suite(const BenchCliOptions *const options) {
    BenchScenario scenarios[BENCH_MAX_SCENARIOS] = {0};
    int scenario_count = 0;
    if (!bench_collect_suite_scenarios(options, scenarios, &scenario_count) || scenario_count <= 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to collect benchmark scenarios");
        return 1;
    }

    char timestamp[64] = {0};
    bench_make_timestamp(timestamp, sizeof(timestamp));

    char suite_dir[PATH_MAX] = {0};
    if (!bench_join_path(suite_dir, sizeof(suite_dir), options->output_dir, timestamp)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to build suite output path");
        return 1;
    }

    if (!bench_ensure_directory_recursive(suite_dir)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create suite output directory: %s", suite_dir);
        return 1;
    }

    MisoEngine *engine = NULL;
    TestbedGame *game = NULL;
    if (!bench_setup_runtime(&engine, &game)) {
        return 1;
    }

    BenchScenarioResult *results = SDL_calloc((size_t)scenario_count, sizeof(BenchScenarioResult));
    if (!results) {
        bench_teardown_runtime(engine, game);
        return 1;
    }

    const int repetition_count = options->repeat_index >= 0 ? 1 : options->repetitions;
    const int start_repetition = options->repeat_index >= 0 ? options->repeat_index : 0;
    bool suite_gates_pass = true;
    int total_run_count = 0;

    for (int scenario_index = 0; scenario_index < scenario_count; scenario_index++) {
        results[scenario_index].scenario = scenarios[scenario_index];
        results[scenario_index].run_count = repetition_count;
        results[scenario_index].runs = SDL_calloc((size_t)repetition_count, sizeof(BenchRunSummary));
        if (!results[scenario_index].runs) {
            suite_gates_pass = false;
            break;
        }

        for (int local_run = 0; local_run < repetition_count; local_run++) {
            const int run_index = start_repetition + local_run;
            BenchRunSummary run_summary = {0};

            if (!bench_run_single_scenario(engine,
                                           game,
                                           &scenarios[scenario_index],
                                           run_index,
                                           repetition_count,
                                           options->warmup_s,
                                           options->sample_s,
                                           suite_dir,
                                           &run_summary)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Benchmark run failed: scenario=%s run=%d",
                             scenarios[scenario_index].name,
                             run_index);
                suite_gates_pass = false;
                break;
            }

            results[scenario_index].runs[local_run] = run_summary;
            if (run_summary.gates_enabled && !run_summary.gates_pass) {
                results[scenario_index].gate_failed_runs++;
                suite_gates_pass = false;
            }

            total_run_count++;
        }

        float *fps_values = SDL_malloc(sizeof(float) * (size_t)results[scenario_index].run_count);
        float *frame_p95_values = SDL_malloc(sizeof(float) * (size_t)results[scenario_index].run_count);
        float *acquire_p95_values = SDL_malloc(sizeof(float) * (size_t)results[scenario_index].run_count);
        float *upload_avg_values = SDL_malloc(sizeof(float) * (size_t)results[scenario_index].run_count);

        if (fps_values && frame_p95_values && acquire_p95_values && upload_avg_values) {
            for (int run_idx = 0; run_idx < results[scenario_index].run_count; run_idx++) {
                const BenchRunSummary *run = &results[scenario_index].runs[run_idx];
                fps_values[run_idx] = (float)run->fps_mean;
                frame_p95_values[run_idx] = run->frame_p95_ms;
                acquire_p95_values[run_idx] = run->acquire_p95_ms;
                upload_avg_values[run_idx] = run->upload_avg_mib;
            }

            results[scenario_index].median_fps_mean = bench_median_float(fps_values, results[scenario_index].run_count);
            results[scenario_index].median_frame_p95_ms =
                bench_median_float(frame_p95_values, results[scenario_index].run_count);
            results[scenario_index].median_acquire_p95_ms =
                bench_median_float(acquire_p95_values, results[scenario_index].run_count);
            results[scenario_index].median_upload_avg_mib =
                bench_median_float(upload_avg_values, results[scenario_index].run_count);
        }

        float bottleneck_score = 0.0f;
        const char *signal = bench_classify_bottleneck_signal(&results[scenario_index], &bottleneck_score);
        SDL_strlcpy(
            results[scenario_index].bottleneck_signal, signal, sizeof(results[scenario_index].bottleneck_signal));
        results[scenario_index].bottleneck_score = bottleneck_score;

        SDL_free(fps_values);
        SDL_free(frame_p95_values);
        SDL_free(acquire_p95_values);
        SDL_free(upload_avg_values);
    }

    BenchBaselineComparison baseline_comparison =
        bench_compare_with_baseline(results, scenario_count, options->baseline_metrics_csv);
    if (baseline_comparison.enabled && !baseline_comparison.pass) {
        suite_gates_pass = false;
    }

    bench_write_suite_metrics_csv(suite_dir, results, scenario_count);
    bench_write_suite_summary_json(
        suite_dir, results, scenario_count, total_run_count, suite_gates_pass, &baseline_comparison);

    SDL_Log("Benchmark suite complete: %s", suite_dir);

    for (int i = 0; i < scenario_count; i++) {
        SDL_free(results[i].runs);
    }
    SDL_free(results);

    bench_free_baseline_comparison(&baseline_comparison);
    bench_teardown_runtime(engine, game);
    return suite_gates_pass ? 0 : 2;
}

static int run_interactive(void) {
    LOG_init();

    MisoEngine *engine = NULL;
    TestbedGame *game = NULL;
    if (!bench_setup_runtime(&engine, &game)) {
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

        miso_run_simulation_ticks(engine, NULL, NULL);
        miso_end_frame(engine);
        testbed_game_frame_end(game);
    }

    bench_teardown_runtime(engine, game);
    return 0;
}

int main(int argc, char **argv) {
    BenchCliOptions options = {0};
    if (!bench_parse_cli_options(argc, argv, &options)) {
        bench_print_usage(argv[0]);
        return 1;
    }

    if (options.help) {
        bench_print_usage(argv[0]);
        return 0;
    }

    if (!options.bench) {
        return run_interactive();
    }

    SDL_Log("Benchmark mode enabled");
    SDL_Log("Build type: %s", bench_build_type_name());
    SDL_Log("Output dir: %s", options.output_dir);
    SDL_Log("Allowed frames in flight: %d", options.allowed_frames_in_flight);
    SDL_Log("Warmup: %.2fs | Sample: %.2fs | Repetitions: %d", options.warmup_s, options.sample_s, options.repetitions);

    bench_install_signal_handlers();
    return bench_run_suite(&options);
}
