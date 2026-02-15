#include "renderer.h"

#include "renderer_internal.h"

#include <SDL3/SDL_log.h>
#include <SDL3_image/SDL_image.h>
#include <stdint.h>
#include <string.h>

const char *getResourcePath(char *const string, const char *const relative_path) {
    SDL_snprintf(string, 512, "%s../../../../%s", SDL_GetBasePath(), relative_path);
    return string;
}

static SDL_GPUDevice *gpu_device = nullptr;
static SDL_Window *render_window = nullptr;
static SDL_GPUSampler *sampler = nullptr;

static SDL_GPUGraphicsPipeline *sprite_pipeline = nullptr;
static SDL_GPUGraphicsPipeline *geometry_pipeline = nullptr;
static SDL_GPUGraphicsPipeline *line_pipeline = nullptr;
static SDL_GPUGraphicsPipeline *text_pipeline = nullptr;

static TTF_TextEngine *text_engine = nullptr;

static SDL_GPUCommandBuffer *cmd_buffer = nullptr;
static SDL_GPUTexture *swapchain_texture = nullptr;
static SDL_GPUTexture *depth_texture = nullptr;
static SDL_GPUPresentMode g_present_mode = SDL_GPU_PRESENTMODE_VSYNC;
static bool g_upload_suppressed = false;

#define RENDERER_FRAMES_IN_FLIGHT 3U
#define RENDERER_STREAM_ALIGN 16U

#define RENDERER_MAX_SPRITE_CMDS 4096U
#define RENDERER_MAX_WORLD_GEOM_CMDS 4096U
#define RENDERER_MAX_LINE_CMDS 8192U
#define RENDERER_MAX_UI_GEOM_CMDS 4096U
#define RENDERER_MAX_UI_TEXT_CMDS 1024U
#define RENDERER_MAX_UI_TEXT_RANGES 16U

#define RENDERER_SPRITE_SLOT_BYTES (sizeof(SpriteInstance) * 100000U)
#define RENDERER_WORLD_GEOM_SLOT_BYTES (sizeof(SDL_Vertex) * 65536U)
#define RENDERER_LINE_SLOT_BYTES (sizeof(float) * 3U * 65536U)
#define RENDERER_UI_GEOM_SLOT_BYTES (sizeof(SDL_Vertex) * 131072U)
#define RENDERER_UI_TEXT_VERT_SLOT_BYTES (sizeof(float) * 4U * 262144U)
#define RENDERER_UI_TEXT_INDEX_SLOT_BYTES (sizeof(int) * 524288U)

typedef struct {
    float viewProjection[16];
    float waterParams[4];
} SpriteUniforms;

typedef struct {
    SDL_GPUBuffer *gpu;
    SDL_GPUTransferBuffer *transfer;
    uint8_t *mapped;
    Uint32 slot_size;
    Uint32 total_size;
    Uint32 slot_base;
    Uint32 write_offset;
    Uint32 peak_used_bytes;
    Uint32 overflow_count;
} RendererUploadStream;

typedef struct {
    SDL_GPUTexture *texture;
    Uint32 first_instance;
    Uint32 instance_count;
    SpriteUniforms uniforms;
} SpriteCmd;

typedef struct {
    Uint32 vertex_offset;
    Uint32 vertex_count;
    float matrix[16];
} GeometryCmd;

typedef struct {
    Uint32 vertex_offset;
    Uint32 vertex_count;
    SDL_FColor color;
    float matrix[16];
} LineCmd;

typedef struct {
    SDL_GPUTexture *atlas;
    Uint32 start_index;
    Uint32 index_count;
} UITextRangeCmd;

typedef struct {
    Uint32 vertex_offset;
    Uint32 index_offset;
    Uint32 vertex_count;
    Uint32 index_count;
    UITextRangeCmd ranges[RENDERER_MAX_UI_TEXT_RANGES];
    Uint32 range_count;
} UITextCmd;

static SpriteUniforms sprite_uniforms = {0};

static RendererUploadStream sprite_stream = {0};
static RendererUploadStream world_geom_stream = {0};
static RendererUploadStream line_stream = {0};
static RendererUploadStream ui_geom_stream = {0};
static RendererUploadStream ui_text_vert_stream = {0};
static RendererUploadStream ui_text_index_stream = {0};

static SpriteCmd sprite_cmds[RENDERER_MAX_SPRITE_CMDS] = {0};
static Uint32 sprite_cmd_count = 0;

static GeometryCmd world_geom_cmds[RENDERER_MAX_WORLD_GEOM_CMDS] = {0};
static Uint32 world_geom_cmd_count = 0;

static LineCmd line_cmds[RENDERER_MAX_LINE_CMDS] = {0};
static Uint32 line_cmd_count = 0;

static GeometryCmd ui_geom_cmds[RENDERER_MAX_UI_GEOM_CMDS] = {0};
static Uint32 ui_geom_cmd_count = 0;

static UITextCmd ui_text_cmds[RENDERER_MAX_UI_TEXT_CMDS] = {0};
static Uint32 ui_text_cmd_count = 0;

static Uint32 current_frame_slot = 0;
static bool frame_queues_flushed = false;

static RendererFrameStats g_frame_stats = {0};
static Uint64 g_frame_cpu_start_ticks = 0;
static Uint64 g_record_commands_start_ticks = 0;
static bool g_frame_active = false;

static float g_screen_projection[16] = {0};

static inline Uint32 renderer_align_up(const Uint32 value, const Uint32 align) {
    const Uint32 mask = align - 1U;
    return (value + mask) & ~mask;
}

static void renderer_make_screen_projection(float out[16]) {
    int w = 1;
    int h = 1;
    if (render_window) {
        SDL_GetWindowSizeInPixels(render_window, &w, &h);
    }
    if (w <= 0) {
        w = 1;
    }
    if (h <= 0) {
        h = 1;
    }

    out[0] = 2.0f / (float)w;
    out[1] = 0.0f;
    out[2] = 0.0f;
    out[3] = 0.0f;

    out[4] = 0.0f;
    out[5] = -2.0f / (float)h;
    out[6] = 0.0f;
    out[7] = 0.0f;

    out[8] = 0.0f;
    out[9] = 0.0f;
    out[10] = 1.0f;
    out[11] = 0.0f;

    out[12] = -1.0f;
    out[13] = 1.0f;
    out[14] = 0.0f;
    out[15] = 1.0f;
}

static SDL_GPUShader *LoadShader(SDL_GPUDevice *const device,
                                 const char *const path,
                                 const char *const entrypoint,
                                 const int num_samplers,
                                 const int num_uniform_buffers,
                                 const int num_storage_buffers,
                                 int num_storage_textures,
                                 const SDL_GPUShaderStage stage) {
    size_t code_size = 0;
    void *const code = SDL_LoadFile(path, &code_size);
    if (!code) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to load shader '%s' from %s: %s", entrypoint, path, SDL_GetError());
        return nullptr;
    }

    const SDL_GPUShaderCreateInfo create_info = {
        .code_size = code_size,
        .code = (const Uint8 *)code,
        .entrypoint = entrypoint,
        .format = SDL_GPU_SHADERFORMAT_MSL,
        .stage = stage,
        .num_samplers = (Uint32)num_samplers,
        .num_uniform_buffers = (Uint32)num_uniform_buffers,
        .num_storage_buffers = (Uint32)num_storage_buffers,
        .num_storage_textures = (Uint32)num_storage_textures,
    };

    SDL_GPUShader *const shader = SDL_CreateGPUShader(device, &create_info);
    SDL_free(code);
    return shader;
}

static bool
renderer_stream_init(RendererUploadStream *const stream, const SDL_GPUBufferUsageFlags usage, const Uint32 slot_size) {
    stream->slot_size = renderer_align_up(slot_size, RENDERER_STREAM_ALIGN);
    stream->total_size = stream->slot_size * RENDERER_FRAMES_IN_FLIGHT;

    const SDL_GPUBufferCreateInfo gpu_info = {
        .usage = usage,
        .size = stream->total_size,
    };
    stream->gpu = SDL_CreateGPUBuffer(gpu_device, &gpu_info);
    if (!stream->gpu) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create GPU buffer stream: %s", SDL_GetError());
        return false;
    }

    const SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = stream->total_size,
    };
    stream->transfer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_info);
    if (!stream->transfer) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create transfer stream: %s", SDL_GetError());
        SDL_ReleaseGPUBuffer(gpu_device, stream->gpu);
        stream->gpu = nullptr;
        return false;
    }

    stream->mapped = nullptr;
    stream->slot_base = 0;
    stream->write_offset = 0;
    stream->peak_used_bytes = 0;
    stream->overflow_count = 0;
    return true;
}

static void renderer_stream_shutdown(RendererUploadStream *const stream) {
    if (stream->mapped) {
        SDL_UnmapGPUTransferBuffer(gpu_device, stream->transfer);
        stream->mapped = nullptr;
    }
    if (stream->transfer) {
        SDL_ReleaseGPUTransferBuffer(gpu_device, stream->transfer);
        stream->transfer = nullptr;
    }
    if (stream->gpu) {
        SDL_ReleaseGPUBuffer(gpu_device, stream->gpu);
        stream->gpu = nullptr;
    }
}

static bool renderer_stream_begin_frame(RendererUploadStream *const stream, const Uint32 frame_slot) {
    stream->slot_base = frame_slot * stream->slot_size;
    stream->write_offset = stream->slot_base;
    stream->peak_used_bytes = 0;
    stream->overflow_count = 0;
    stream->mapped = (uint8_t *)SDL_MapGPUTransferBuffer(gpu_device, stream->transfer, true);
    if (!stream->mapped) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to map transfer stream: %s", SDL_GetError());
        return false;
    }
    return true;
}

static void renderer_stream_end_frame(RendererUploadStream *const stream) {
    if (stream->mapped) {
        SDL_UnmapGPUTransferBuffer(gpu_device, stream->transfer);
        stream->mapped = nullptr;
    }
}

static bool renderer_stream_alloc(RendererUploadStream *const stream,
                                  const Uint32 size,
                                  const Uint32 alignment,
                                  Uint32 *const out_offset) {
    if (!stream || !out_offset || size == 0) {
        return false;
    }

    const Uint32 aligned = renderer_align_up(stream->write_offset, alignment);
    const Uint32 end_offset = aligned + size;
    if (end_offset > (stream->slot_base + stream->slot_size)) {
        stream->overflow_count++;
        SDL_LogWarn(
            SDL_LOG_CATEGORY_RENDER, "Upload stream exhausted (slot_size=%u, requested=%u)", stream->slot_size, size);
        return false;
    }

    *out_offset = aligned;
    stream->write_offset = end_offset;
    const Uint32 used_after = end_offset - stream->slot_base;
    if (used_after > stream->peak_used_bytes) {
        stream->peak_used_bytes = used_after;
    }
    return true;
}

static bool renderer_stream_write(RendererUploadStream *const restrict stream,
                                  const void *const restrict src,
                                  const Uint32 size,
                                  const Uint32 alignment,
                                  Uint32 *const restrict out_offset) {
    if (!src || size == 0) {
        return false;
    }
    if (!renderer_stream_alloc(stream, size, alignment, out_offset)) {
        return false;
    }
    if (!g_upload_suppressed) {
        SDL_memcpy(stream->mapped + *out_offset, src, size);
    }
    return true;
}

static bool renderer_stream_upload_used(SDL_GPUCopyPass *const copy_pass, const RendererUploadStream *const stream) {
    if (!stream || stream->write_offset <= stream->slot_base) {
        return true;
    }
    if (g_upload_suppressed) {
        return true;
    }

    const Uint32 used = stream->write_offset - stream->slot_base;
    const SDL_GPUTransferBufferLocation source = {
        .transfer_buffer = stream->transfer,
        .offset = stream->slot_base,
    };
    const SDL_GPUBufferRegion destination = {
        .buffer = stream->gpu,
        .offset = stream->slot_base,
        .size = used,
    };

    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    return true;
}

static Uint32 renderer_stream_used_bytes(const RendererUploadStream *const stream) {
    if (!stream || stream->write_offset <= stream->slot_base) {
        return 0;
    }
    return stream->write_offset - stream->slot_base;
}

static void renderer_record_stream_stat(const RendererStatsStreamKind stream_kind,
                                        const RendererUploadStream *const stream) {
    RendererStreamStats *const stream_stats = &g_frame_stats.streams[stream_kind];
    stream_stats->used_bytes = renderer_stream_used_bytes(stream);
    stream_stats->peak_bytes = stream->peak_used_bytes;
    stream_stats->capacity_bytes = stream->slot_size;
    stream_stats->uploaded_bytes = g_upload_suppressed ? 0U : stream_stats->used_bytes;
    stream_stats->overflow_count = stream->overflow_count;
}

static void renderer_record_stream_stats(void) {
    renderer_record_stream_stat(RENDERER_STATS_STREAM_SPRITE, &sprite_stream);
    renderer_record_stream_stat(RENDERER_STATS_STREAM_WORLD_GEOMETRY, &world_geom_stream);
    renderer_record_stream_stat(RENDERER_STATS_STREAM_LINE, &line_stream);
    renderer_record_stream_stat(RENDERER_STATS_STREAM_UI_GEOMETRY, &ui_geom_stream);
    renderer_record_stream_stat(RENDERER_STATS_STREAM_UI_TEXT_VERT, &ui_text_vert_stream);
    renderer_record_stream_stat(RENDERER_STATS_STREAM_UI_TEXT_INDEX, &ui_text_index_stream);

    g_frame_stats.uploaded_bytes_sprite = g_frame_stats.streams[RENDERER_STATS_STREAM_SPRITE].uploaded_bytes;
    g_frame_stats.uploaded_bytes_world_geo = g_frame_stats.streams[RENDERER_STATS_STREAM_WORLD_GEOMETRY].uploaded_bytes;
    g_frame_stats.uploaded_bytes_line = g_frame_stats.streams[RENDERER_STATS_STREAM_LINE].uploaded_bytes;
    g_frame_stats.uploaded_bytes_ui_geo = g_frame_stats.streams[RENDERER_STATS_STREAM_UI_GEOMETRY].uploaded_bytes;
    g_frame_stats.uploaded_bytes_ui_text = g_frame_stats.streams[RENDERER_STATS_STREAM_UI_TEXT_VERT].uploaded_bytes +
                                           g_frame_stats.streams[RENDERER_STATS_STREAM_UI_TEXT_INDEX].uploaded_bytes;
    g_frame_stats.uploaded_bytes_total = g_frame_stats.uploaded_bytes_sprite + g_frame_stats.uploaded_bytes_world_geo +
                                         g_frame_stats.uploaded_bytes_line + g_frame_stats.uploaded_bytes_ui_geo +
                                         g_frame_stats.uploaded_bytes_ui_text;
}

static void renderer_reset_queues(void) {
    sprite_cmd_count = 0;
    world_geom_cmd_count = 0;
    line_cmd_count = 0;
    ui_geom_cmd_count = 0;
    ui_text_cmd_count = 0;
}

static void renderer_reset_frame_stats(void) {
    SDL_memset(&g_frame_stats, 0, sizeof(g_frame_stats));
}

static float renderer_elapsed_ms(const Uint64 start, const Uint64 end) {
    const Uint64 freq = SDL_GetPerformanceFrequency();
    if (freq == 0 || end <= start) {
        return 0.0f;
    }
    return (float)(((double)(end - start) * 1000.0) / (double)freq);
}

static void renderer_count_pass_begin(void) {
    g_frame_stats.passes.begin_calls++;
    g_frame_stats.render_pass_count++;
}

static void renderer_count_pass_end(void) {
    g_frame_stats.passes.end_calls++;
}

static void renderer_bind_sprite_pipeline(SDL_GPURenderPass *const pass, SDL_GPUTexture *const texture) {
    SDL_BindGPUGraphicsPipeline(pass, sprite_pipeline);
    SDL_BindGPUFragmentSamplers(pass, 0, &((SDL_GPUTextureSamplerBinding){.texture = texture, .sampler = sampler}), 1);
    SDL_BindGPUVertexStorageBuffers(pass, 0, &sprite_stream.gpu, 1);
}

static void renderer_draw_world_pass(SDL_GPUCommandBuffer *const cmd) {
    const SDL_GPUColorTargetInfo color_target = {
        .texture = swapchain_texture,
        .clear_color = {0.392f, 0.584f, 0.929f, 1.0f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };

    const SDL_GPUDepthStencilTargetInfo depth_target = {
        .texture = depth_texture,
        .clear_depth = 1.0f,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
    };

    SDL_GPURenderPass *const pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
    renderer_count_pass_begin();
    g_frame_stats.passes.world_passes++;

    const SDL_GPUTexture *bound_sprite_tex = nullptr;
    for (Uint32 i = 0; i < sprite_cmd_count; i++) {
        const SpriteCmd *const cmdi = &sprite_cmds[i];
        if (!cmdi->texture || cmdi->instance_count == 0) {
            continue;
        }

        if (bound_sprite_tex != cmdi->texture) {
            renderer_bind_sprite_pipeline(pass, cmdi->texture);
            bound_sprite_tex = cmdi->texture;
        }

        SDL_PushGPUVertexUniformData(cmd, 0, &cmdi->uniforms, sizeof(SpriteUniforms));
        SDL_DrawGPUPrimitives(pass, 6, cmdi->instance_count, 0, cmdi->first_instance);

        g_frame_stats.queues[RENDERER_STATS_QUEUE_SPRITE].draw_calls++;
        g_frame_stats.draw_calls_world++;
    }

    for (Uint32 i = 0; i < world_geom_cmd_count; i++) {
        const GeometryCmd *const cmdi = &world_geom_cmds[i];
        if (cmdi->vertex_count == 0) {
            continue;
        }

        SDL_BindGPUGraphicsPipeline(pass, geometry_pipeline);
        SDL_BindGPUVertexBuffers(
            pass, 0, &((SDL_GPUBufferBinding){.buffer = world_geom_stream.gpu, .offset = cmdi->vertex_offset}), 1);
        SDL_PushGPUVertexUniformData(cmd, 0, cmdi->matrix, sizeof(float) * 16U);
        SDL_DrawGPUPrimitives(pass, cmdi->vertex_count, 1, 0, 0);

        g_frame_stats.queues[RENDERER_STATS_QUEUE_WORLD_GEOMETRY].draw_calls++;
        g_frame_stats.draw_calls_world++;
    }

    for (Uint32 i = 0; i < line_cmd_count; i++) {
        const LineCmd *const cmdi = &line_cmds[i];
        if (cmdi->vertex_count == 0U) {
            continue;
        }

        SDL_BindGPUGraphicsPipeline(pass, line_pipeline);
        SDL_BindGPUVertexBuffers(
            pass, 0, &((SDL_GPUBufferBinding){.buffer = line_stream.gpu, .offset = cmdi->vertex_offset}), 1);
        SDL_PushGPUVertexUniformData(cmd, 0, cmdi->matrix, sizeof(float) * 16U);
        SDL_PushGPUFragmentUniformData(cmd, 0, &cmdi->color, sizeof(cmdi->color));
        SDL_DrawGPUPrimitives(pass, cmdi->vertex_count, 1, 0, 0);

        g_frame_stats.queues[RENDERER_STATS_QUEUE_LINE].draw_calls++;
        g_frame_stats.draw_calls_lines++;
    }

    SDL_EndGPURenderPass(pass);
    renderer_count_pass_end();
}

static void renderer_draw_ui_pass(SDL_GPUCommandBuffer *cmd) {
    const SDL_GPUColorTargetInfo color_target = {
        .texture = swapchain_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
    };

    SDL_GPURenderPass *const pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, NULL);
    renderer_count_pass_begin();
    g_frame_stats.passes.ui_passes++;

    for (Uint32 i = 0; i < ui_geom_cmd_count; i++) {
        const GeometryCmd *cmdi = &ui_geom_cmds[i];
        if (cmdi->vertex_count == 0) {
            continue;
        }

        SDL_BindGPUGraphicsPipeline(pass, geometry_pipeline);
        SDL_BindGPUVertexBuffers(
            pass, 0, &((SDL_GPUBufferBinding){.buffer = ui_geom_stream.gpu, .offset = cmdi->vertex_offset}), 1);
        SDL_PushGPUVertexUniformData(cmd, 0, g_screen_projection, sizeof(float) * 16U);
        SDL_DrawGPUPrimitives(pass, cmdi->vertex_count, 1, 0, 0);

        g_frame_stats.queues[RENDERER_STATS_QUEUE_UI_GEOMETRY].draw_calls++;
        g_frame_stats.draw_calls_ui++;
    }

    for (Uint32 i = 0; i < ui_text_cmd_count; i++) {
        const UITextCmd *cmdi = &ui_text_cmds[i];
        if (cmdi->range_count == 0 || cmdi->index_count == 0 || cmdi->vertex_count == 0) {
            continue;
        }

        SDL_BindGPUGraphicsPipeline(pass, text_pipeline);
        SDL_BindGPUVertexBuffers(
            pass, 0, &((SDL_GPUBufferBinding){.buffer = ui_text_vert_stream.gpu, .offset = cmdi->vertex_offset}), 1);
        SDL_BindGPUIndexBuffer(
            pass,
            &((SDL_GPUBufferBinding){.buffer = ui_text_index_stream.gpu, .offset = cmdi->index_offset}),
            SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_PushGPUVertexUniformData(cmd, 0, g_screen_projection, sizeof(float) * 16U);

        constexpr float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        SDL_PushGPUFragmentUniformData(cmd, 0, color, sizeof(color));

        for (Uint32 r = 0; r < cmdi->range_count; r++) {
            const UITextRangeCmd *const range = &cmdi->ranges[r];
            if (!range->atlas || range->index_count == 0) {
                continue;
            }

            SDL_BindGPUFragmentSamplers(
                pass, 0, &((SDL_GPUTextureSamplerBinding){.texture = range->atlas, .sampler = sampler}), 1);
            SDL_DrawGPUIndexedPrimitives(pass, range->index_count, 1, range->start_index, 0, 0);

            g_frame_stats.queues[RENDERER_STATS_QUEUE_UI_TEXT].draw_calls++;
            g_frame_stats.draw_calls_ui++;
        }
    }

    SDL_EndGPURenderPass(pass);
    renderer_count_pass_end();
}

static void renderer_flush_queued_draws(void) {
    if (frame_queues_flushed || !cmd_buffer || !swapchain_texture) {
        return;
    }

    renderer_make_screen_projection(g_screen_projection);

    renderer_stream_end_frame(&sprite_stream);
    renderer_stream_end_frame(&world_geom_stream);
    renderer_stream_end_frame(&line_stream);
    renderer_stream_end_frame(&ui_geom_stream);
    renderer_stream_end_frame(&ui_text_vert_stream);
    renderer_stream_end_frame(&ui_text_index_stream);
    renderer_record_stream_stats();

    SDL_GPUCopyPass *const copy_pass = SDL_BeginGPUCopyPass(cmd_buffer);
    renderer_stream_upload_used(copy_pass, &sprite_stream);
    renderer_stream_upload_used(copy_pass, &world_geom_stream);
    renderer_stream_upload_used(copy_pass, &line_stream);
    renderer_stream_upload_used(copy_pass, &ui_geom_stream);
    renderer_stream_upload_used(copy_pass, &ui_text_vert_stream);
    renderer_stream_upload_used(copy_pass, &ui_text_index_stream);
    SDL_EndGPUCopyPass(copy_pass);

    renderer_draw_world_pass(cmd_buffer);
    renderer_draw_ui_pass(cmd_buffer);

    frame_queues_flushed = true;
}

static void CreateDepthTexture(const Uint32 width, const Uint32 height) {
    if (depth_texture) {
        SDL_ReleaseGPUTexture(gpu_device, depth_texture);
        depth_texture = nullptr;
    }

    SDL_GPUTextureCreateInfo depth_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
    };
    depth_texture = SDL_CreateGPUTexture(gpu_device, &depth_info);
}

bool Renderer_Init(SDL_Window *const window) {
    render_window = window;

    gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
    if (!gpu_device) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create SDL_GPU device: %s", SDL_GetError());
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(gpu_device, window)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to claim window for GPU device: %s", SDL_GetError());
        return false;
    }

    if (!SDL_SetGPUSwapchainParameters(
            gpu_device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_MAILBOX)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_GPU, "Present mode MAILBOX unavailable, falling back to VSYNC");
        if (!SDL_SetGPUSwapchainParameters(
                gpu_device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC)) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to set swapchain parameters: %s", SDL_GetError());
            return false;
        }
        g_present_mode = SDL_GPU_PRESENTMODE_VSYNC;
    } else {
        g_present_mode = SDL_GPU_PRESENTMODE_MAILBOX;
    }

    char shader_path[512] = {0};

    const SDL_GPUColorTargetDescription color_target_desc = {
        .format = SDL_GetGPUSwapchainTextureFormat(gpu_device, window),
        .blend_state =
            {
                .enable_blend = true,
                .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .color_blend_op = SDL_GPU_BLENDOP_ADD,
                .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            },
    };

    SDL_GPUShader *const sprite_vs = LoadShader(gpu_device,
                                                getResourcePath(shader_path, "shaders/sprite.metal"),
                                                "vertex_main",
                                                0,
                                                1,
                                                1,
                                                0,
                                                SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *const sprite_fs = LoadShader(gpu_device,
                                                getResourcePath(shader_path, "shaders/sprite.metal"),
                                                "fragment_main",
                                                1,
                                                0,
                                                0,
                                                0,
                                                SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!sprite_vs || !sprite_fs) {
        return false;
    }

    const SDL_GPUGraphicsPipelineCreateInfo sprite_pipe_info = {
        .vertex_shader = sprite_vs,
        .fragment_shader = sprite_fs,
        .target_info =
            {
                .num_color_targets = 1,
                .color_target_descriptions = &color_target_desc,
                .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                .has_depth_stencil_target = true,
            },
        .depth_stencil_state =
            {
                .enable_depth_test = true,
                .enable_depth_write = true,
                .compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
            },
        .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1},
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {.cull_mode = SDL_GPU_CULLMODE_NONE},
    };
    sprite_pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &sprite_pipe_info);
    SDL_ReleaseGPUShader(gpu_device, sprite_vs);
    SDL_ReleaseGPUShader(gpu_device, sprite_fs);
    if (!sprite_pipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create sprite pipeline: %s", SDL_GetError());
        return false;
    }

    SDL_GPUShader *const geo_vs = LoadShader(gpu_device,
                                             getResourcePath(shader_path, "shaders/geometry.metal"),
                                             "vertex_geometry",
                                             0,
                                             1,
                                             0,
                                             0,
                                             SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *const geo_fs = LoadShader(gpu_device,
                                             getResourcePath(shader_path, "shaders/geometry.metal"),
                                             "fragment_geometry",
                                             0,
                                             0,
                                             0,
                                             0,
                                             SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!geo_vs || !geo_fs) {
        return false;
    }

    const SDL_GPUVertexAttribute geo_attrs[] = {
        {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 0},
        {.location = 1,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
         .offset = (Uint32)(sizeof(float) * 2)},
        {.location = 2,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset = (Uint32)(sizeof(float) * 6)},
    };
    const SDL_GPUVertexBufferDescription geo_binding = {
        .slot = 0,
        .pitch = (Uint32)sizeof(SDL_Vertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };

    const SDL_GPUGraphicsPipelineCreateInfo geo_pipe_info = {
        .vertex_shader = geo_vs,
        .fragment_shader = geo_fs,
        .vertex_input_state =
            {
                .num_vertex_attributes = 3,
                .vertex_attributes = geo_attrs,
                .num_vertex_buffers = 1,
                .vertex_buffer_descriptions = &geo_binding,
            },
        .target_info =
            {
                .num_color_targets = 1,
                .color_target_descriptions = &color_target_desc,
                .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                .has_depth_stencil_target = true,
            },
        .depth_stencil_state =
            {
                .enable_depth_test = true,
                .enable_depth_write = true,
                .compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
            },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1},
    };
    geometry_pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &geo_pipe_info);
    SDL_ReleaseGPUShader(gpu_device, geo_vs);
    SDL_ReleaseGPUShader(gpu_device, geo_fs);
    if (!geometry_pipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create geometry pipeline: %s", SDL_GetError());
        return false;
    }

    SDL_GPUShader *const line_vs = LoadShader(gpu_device,
                                              getResourcePath(shader_path, "shaders/ui.metal"),
                                              "vertex_line",
                                              0,
                                              1,
                                              0,
                                              0,
                                              SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *const line_fs = LoadShader(gpu_device,
                                              getResourcePath(shader_path, "shaders/ui.metal"),
                                              "fragment_line",
                                              0,
                                              1,
                                              0,
                                              0,
                                              SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!line_vs || !line_fs) {
        return false;
    }

    const SDL_GPUVertexAttribute line_attrs[] = {
        {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0},
    };
    const SDL_GPUVertexBufferDescription line_binding = {
        .slot = 0,
        .pitch = (Uint32)(sizeof(float) * 3),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };

    const SDL_GPUGraphicsPipelineCreateInfo line_pipe_info = {
        .vertex_shader = line_vs,
        .fragment_shader = line_fs,
        .vertex_input_state =
            {
                .num_vertex_attributes = 1,
                .vertex_attributes = line_attrs,
                .num_vertex_buffers = 1,
                .vertex_buffer_descriptions = &line_binding,
            },
        .target_info =
            {
                .num_color_targets = 1,
                .color_target_descriptions = &color_target_desc,
                .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                .has_depth_stencil_target = true,
            },
        .depth_stencil_state =
            {
                .enable_depth_test = true,
                .enable_depth_write = true,
                .compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
            },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST,
        .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1},
    };
    line_pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &line_pipe_info);
    SDL_ReleaseGPUShader(gpu_device, line_vs);
    SDL_ReleaseGPUShader(gpu_device, line_fs);
    if (!line_pipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create line pipeline: %s", SDL_GetError());
        return false;
    }

    SDL_GPUShader *const text_vs = LoadShader(gpu_device,
                                              getResourcePath(shader_path, "shaders/ui.metal"),
                                              "vertex_text",
                                              0,
                                              1,
                                              0,
                                              0,
                                              SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *const text_fs = LoadShader(gpu_device,
                                              getResourcePath(shader_path, "shaders/ui.metal"),
                                              "fragment_text",
                                              1,
                                              1,
                                              0,
                                              0,
                                              SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!text_vs || !text_fs) {
        return false;
    }

    SDL_GPUVertexAttribute text_attrs[] = {
        {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 0},
        {.location = 1,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset = (Uint32)(sizeof(float) * 2)},
    };
    SDL_GPUVertexBufferDescription text_binding = {
        .slot = 0,
        .pitch = (Uint32)(sizeof(float) * 4),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };

    SDL_GPUGraphicsPipelineCreateInfo text_pipe_info = {
        .vertex_shader = text_vs,
        .fragment_shader = text_fs,
        .vertex_input_state =
            {
                .num_vertex_attributes = 2,
                .vertex_attributes = text_attrs,
                .num_vertex_buffers = 1,
                .vertex_buffer_descriptions = &text_binding,
            },
        .target_info =
            {
                .num_color_targets = 1,
                .color_target_descriptions = &color_target_desc,
                .has_depth_stencil_target = false,
            },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1},
    };
    text_pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &text_pipe_info);
    SDL_ReleaseGPUShader(gpu_device, text_vs);
    SDL_ReleaseGPUShader(gpu_device, text_fs);
    if (!text_pipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create text pipeline: %s", SDL_GetError());
        return false;
    }

    SDL_GPUSamplerCreateInfo sampler_info = {
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    };
    sampler = SDL_CreateGPUSampler(gpu_device, &sampler_info);
    if (!sampler) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create sampler: %s", SDL_GetError());
        return false;
    }

    int w = 1;
    int h = 1;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    CreateDepthTexture((Uint32)w, (Uint32)h);

    if (!TTF_Init()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to init SDL_ttf: %s", SDL_GetError());
        return false;
    }

    text_engine = TTF_CreateGPUTextEngine(gpu_device);
    if (!text_engine) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create text engine: %s", SDL_GetError());
        return false;
    }

    if (!renderer_stream_init(&sprite_stream, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, RENDERER_SPRITE_SLOT_BYTES) ||
        !renderer_stream_init(&world_geom_stream, SDL_GPU_BUFFERUSAGE_VERTEX, RENDERER_WORLD_GEOM_SLOT_BYTES) ||
        !renderer_stream_init(&line_stream, SDL_GPU_BUFFERUSAGE_VERTEX, RENDERER_LINE_SLOT_BYTES) ||
        !renderer_stream_init(&ui_geom_stream, SDL_GPU_BUFFERUSAGE_VERTEX, RENDERER_UI_GEOM_SLOT_BYTES) ||
        !renderer_stream_init(&ui_text_vert_stream, SDL_GPU_BUFFERUSAGE_VERTEX, RENDERER_UI_TEXT_VERT_SLOT_BYTES) ||
        !renderer_stream_init(&ui_text_index_stream, SDL_GPU_BUFFERUSAGE_INDEX, RENDERER_UI_TEXT_INDEX_SLOT_BYTES)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to initialize upload streams");
        return false;
    }

    renderer_reset_queues();
    renderer_reset_frame_stats();

    sprite_uniforms.viewProjection[0] = 1.0f;
    sprite_uniforms.viewProjection[5] = 1.0f;
    sprite_uniforms.viewProjection[10] = 1.0f;
    sprite_uniforms.viewProjection[15] = 1.0f;

    return true;
}

void Renderer_Shutdown(void) {
    renderer_stream_shutdown(&sprite_stream);
    renderer_stream_shutdown(&world_geom_stream);
    renderer_stream_shutdown(&line_stream);
    renderer_stream_shutdown(&ui_geom_stream);
    renderer_stream_shutdown(&ui_text_vert_stream);
    renderer_stream_shutdown(&ui_text_index_stream);

    if (text_engine) {
        TTF_DestroyGPUTextEngine(text_engine);
        text_engine = nullptr;
    }
    TTF_Quit();

    if (sampler) {
        SDL_ReleaseGPUSampler(gpu_device, sampler);
        sampler = nullptr;
    }

    if (sprite_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, sprite_pipeline);
        sprite_pipeline = nullptr;
    }
    if (geometry_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, geometry_pipeline);
        geometry_pipeline = nullptr;
    }
    if (line_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, line_pipeline);
        line_pipeline = nullptr;
    }
    if (text_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, text_pipeline);
        text_pipeline = nullptr;
    }

    if (depth_texture) {
        SDL_ReleaseGPUTexture(gpu_device, depth_texture);
        depth_texture = nullptr;
    }

    if (gpu_device) {
        SDL_DestroyGPUDevice(gpu_device);
        gpu_device = nullptr;
    }
}

void Renderer_Resize(const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    CreateDepthTexture((Uint32)width, (Uint32)height);
}

void Renderer_SetPresentMode(const SDL_GPUPresentMode mode) {
    if (!gpu_device || !render_window) {
        return;
    }

    if (cmd_buffer) {
        SDL_SubmitGPUCommandBuffer(cmd_buffer);
        cmd_buffer = nullptr;
    }

    SDL_WaitForGPUIdle(gpu_device);

    if (!SDL_SetGPUSwapchainParameters(gpu_device, render_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) {
        if (mode == SDL_GPU_PRESENTMODE_MAILBOX) {
            SDL_LogWarn(SDL_LOG_CATEGORY_GPU, "Failed to set present mode (MAILBOX): %s. Falling back to VSYNC", SDL_GetError());
            if (SDL_SetGPUSwapchainParameters(
                    gpu_device, render_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC)) {
                g_present_mode = SDL_GPU_PRESENTMODE_VSYNC;
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to set present mode (VSYNC): %s.", SDL_GetError());
            }
        }
    } else {
        g_present_mode = mode;
    }

    swapchain_texture = nullptr;
}

void Renderer_SetVSync(const bool enabled) {
    Renderer_SetPresentMode(enabled ? SDL_GPU_PRESENTMODE_MAILBOX : SDL_GPU_PRESENTMODE_IMMEDIATE);
}

SDL_GPUPresentMode Renderer_GetPresentMode(void) {
    return g_present_mode;
}

void Renderer_SetUploadSuppressed(const bool enabled) {
    g_upload_suppressed = enabled;
}

bool Renderer_GetUploadSuppressed(void) {
    return g_upload_suppressed;
}

SDL_GPUTexture *Renderer_LoadTexture(const char *const path) {
    SDL_Surface *const surface = IMG_Load(path);
    if (!surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load image %s: %s", path, SDL_GetError());
        return nullptr;
    }

    const SDL_GPUTextureCreateInfo tex_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .width = (Uint32)surface->w,
        .height = (Uint32)surface->h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
    };

    SDL_GPUTexture *const texture = SDL_CreateGPUTexture(gpu_device, &tex_info);
    if (!texture) {
        SDL_DestroySurface(surface);
        return nullptr;
    }

    SDL_Surface *const converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ABGR8888);
    if (!converted) {
        SDL_DestroySurface(surface);
        SDL_ReleaseGPUTexture(gpu_device, texture);
        return nullptr;
    }

    const Uint32 upload_size = (Uint32)(converted->w * converted->h * 4);
    if (g_frame_active) {
        g_frame_stats.texture_upload_count++;
        g_frame_stats.texture_upload_bytes += upload_size;
    }
    const SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = upload_size,
    };
    if (g_frame_active) {
        g_frame_stats.transient_buffer_creations++;
    }
    SDL_GPUTransferBuffer *const transfer_buffer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_info);
    if (!transfer_buffer) {
        SDL_DestroySurface(converted);
        SDL_DestroySurface(surface);
        SDL_ReleaseGPUTexture(gpu_device, texture);
        return nullptr;
    }

    Uint8 *const map = (Uint8 *)SDL_MapGPUTransferBuffer(gpu_device, transfer_buffer, true);
    for (int y = 0; y < converted->h; y++) {
        const Uint8 *const src = (const Uint8 *)converted->pixels + converted->pitch * y;
        Uint8 *const dst = map + (Uint32)(y * converted->w * 4);
        SDL_memcpy(dst, src, (size_t)converted->w * 4U);
    }
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer_buffer);

    SDL_GPUCommandBuffer *const upload_cmd = SDL_AcquireGPUCommandBuffer(gpu_device);
    SDL_GPUCopyPass *const copy = SDL_BeginGPUCopyPass(upload_cmd);

    const SDL_GPUTextureTransferInfo src_info = {
        .transfer_buffer = transfer_buffer,
        .offset = 0,
        .pixels_per_row = (Uint32)converted->w,
        .rows_per_layer = (Uint32)converted->h,
    };
    const SDL_GPUTextureRegion dst_info = {
        .texture = texture,
        .w = (Uint32)converted->w,
        .h = (Uint32)converted->h,
        .d = 1,
    };

    SDL_UploadToGPUTexture(copy, &src_info, &dst_info, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(upload_cmd);

    SDL_ReleaseGPUTransferBuffer(gpu_device, transfer_buffer);
    SDL_DestroySurface(converted);
    SDL_DestroySurface(surface);
    return texture;
}

void Renderer_DestroyTexture(SDL_GPUTexture *const texture) {
    if (texture) {
        SDL_ReleaseGPUTexture(gpu_device, texture);
    }
}

void Renderer_BeginFrame(void) {
    renderer_reset_queues();
    renderer_reset_frame_stats();
    frame_queues_flushed = false;
    g_frame_active = true;
    g_frame_cpu_start_ticks = SDL_GetPerformanceCounter();
    g_record_commands_start_ticks = 0;

    cmd_buffer = SDL_AcquireGPUCommandBuffer(gpu_device);
    if (!cmd_buffer) {
        g_frame_active = false;
        return;
    }

    const Uint64 acquire_start = SDL_GetPerformanceCounter();
    const bool got_swapchain =
        SDL_AcquireGPUSwapchainTexture(cmd_buffer, render_window, &swapchain_texture, nullptr, nullptr);
    const Uint64 acquire_end = SDL_GetPerformanceCounter();
    g_frame_stats.timing.acquire_swapchain_ms = renderer_elapsed_ms(acquire_start, acquire_end);

    if (!got_swapchain) {
        SDL_SubmitGPUCommandBuffer(cmd_buffer);
        cmd_buffer = nullptr;
        swapchain_texture = nullptr;
        g_frame_stats.timing.frame_cpu_ms = renderer_elapsed_ms(g_frame_cpu_start_ticks, SDL_GetPerformanceCounter());
        g_frame_active = false;
        return;
    }

    if (!swapchain_texture) {
        SDL_SubmitGPUCommandBuffer(cmd_buffer);
        cmd_buffer = nullptr;
        g_frame_stats.timing.frame_cpu_ms = renderer_elapsed_ms(g_frame_cpu_start_ticks, SDL_GetPerformanceCounter());
        g_frame_active = false;
        return;
    }

    current_frame_slot = (current_frame_slot + 1U) % RENDERER_FRAMES_IN_FLIGHT;

    if (!renderer_stream_begin_frame(&sprite_stream, current_frame_slot) ||
        !renderer_stream_begin_frame(&world_geom_stream, current_frame_slot) ||
        !renderer_stream_begin_frame(&line_stream, current_frame_slot) ||
        !renderer_stream_begin_frame(&ui_geom_stream, current_frame_slot) ||
        !renderer_stream_begin_frame(&ui_text_vert_stream, current_frame_slot) ||
        !renderer_stream_begin_frame(&ui_text_index_stream, current_frame_slot)) {
        renderer_stream_end_frame(&sprite_stream);
        renderer_stream_end_frame(&world_geom_stream);
        renderer_stream_end_frame(&line_stream);
        renderer_stream_end_frame(&ui_geom_stream);
        renderer_stream_end_frame(&ui_text_vert_stream);
        renderer_stream_end_frame(&ui_text_index_stream);
        SDL_SubmitGPUCommandBuffer(cmd_buffer);
        cmd_buffer = nullptr;
        swapchain_texture = nullptr;
        g_frame_stats.timing.frame_cpu_ms = renderer_elapsed_ms(g_frame_cpu_start_ticks, SDL_GetPerformanceCounter());
        g_frame_active = false;
        return;
    }

    g_record_commands_start_ticks = SDL_GetPerformanceCounter();
}

void Renderer_EndFrame(void) {
    if (!cmd_buffer) {
        g_frame_active = false;
        return;
    }

    renderer_flush_queued_draws();
    if (g_record_commands_start_ticks != 0) {
        g_frame_stats.timing.record_commands_ms =
            renderer_elapsed_ms(g_record_commands_start_ticks, SDL_GetPerformanceCounter());
    }

    const Uint64 submit_start = SDL_GetPerformanceCounter();
    SDL_SubmitGPUCommandBuffer(cmd_buffer);
    const Uint64 submit_end = SDL_GetPerformanceCounter();
    g_frame_stats.timing.submit_ms = renderer_elapsed_ms(submit_start, submit_end);
    g_frame_stats.timing.frame_cpu_ms = renderer_elapsed_ms(g_frame_cpu_start_ticks, submit_end);
    cmd_buffer = nullptr;
    swapchain_texture = nullptr;
    g_frame_active = false;
}

void Renderer_SetViewProjection(const float *const viewProjMatrix) {
    if (!viewProjMatrix) {
        return;
    }
    SDL_memcpy(sprite_uniforms.viewProjection, viewProjMatrix, sizeof(float) * 16U);
}

void Renderer_SetWaterParams(const float time, const float speed, const float amplitude, const float phase) {
    sprite_uniforms.waterParams[0] = time;
    sprite_uniforms.waterParams[1] = speed;
    sprite_uniforms.waterParams[2] = amplitude;
    sprite_uniforms.waterParams[3] = phase;
}

void Renderer_DrawSprites(SDL_GPUTexture *const texture, const SpriteInstance *const instances, const int count) {
    if (!texture || !instances || count <= 0 || !cmd_buffer || !swapchain_texture || frame_queues_flushed) {
        return;
    }

    const Uint32 upload_size = (Uint32)(sizeof(SpriteInstance) * (Uint32)count);
    Uint32 byte_offset = 0;
    if (!renderer_stream_write(&sprite_stream,
                               instances,
                               upload_size,
                               SDL_max((Uint32)sizeof(SpriteInstance), RENDERER_STREAM_ALIGN),
                               &byte_offset)) {
        return;
    }

    const Uint32 instance_base = (byte_offset - sprite_stream.slot_base) / (Uint32)sizeof(SpriteInstance);

    SpriteCmd *cmd = nullptr;
    if (sprite_cmd_count > 0) {
        SpriteCmd *last = &sprite_cmds[sprite_cmd_count - 1U];
        if (last->texture == texture && SDL_memcmp(&last->uniforms, &sprite_uniforms, sizeof(SpriteUniforms)) == 0 &&
            last->first_instance + last->instance_count == instance_base) {
            last->instance_count += (Uint32)count;
            cmd = last;
        }
    }

    if (!cmd) {
        if (sprite_cmd_count >= RENDERER_MAX_SPRITE_CMDS) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Sprite command queue overflow");
            return;
        }
        cmd = &sprite_cmds[sprite_cmd_count++];
        cmd->texture = texture;
        cmd->first_instance = instance_base;
        cmd->instance_count = (Uint32)count;
        cmd->uniforms = sprite_uniforms;
    }

    g_frame_stats.queues[RENDERER_STATS_QUEUE_SPRITE].cmd_count = sprite_cmd_count;
    g_frame_stats.instances_submitted += (Uint32)count;
}

void Renderer_DrawLine(const float x1,
                       const float y1,
                       const float z1,
                       const float x2,
                       const float y2,
                       const float z2,
                       const SDL_FColor color) {
    const float vertices[6] = {x1, y1, z1, x2, y2, z2};
    Renderer_DrawLineBatch(vertices, 2, color);
}

void Renderer_DrawLineBatch(const float *const vertices_xyz, const int vertex_count, const SDL_FColor color) {
    if (!vertices_xyz || vertex_count < 2 || (vertex_count & 1) != 0 || !cmd_buffer || !swapchain_texture ||
        frame_queues_flushed || line_cmd_count >= RENDERER_MAX_LINE_CMDS) {
        return;
    }

    const Uint32 upload_size = (Uint32)(sizeof(float) * 3U * (Uint32)vertex_count);
    Uint32 byte_offset = 0;
    if (!renderer_stream_write(&line_stream, vertices_xyz, upload_size, RENDERER_STREAM_ALIGN, &byte_offset)) {
        return;
    }

    LineCmd *cmd = &line_cmds[line_cmd_count++];
    cmd->vertex_offset = byte_offset;
    cmd->vertex_count = (Uint32)vertex_count;
    cmd->color = color;
    SDL_memcpy(cmd->matrix, sprite_uniforms.viewProjection, sizeof(float) * 16U);

    g_frame_stats.queues[RENDERER_STATS_QUEUE_LINE].cmd_count = line_cmd_count;
    g_frame_stats.line_vertices_submitted += (Uint32)vertex_count;
}

void Renderer_DrawGeometry(const SDL_Vertex *const vertices, const int count) {
    if (!vertices || count <= 0 || !cmd_buffer || !swapchain_texture || frame_queues_flushed) {
        return;
    }
    if (world_geom_cmd_count >= RENDERER_MAX_WORLD_GEOM_CMDS) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "World geometry command queue overflow");
        return;
    }

    const Uint32 upload_size = (Uint32)(sizeof(SDL_Vertex) * (Uint32)count);
    Uint32 byte_offset = 0;
    if (!renderer_stream_write(&world_geom_stream, vertices, upload_size, RENDERER_STREAM_ALIGN, &byte_offset)) {
        return;
    }

    GeometryCmd *cmd = &world_geom_cmds[world_geom_cmd_count++];
    cmd->vertex_offset = byte_offset;
    cmd->vertex_count = (Uint32)count;
    SDL_memcpy(cmd->matrix, sprite_uniforms.viewProjection, sizeof(float) * 16U);

    g_frame_stats.queues[RENDERER_STATS_QUEUE_WORLD_GEOMETRY].cmd_count = world_geom_cmd_count;
}

void Renderer_DrawGeometryScreenSpace(const SDL_Vertex *vertices, const int count) {
    Renderer_FlushUIGeometry(vertices, count);
}

TTF_TextEngine *Renderer_GetTextEngine(void) {
    return text_engine;
}

void Renderer_DrawText(TTF_Text *text, const float x, const float y) {
    if (!text || !cmd_buffer || !swapchain_texture || frame_queues_flushed) {
        return;
    }

    const TTF_GPUAtlasDrawSequence *seq = TTF_GetGPUTextDrawData(text);
    while (seq) {
        if (seq->num_vertices <= 0 || seq->num_indices <= 0) {
            seq = seq->next;
            continue;
        }

        const Uint32 vert_bytes = (Uint32)(sizeof(float) * 4U * (Uint32)seq->num_vertices);
        const Uint32 idx_bytes = (Uint32)(sizeof(int) * (Uint32)seq->num_indices);

        Uint32 vert_offset = 0;
        if (!renderer_stream_alloc(&ui_text_vert_stream, vert_bytes, RENDERER_STREAM_ALIGN, &vert_offset)) {
            return;
        }

        float *const dst = (float *)(ui_text_vert_stream.mapped + vert_offset);
        for (int i = 0; i < seq->num_vertices; i++) {
            dst[i * 4 + 0] = seq->xy[i].x + x;
            dst[i * 4 + 1] = -seq->xy[i].y + y;
            dst[i * 4 + 2] = seq->uv[i].x;
            dst[i * 4 + 3] = seq->uv[i].y;
        }

        Uint32 idx_offset = 0;
        if (!renderer_stream_write(
                &ui_text_index_stream, seq->indices, idx_bytes, RENDERER_STREAM_ALIGN, &idx_offset)) {
            return;
        }

        if (ui_text_cmd_count >= RENDERER_MAX_UI_TEXT_CMDS) {
            return;
        }

        UITextCmd *cmd = &ui_text_cmds[ui_text_cmd_count++];
        cmd->vertex_offset = vert_offset;
        cmd->index_offset = idx_offset;
        cmd->vertex_count = (Uint32)seq->num_vertices;
        cmd->index_count = (Uint32)seq->num_indices;
        cmd->range_count = 1;
        cmd->ranges[0] = (UITextRangeCmd){
            .atlas = seq->atlas_texture,
            .start_index = 0,
            .index_count = (Uint32)seq->num_indices,
        };

        seq = seq->next;
    }

    g_frame_stats.queues[RENDERER_STATS_QUEUE_UI_TEXT].cmd_count = ui_text_cmd_count;
}

void Renderer_FlushUIGeometry(const SDL_Vertex *vertices, const int count) {
    if (!vertices || count <= 0 || !cmd_buffer || !swapchain_texture || frame_queues_flushed) {
        return;
    }
    if (ui_geom_cmd_count >= RENDERER_MAX_UI_GEOM_CMDS) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "UI geometry command queue overflow");
        return;
    }

    const Uint32 upload_size = (Uint32)(sizeof(SDL_Vertex) * (Uint32)count);
    Uint32 byte_offset = 0;
    if (!renderer_stream_write(&ui_geom_stream, vertices, upload_size, RENDERER_STREAM_ALIGN, &byte_offset)) {
        return;
    }

    GeometryCmd *cmd = &ui_geom_cmds[ui_geom_cmd_count++];
    cmd->vertex_offset = byte_offset;
    cmd->vertex_count = (Uint32)count;
    SDL_memcpy(cmd->matrix, g_screen_projection, sizeof(float) * 16U);

    g_frame_stats.queues[RENDERER_STATS_QUEUE_UI_GEOMETRY].cmd_count = ui_geom_cmd_count;
}

void Renderer_FlushUIText(const float *const restrict vertices,
                          const int vertex_count,
                          const int *const restrict indices,
                          const int index_count,
                          const UITextAtlasInfo *const restrict atlases,
                          const int atlas_count) {
    if (!vertices || !indices || vertex_count <= 0 || index_count <= 0 || !cmd_buffer || !swapchain_texture) {
        return;
    }
    if (frame_queues_flushed) {
        return;
    }
    if (ui_text_cmd_count >= RENDERER_MAX_UI_TEXT_CMDS) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "UI text command queue overflow");
        return;
    }

    const Uint32 vert_bytes = (Uint32)(sizeof(float) * 4U * (Uint32)vertex_count);
    const Uint32 idx_bytes = (Uint32)(sizeof(int) * (Uint32)index_count);

    Uint32 vert_offset = 0;
    Uint32 idx_offset = 0;

    if (!renderer_stream_write(&ui_text_vert_stream, vertices, vert_bytes, RENDERER_STREAM_ALIGN, &vert_offset)) {
        return;
    }
    if (!renderer_stream_write(&ui_text_index_stream, indices, idx_bytes, RENDERER_STREAM_ALIGN, &idx_offset)) {
        return;
    }

    UITextCmd *cmd = &ui_text_cmds[ui_text_cmd_count++];
    cmd->vertex_offset = vert_offset;
    cmd->index_offset = idx_offset;
    cmd->vertex_count = (Uint32)vertex_count;
    cmd->index_count = (Uint32)index_count;

    Uint32 ranges_written = 0;
    for (int i = 0; i < atlas_count && ranges_written < RENDERER_MAX_UI_TEXT_RANGES; i++) {
        if (!atlases[i].atlas || atlases[i].index_count <= 0) {
            continue;
        }

        cmd->ranges[ranges_written++] = (UITextRangeCmd){
            .atlas = atlases[i].atlas,
            .start_index = (Uint32)atlases[i].start_index,
            .index_count = (Uint32)atlases[i].index_count,
        };
    }
    cmd->range_count = ranges_written;

    g_frame_stats.queues[RENDERER_STATS_QUEUE_UI_TEXT].cmd_count = ui_text_cmd_count;
}

void Renderer_DrawTextureDebug(
    SDL_GPUTexture *texture, const float x, const float y, const float width, const float height) {
    if (!texture || width <= 0.0f || height <= 0.0f) {
        return;
    }

    const float vertices[16] = {
        x,
        y,
        0.0f,
        0.0f,
        x + width,
        y,
        1.0f,
        0.0f,
        x + width,
        y + height,
        1.0f,
        1.0f,
        x,
        y + height,
        0.0f,
        1.0f,
    };
    const int indices[6] = {0, 1, 2, 0, 2, 3};
    const UITextAtlasInfo atlas = {
        .atlas = texture,
        .start_index = 0,
        .index_count = 6,
    };
    Renderer_FlushUIText(vertices, 4, indices, 6, &atlas, 1);
}

void Renderer_DrawFilledQuadDebug(
    const float x, const float y, const float width, const float height, const SDL_FColor color) {
    const SDL_Vertex vertices[6] = {
        {{x, y}, color, {0, 0}},
        {{x + width, y}, color, {1, 0}},
        {{x, y + height}, color, {0, 1}},
        {{x + width, y}, color, {1, 0}},
        {{x + width, y + height}, color, {1, 1}},
        {{x, y + height}, color, {0, 1}},
    };
    Renderer_FlushUIGeometry(vertices, 6);
}

const RendererFrameStats *Renderer_GetFrameStats(void) {
    return &g_frame_stats;
}

SDL_Window *Renderer_GetWindow(void) {
    return render_window;
}

SDL_GPUDevice *Renderer_GetDevice(void) {
    return gpu_device;
}

SDL_GPUCommandBuffer *Renderer_GetCommandBuffer(void) {
    return cmd_buffer;
}

SDL_GPUTexture *Renderer_GetSwapchainTexture(void) {
    return swapchain_texture;
}

void Renderer_EndRenderPass(void) {
    renderer_flush_queued_draws();
}

void Renderer_ResumeRenderPass(void) {
    // No-op with queued renderer architecture.
}
