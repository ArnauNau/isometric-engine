/* nuklear_sdl3_gpu.h - SDL3 GPU backend for Nuklear
 *
 * This backend uses SDL3's GPU API (Metal/Vulkan/D3D12) for rendering.
 * Designed for miso but structured for potential future extraction.
 *
 * Usage:
 *   // First include nuklear with your configuration
 *   #define NK_INCLUDE_...
 *   #define NK_IMPLEMENTATION
 *   #include "nuklear.h"
 *
 *   // Then include this backend
 *   #define NK_SDL3_GPU_IMPLEMENTATION
 *   #include "nuklear_sdl3_gpu.h"
 *
 * Requirements (define before nuklear.h):
 *   - NK_INCLUDE_COMMAND_USERDATA
 *   - NK_INCLUDE_VERTEX_BUFFER_OUTPUT
 *   - NK_INCLUDE_FONT_BAKING (optional, for font atlas)
 */

#ifndef NK_SDL3_GPU_H_
#define NK_SDL3_GPU_H_

#include "renderer.h"

#include <SDL3/SDL.h>

/* Nuklear must be included before this header */
#ifndef NK_NUKLEAR_H_
#error "nuklear_sdl3_gpu.h requires nuklear.h to be included first"
#endif
#if SDL_MAJOR_VERSION < 3
#error "nuklear_sdl3_gpu requires SDL 3.0.0 or later"
#endif
#ifndef NK_INCLUDE_COMMAND_USERDATA
#error "nuklear_sdl3_gpu requires NK_INCLUDE_COMMAND_USERDATA"
#endif
#ifndef NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#error "nuklear_sdl3_gpu requires NK_INCLUDE_VERTEX_BUFFER_OUTPUT"
#endif

#ifndef NK_BUFFER_DEFAULT_INITIAL_SIZE
#define NK_BUFFER_DEFAULT_INITIAL_SIZE (4 * 1024)
#endif

/* API */
NK_API struct nk_context *nk_sdl_gpu_init(SDL_Window *win, SDL_GPUDevice *device);
#ifdef NK_INCLUDE_FONT_BAKING
NK_API struct nk_font_atlas *nk_sdl_gpu_font_stash_begin(const struct nk_context *ctx);
NK_API void nk_sdl_gpu_font_stash_end(struct nk_context *ctx);
#endif
NK_API int nk_sdl_gpu_handle_event(struct nk_context *ctx, const SDL_Event *evt);
NK_API void nk_sdl_gpu_render(struct nk_context *ctx,
                              SDL_GPUCommandBuffer *cmd,
                              SDL_GPUTexture *swapchain,
                              Uint32 swapchain_width,
                              Uint32 swapchain_height,
                              enum nk_anti_aliasing AA);
NK_API void nk_sdl_gpu_shutdown(struct nk_context *ctx);

#endif /* NK_SDL3_GPU_H_ */

/*
 * ==============================================================
 *
 *                          IMPLEMENTATION
 *
 * ===============================================================
 */
#ifdef NK_SDL3_GPU_IMPLEMENTATION
#ifndef NK_SDL3_GPU_IMPLEMENTATION_ONCE
#define NK_SDL3_GPU_IMPLEMENTATION_ONCE

#ifndef NK_SDL_DOUBLE_CLICK_LO
#define NK_SDL_DOUBLE_CLICK_LO 0.02
#endif
#ifndef NK_SDL_DOUBLE_CLICK_HI
#define NK_SDL_DOUBLE_CLICK_HI 0.2
#endif

#define NK_SDL_GPU_FRAMES_IN_FLIGHT 3U
#define NK_SDL_GPU_STREAM_ALIGN 16U
#define NK_SDL_GPU_VERTEX_SLOT_BYTES (sizeof(struct nk_sdl_gpu_vertex) * 131072U)
#define NK_SDL_GPU_INDEX_SLOT_BYTES (sizeof(nk_draw_index) * 524288U)

struct nk_sdl_gpu_vertex {
    float position[2];
    float uv[2];
    float col[4];
};

struct nk_sdl_gpu_device {
    struct nk_buffer cmds;
    struct nk_draw_null_texture tex_null;
    SDL_GPUTexture *font_tex;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUSampler *sampler;
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUBuffer *index_buffer;
    SDL_GPUTransferBuffer *vertex_transfer;
    SDL_GPUTransferBuffer *index_transfer;
    Uint32 vertex_slot_size;
    Uint32 index_slot_size;
    Uint32 frame_slot;
    bool stream_capacity_warned;
};

struct nk_sdl_gpu {
    SDL_Window *win;
    SDL_GPUDevice *device;
    struct nk_sdl_gpu_device gpu;
    struct nk_context ctx;
#ifdef NK_INCLUDE_FONT_BAKING
    struct nk_font_atlas atlas;
#endif
    struct nk_allocator allocator;
    Uint64 last_left_click;
    Uint64 last_render;
    bool insert_toggle;
    bool edit_was_active;
};

/* Memory allocation using SDL */
NK_INTERN void *nk_sdl_gpu_alloc(nk_handle user, void *old, nk_size size) {
    NK_UNUSED(user);
    NK_UNUSED(old);
    return SDL_malloc(size);
}

NK_INTERN void nk_sdl_gpu_free(const nk_handle user, void *const old) {
    NK_UNUSED(user);
    SDL_free(old);
}

NK_INTERN Uint32 nk_sdl_gpu_align_up(const Uint32 value, const Uint32 align) {
    const Uint32 mask = align - 1U;
    return (value + mask) & ~mask;
}

NK_INTERN void nk_sdl_gpu_destroy_stream_buffers(struct nk_sdl_gpu *const sdl) {
    if (sdl->gpu.vertex_transfer) {
        SDL_ReleaseGPUTransferBuffer(sdl->device, sdl->gpu.vertex_transfer);
        sdl->gpu.vertex_transfer = nullptr;
    }
    if (sdl->gpu.index_transfer) {
        SDL_ReleaseGPUTransferBuffer(sdl->device, sdl->gpu.index_transfer);
        sdl->gpu.index_transfer = nullptr;
    }
    if (sdl->gpu.vertex_buffer) {
        SDL_ReleaseGPUBuffer(sdl->device, sdl->gpu.vertex_buffer);
        sdl->gpu.vertex_buffer = nullptr;
    }
    if (sdl->gpu.index_buffer) {
        SDL_ReleaseGPUBuffer(sdl->device, sdl->gpu.index_buffer);
        sdl->gpu.index_buffer = nullptr;
    }
}

NK_INTERN bool nk_sdl_gpu_create_stream_buffers(struct nk_sdl_gpu *const sdl) {
    const Uint32 vertex_slot_size = nk_sdl_gpu_align_up(NK_SDL_GPU_VERTEX_SLOT_BYTES, NK_SDL_GPU_STREAM_ALIGN);
    const Uint32 index_slot_size = nk_sdl_gpu_align_up(NK_SDL_GPU_INDEX_SLOT_BYTES, NK_SDL_GPU_STREAM_ALIGN);
    const Uint32 vertex_total_size = vertex_slot_size * NK_SDL_GPU_FRAMES_IN_FLIGHT;
    const Uint32 index_total_size = index_slot_size * NK_SDL_GPU_FRAMES_IN_FLIGHT;

    sdl->gpu.vertex_slot_size = vertex_slot_size;
    sdl->gpu.index_slot_size = index_slot_size;
    sdl->gpu.frame_slot = 0;
    sdl->gpu.stream_capacity_warned = false;

    const SDL_GPUBufferCreateInfo vb_info = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = vertex_total_size};
    const SDL_GPUBufferCreateInfo ib_info = {.usage = SDL_GPU_BUFFERUSAGE_INDEX, .size = index_total_size};
    sdl->gpu.vertex_buffer = SDL_CreateGPUBuffer(sdl->device, &vb_info);
    sdl->gpu.index_buffer = SDL_CreateGPUBuffer(sdl->device, &ib_info);
    if (!sdl->gpu.vertex_buffer || !sdl->gpu.index_buffer) {
        SDL_Log("nuklear: Failed to create persistent GPU stream buffers");
        nk_sdl_gpu_destroy_stream_buffers(sdl);
        return false;
    }

    const SDL_GPUTransferBufferCreateInfo vtx_transfer_info = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                               .size = vertex_total_size};
    const SDL_GPUTransferBufferCreateInfo idx_transfer_info = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                               .size = index_total_size};
    sdl->gpu.vertex_transfer = SDL_CreateGPUTransferBuffer(sdl->device, &vtx_transfer_info);
    sdl->gpu.index_transfer = SDL_CreateGPUTransferBuffer(sdl->device, &idx_transfer_info);
    if (!sdl->gpu.vertex_transfer || !sdl->gpu.index_transfer) {
        SDL_Log("nuklear: Failed to create persistent transfer stream buffers");
        nk_sdl_gpu_destroy_stream_buffers(sdl);
        return false;
    }

    return true;
}

/* Shader loading helper */
NK_INTERN SDL_GPUShader *nk_sdl_gpu_load_shader(SDL_GPUDevice *const device,
                                                const char *const path,
                                                const char *const entrypoint,
                                                const int num_samplers,
                                                const int num_uniform_buffers,
                                                const SDL_GPUShaderStage stage) {
    size_t code_size;
    void *const code = SDL_LoadFile(path, &code_size);
    if (!code) {
        SDL_Log("nuklear: Failed to load shader %s: %s", path, SDL_GetError());
        return nullptr;
    }

    const SDL_GPUShaderCreateInfo info = {.code_size = code_size,
                                          .code = (const Uint8 *)code,
                                          .entrypoint = entrypoint,
                                          .format = SDL_GPU_SHADERFORMAT_MSL,
                                          .stage = stage,
                                          .num_samplers = (Uint32)num_samplers,
                                          .num_uniform_buffers = (Uint32)num_uniform_buffers,
                                          .num_storage_buffers = 0,
                                          .num_storage_textures = 0};

    SDL_GPUShader *const shader = SDL_CreateGPUShader(device, &info);
    SDL_free(code);
    return shader;
}

/* Upload font atlas to GPU texture */
NK_INTERN void nk_sdl_gpu_upload_atlas(const struct nk_context *const ctx,
                                       const void *const image,
                                       const int width,
                                       const int height) {
    struct nk_sdl_gpu *const sdl = (struct nk_sdl_gpu *)ctx->userdata.ptr;
    NK_ASSERT(sdl);

    /* Clean up existing texture */
    if (sdl->gpu.font_tex) {
        SDL_ReleaseGPUTexture(sdl->device, sdl->gpu.font_tex);
        sdl->gpu.font_tex = nullptr;
    }

    /* Create texture */
    const SDL_GPUTextureCreateInfo tex_info = {.type = SDL_GPU_TEXTURETYPE_2D,
                                               .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                               .width = (Uint32)width,
                                               .height = (Uint32)height,
                                               .layer_count_or_depth = 1,
                                               .num_levels = 1,
                                               .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER};
    sdl->gpu.font_tex = SDL_CreateGPUTexture(sdl->device, &tex_info);
    if (!sdl->gpu.font_tex) {
        SDL_Log("nuklear: Failed to create font texture");
        return;
    }

    /* Upload via transfer buffer */
    const Uint32 data_size = (Uint32)(width * height * 4);
    const SDL_GPUTransferBufferCreateInfo tb_info = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = data_size};
    SDL_GPUTransferBuffer *const tb = SDL_CreateGPUTransferBuffer(sdl->device, &tb_info);
    if (!tb) {
        SDL_Log("nuklear: Failed to create transfer buffer");
        return;
    }

    void *const map = SDL_MapGPUTransferBuffer(sdl->device, tb, false);
    SDL_memcpy(map, image, data_size);
    SDL_UnmapGPUTransferBuffer(sdl->device, tb);

    /* Copy to texture */
    SDL_GPUCommandBuffer *const cmd = SDL_AcquireGPUCommandBuffer(sdl->device);
    SDL_GPUCopyPass *const copy = SDL_BeginGPUCopyPass(cmd);

    const SDL_GPUTextureTransferInfo src = {
        .transfer_buffer = tb, .offset = 0, .pixels_per_row = (Uint32)width, .rows_per_layer = (Uint32)height};
    const SDL_GPUTextureRegion dst = {.texture = sdl->gpu.font_tex, .w = (Uint32)width, .h = (Uint32)height, .d = 1};
    SDL_UploadToGPUTexture(copy, &src, &dst, false);

    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(sdl->device, tb);
}

/* Clipboard */
NK_INTERN void nk_sdl_gpu_clipboard_paste(const nk_handle usr, struct nk_text_edit *const edit) {
    NK_UNUSED(usr);
    char *const text = SDL_GetClipboardText();
    if (text && text[0] != '\0') {
        const int len = (int)SDL_utf8strlen(text);
        nk_textedit_paste(edit, text, len);
    }
    SDL_free(text);
}

NK_INTERN void nk_sdl_gpu_clipboard_copy(const nk_handle usr, const char *const text, const int len) {
    const struct nk_sdl_gpu *const sdl = (struct nk_sdl_gpu *)usr.ptr;
    if (len <= 0 || !text)
        return;

    const char *ptext = text;
    for (int i = len; i > 0; i--)
        (void)SDL_StepUTF8(&ptext, nullptr);
    const size_t buflen = (size_t)(ptext - text) + 1;

    char *str = sdl->allocator.alloc(sdl->allocator.userdata, nullptr, buflen);
    if (!str)
        return;
    SDL_strlcpy(str, text, buflen);
    SDL_SetClipboardText(str);
    sdl->allocator.free(sdl->allocator.userdata, str);
}

NK_API struct nk_context *nk_sdl_gpu_init(SDL_Window *const win, SDL_GPUDevice *const device) {
    NK_ASSERT(win);
    NK_ASSERT(device);

    /* Allocate context */
    struct nk_sdl_gpu *const sdl = SDL_calloc(1, sizeof(struct nk_sdl_gpu));
    if (!sdl)
        return nullptr;

    sdl->win = win;
    sdl->device = device;
    sdl->allocator.userdata.ptr = NULL;
    sdl->allocator.alloc = nk_sdl_gpu_alloc;
    sdl->allocator.free = nk_sdl_gpu_free;

    /* Initialize Nuklear */
    nk_init(&sdl->ctx, &sdl->allocator, nullptr);
    sdl->ctx.userdata = nk_handle_ptr(sdl);
    sdl->ctx.clip.copy = nk_sdl_gpu_clipboard_copy;
    sdl->ctx.clip.paste = nk_sdl_gpu_clipboard_paste;
    sdl->ctx.clip.userdata = nk_handle_ptr(sdl);
    nk_buffer_init(&sdl->gpu.cmds, &sdl->allocator, NK_BUFFER_DEFAULT_INITIAL_SIZE);

    /* Load shaders */
    const char *const shader_path = Renderer_GetNuklearShaderPath();
    if (!shader_path) {
        SDL_Log("nuklear: Shader path not configured");
        SDL_free(sdl);
        return nullptr;
    }

    SDL_GPUShader *const vs =
        nk_sdl_gpu_load_shader(device, shader_path, "vertex_nuklear", 0, 1, SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *const fs =
        nk_sdl_gpu_load_shader(device, shader_path, "fragment_nuklear", 1, 0, SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!vs || !fs) {
        SDL_Log("nuklear: Failed to load shaders");
        if (vs)
            SDL_ReleaseGPUShader(device, vs);
        if (fs)
            SDL_ReleaseGPUShader(device, fs);
        SDL_free(sdl);
        return nullptr;
    }

    /* Create pipeline */
    const SDL_GPUVertexAttribute attrs[] = {{.location = 0,
                                             .buffer_slot = 0,
                                             .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                                             .offset = NK_OFFSETOF(struct nk_sdl_gpu_vertex, position)},
                                            {.location = 1,
                                             .buffer_slot = 0,
                                             .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                                             .offset = NK_OFFSETOF(struct nk_sdl_gpu_vertex, uv)},
                                            {.location = 2,
                                             .buffer_slot = 0,
                                             .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
                                             .offset = NK_OFFSETOF(struct nk_sdl_gpu_vertex, col)}};

    const SDL_GPUVertexBufferDescription vb_desc = {.slot = 0,
                                                    .pitch = sizeof(struct nk_sdl_gpu_vertex),
                                                    .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
                                                    .instance_step_rate = 0};

    const SDL_GPUColorTargetDescription color_desc = {
        .format = SDL_GetGPUSwapchainTextureFormat(device, win),
        .blend_state = {.enable_blend = true,
                        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                        .color_blend_op = SDL_GPU_BLENDOP_ADD,
                        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                        .alpha_blend_op = SDL_GPU_BLENDOP_ADD}};

    const SDL_GPUGraphicsPipelineCreateInfo pipe_info = {.vertex_shader = vs,
                                                         .fragment_shader = fs,
                                                         .vertex_input_state = {.num_vertex_attributes = 3,
                                                                                .vertex_attributes = attrs,
                                                                                .num_vertex_buffers = 1,
                                                                                .vertex_buffer_descriptions = &vb_desc},
                                                         .target_info = {.num_color_targets = 1,
                                                                         .color_target_descriptions = &color_desc,
                                                                         .has_depth_stencil_target = false},
                                                         .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
                                                         .rasterizer_state = {.cull_mode = SDL_GPU_CULLMODE_NONE},
                                                         .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1}};

    sdl->gpu.pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipe_info);
    SDL_ReleaseGPUShader(device, vs);
    SDL_ReleaseGPUShader(device, fs);

    if (!sdl->gpu.pipeline) {
        SDL_Log("nuklear: Failed to create pipeline: %s", SDL_GetError());
        SDL_free(sdl);
        return nullptr;
    }

    /* Create sampler (linear filtering for smooth text) */
    const SDL_GPUSamplerCreateInfo smp_info = {.min_filter = SDL_GPU_FILTER_LINEAR,
                                               .mag_filter = SDL_GPU_FILTER_LINEAR,
                                               .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
                                               .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
                                               .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE};
    sdl->gpu.sampler = SDL_CreateGPUSampler(device, &smp_info);
    if (!sdl->gpu.sampler) {
        SDL_Log("nuklear: Failed to create sampler");
        SDL_ReleaseGPUGraphicsPipeline(device, sdl->gpu.pipeline);
        nk_buffer_free(&sdl->gpu.cmds);
        nk_free(&sdl->ctx);
        SDL_free(sdl);
        return nullptr;
    }

    if (!nk_sdl_gpu_create_stream_buffers(sdl)) {
        SDL_ReleaseGPUSampler(device, sdl->gpu.sampler);
        SDL_ReleaseGPUGraphicsPipeline(device, sdl->gpu.pipeline);
        nk_buffer_free(&sdl->gpu.cmds);
        nk_free(&sdl->ctx);
        SDL_free(sdl);
        return nullptr;
    }

    return &sdl->ctx;
}

#ifdef NK_INCLUDE_FONT_BAKING
NK_API struct nk_font_atlas *nk_sdl_gpu_font_stash_begin(const struct nk_context *const ctx) {
    struct nk_sdl_gpu *const sdl = (struct nk_sdl_gpu *)ctx->userdata.ptr;
    NK_ASSERT(sdl);
    nk_font_atlas_init(&sdl->atlas, &sdl->allocator);
    nk_font_atlas_begin(&sdl->atlas);
    return &sdl->atlas;
}

NK_API void nk_sdl_gpu_font_stash_end(struct nk_context *const ctx) {
    struct nk_sdl_gpu *const sdl = (struct nk_sdl_gpu *)ctx->userdata.ptr;
    NK_ASSERT(sdl);

    int w, h;
    const void *const image = nk_font_atlas_bake(&sdl->atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
    nk_sdl_gpu_upload_atlas(ctx, image, w, h);
    nk_font_atlas_end(&sdl->atlas, nk_handle_ptr(sdl->gpu.font_tex), &sdl->gpu.tex_null);

    if (sdl->atlas.default_font) {
        nk_style_set_font(ctx, &sdl->atlas.default_font->handle);
    }
}
#endif

NK_API int nk_sdl_gpu_handle_event(struct nk_context *const ctx, const SDL_Event *const evt) {
    struct nk_sdl_gpu *const sdl = (struct nk_sdl_gpu *)ctx->userdata.ptr;
    NK_ASSERT(sdl);

    if (sdl->win != SDL_GetWindowFromEvent(evt))
        return 0;

    /* Get pixel density for HDPI scaling */
    const float scale = SDL_GetWindowPixelDensity(sdl->win);

    switch (evt->type) {
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_KEY_DOWN: {
        /* Only consume keyboard if a Nuklear widget is active */
        if (!nk_item_is_any_active(ctx))
            return 0;

        const int down = evt->type == SDL_EVENT_KEY_DOWN;
        const int ctrl = evt->key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL);

        switch (evt->key.scancode) {
        case SDL_SCANCODE_RSHIFT:
        case SDL_SCANCODE_LSHIFT:
            nk_input_key(ctx, NK_KEY_SHIFT, down);
            break;
        case SDL_SCANCODE_DELETE:
            nk_input_key(ctx, NK_KEY_DEL, down);
            break;
        case SDL_SCANCODE_RETURN:
            nk_input_key(ctx, NK_KEY_ENTER, down);
            break;
        case SDL_SCANCODE_TAB:
            nk_input_key(ctx, NK_KEY_TAB, down);
            break;
        case SDL_SCANCODE_BACKSPACE:
            nk_input_key(ctx, NK_KEY_BACKSPACE, down);
            break;
        case SDL_SCANCODE_HOME:
            nk_input_key(ctx, NK_KEY_TEXT_START, down);
            nk_input_key(ctx, NK_KEY_SCROLL_START, down);
            break;
        case SDL_SCANCODE_END:
            nk_input_key(ctx, NK_KEY_TEXT_END, down);
            nk_input_key(ctx, NK_KEY_SCROLL_END, down);
            break;
        case SDL_SCANCODE_PAGEDOWN:
            nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down);
            break;
        case SDL_SCANCODE_PAGEUP:
            nk_input_key(ctx, NK_KEY_SCROLL_UP, down);
            break;
        case SDL_SCANCODE_A:
            nk_input_key(ctx, NK_KEY_TEXT_SELECT_ALL, down && ctrl);
            break;
        case SDL_SCANCODE_Z:
            nk_input_key(ctx, NK_KEY_TEXT_UNDO, down && ctrl);
            break;
        case SDL_SCANCODE_R:
            nk_input_key(ctx, NK_KEY_TEXT_REDO, down && ctrl);
            break;
        case SDL_SCANCODE_C:
            nk_input_key(ctx, NK_KEY_COPY, down && ctrl);
            break;
        case SDL_SCANCODE_V:
            nk_input_key(ctx, NK_KEY_PASTE, down && ctrl);
            break;
        case SDL_SCANCODE_X:
            nk_input_key(ctx, NK_KEY_CUT, down && ctrl);
            break;
        case SDL_SCANCODE_B:
            nk_input_key(ctx, NK_KEY_TEXT_LINE_START, down && ctrl);
            break;
        case SDL_SCANCODE_E:
            nk_input_key(ctx, NK_KEY_TEXT_LINE_END, down && ctrl);
            break;
        case SDL_SCANCODE_UP:
            nk_input_key(ctx, NK_KEY_UP, down);
            break;
        case SDL_SCANCODE_DOWN:
            nk_input_key(ctx, NK_KEY_DOWN, down);
            break;
        case SDL_SCANCODE_ESCAPE:
            nk_input_key(ctx, NK_KEY_TEXT_RESET_MODE, down);
            break;
        case SDL_SCANCODE_INSERT:
            if (down)
                sdl->insert_toggle = !sdl->insert_toggle;
            nk_input_key(ctx, sdl->insert_toggle ? NK_KEY_TEXT_INSERT_MODE : NK_KEY_TEXT_REPLACE_MODE, down);
            break;
        case SDL_SCANCODE_LEFT:
            nk_input_key(ctx, ctrl ? NK_KEY_TEXT_WORD_LEFT : NK_KEY_LEFT, down);
            break;
        case SDL_SCANCODE_RIGHT:
            nk_input_key(ctx, ctrl ? NK_KEY_TEXT_WORD_RIGHT : NK_KEY_RIGHT, down);
            break;
        default:
            return 0;
        }
        return 1;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        /* Scale coordinates for HDPI */
        const int x = (int)(evt->button.x * scale);
        const int y = (int)(evt->button.y * scale);
        const int down = evt->button.down;
        const double dt = (double)(evt->button.timestamp - sdl->last_left_click) / 1000000000.0;

        switch (evt->button.button) {
        case SDL_BUTTON_LEFT:
            nk_input_button(ctx, NK_BUTTON_LEFT, x, y, down);
            nk_input_button(
                ctx, NK_BUTTON_DOUBLE, x, y, down && dt > NK_SDL_DOUBLE_CLICK_LO && dt < NK_SDL_DOUBLE_CLICK_HI);
            sdl->last_left_click = evt->button.timestamp;
            break;
        case SDL_BUTTON_MIDDLE:
            nk_input_button(ctx, NK_BUTTON_MIDDLE, x, y, down);
            break;
        case SDL_BUTTON_RIGHT:
            nk_input_button(ctx, NK_BUTTON_RIGHT, x, y, down);
            break;
        default:
            return 0;
        }
        /* Only consume if over a Nuklear window */
        return nk_window_is_any_hovered(ctx) ? 1 : 0;
    }

    case SDL_EVENT_MOUSE_MOTION: {
        /* Scale coordinates for HDPI */
        const float mx = evt->motion.x * scale;
        const float my = evt->motion.y * scale;
        ctx->input.mouse.pos.x = mx;
        ctx->input.mouse.pos.y = my;
        ctx->input.mouse.delta.x = ctx->input.mouse.pos.x - ctx->input.mouse.prev.x;
        ctx->input.mouse.delta.y = ctx->input.mouse.pos.y - ctx->input.mouse.prev.y;
        /* Don't consume - let game also track mouse position */
        return 0;
    }

    case SDL_EVENT_TEXT_INPUT: {
        if (!nk_item_is_any_active(ctx))
            return 0;
        nk_glyph glyph;
        const size_t len = SDL_strlen(evt->text.text);
        if (len <= NK_UTF_SIZE) {
            NK_MEMCPY(glyph, evt->text.text, len);
            nk_input_glyph(ctx, glyph);
        }
        return 1;
    }

    case SDL_EVENT_MOUSE_WHEEL:
        nk_input_scroll(ctx, nk_vec2(evt->wheel.x, evt->wheel.y));
        /* Only consume if over a Nuklear window */
        return nk_window_is_any_hovered(ctx) ? 1 : 0;
    }

    return 0;
}

NK_API void nk_sdl_gpu_render(struct nk_context *const ctx,
                              SDL_GPUCommandBuffer *const cmd,
                              SDL_GPUTexture *const swapchain,
                              const Uint32 swapchain_width,
                              const Uint32 swapchain_height,
                              enum nk_anti_aliasing AA) {
    struct nk_sdl_gpu *const sdl = (struct nk_sdl_gpu *)ctx->userdata.ptr;
    NK_ASSERT(sdl);

    /* Update delta time */
    const Uint64 ticks = SDL_GetTicks();
    ctx->delta_time_seconds = (float)(ticks - sdl->last_render) / 1000.0f;
    sdl->last_render = ticks;

    /* Convert commands to vertex/index buffers */
    static const struct nk_draw_vertex_layout_element vertex_layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct nk_sdl_gpu_vertex, position)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct nk_sdl_gpu_vertex, uv)},
        {NK_VERTEX_COLOR, NK_FORMAT_R32G32B32A32_FLOAT, NK_OFFSETOF(struct nk_sdl_gpu_vertex, col)},
        {NK_VERTEX_LAYOUT_END}};

    struct nk_convert_config config = {.vertex_layout = vertex_layout,
                                       .vertex_size = sizeof(struct nk_sdl_gpu_vertex),
                                       .vertex_alignment = NK_ALIGNOF(struct nk_sdl_gpu_vertex),
                                       .tex_null = sdl->gpu.tex_null,
                                       .circle_segment_count = 22,
                                       .curve_segment_count = 22,
                                       .arc_segment_count = 22,
                                       .global_alpha = 1.0f,
                                       .shape_AA = AA,
                                       .line_AA = AA};

    struct nk_buffer vbuf, ebuf;
    nk_buffer_init(&vbuf, &sdl->allocator, NK_BUFFER_DEFAULT_INITIAL_SIZE);
    nk_buffer_init(&ebuf, &sdl->allocator, NK_BUFFER_DEFAULT_INITIAL_SIZE);
    nk_convert(ctx, &sdl->gpu.cmds, &vbuf, &ebuf, &config);

    /* Get buffer sizes */
    const Uint32 vert_size = (Uint32)vbuf.needed;
    const Uint32 idx_size = (Uint32)ebuf.needed;

    if (vert_size == 0 || idx_size == 0) {
        nk_buffer_free(&vbuf);
        nk_buffer_free(&ebuf);
        nk_clear(ctx);
        nk_buffer_clear(&sdl->gpu.cmds);
        return;
    }

    if (!cmd || !swapchain || swapchain_width == 0U || swapchain_height == 0U) {
        nk_buffer_free(&vbuf);
        nk_buffer_free(&ebuf);
        nk_clear(ctx);
        nk_buffer_clear(&sdl->gpu.cmds);
        return;
    }

    if (vert_size > sdl->gpu.vertex_slot_size || idx_size > sdl->gpu.index_slot_size) {
        if (!sdl->gpu.stream_capacity_warned) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                        "nuklear: UI stream exhausted (vert=%u/%u bytes, idx=%u/%u bytes)",
                        vert_size,
                        sdl->gpu.vertex_slot_size,
                        idx_size,
                        sdl->gpu.index_slot_size);
            sdl->gpu.stream_capacity_warned = true;
        }
        nk_buffer_free(&vbuf);
        nk_buffer_free(&ebuf);
        nk_clear(ctx);
        nk_buffer_clear(&sdl->gpu.cmds);
        return;
    }
    sdl->gpu.stream_capacity_warned = false;

    sdl->gpu.frame_slot = (sdl->gpu.frame_slot + 1U) % NK_SDL_GPU_FRAMES_IN_FLIGHT;
    const Uint32 vertex_offset = sdl->gpu.frame_slot * sdl->gpu.vertex_slot_size;
    const Uint32 index_offset_bytes = sdl->gpu.frame_slot * sdl->gpu.index_slot_size;

    Uint8 *const vertex_map = (Uint8 *)SDL_MapGPUTransferBuffer(sdl->device, sdl->gpu.vertex_transfer, true);
    Uint8 *const index_map = (Uint8 *)SDL_MapGPUTransferBuffer(sdl->device, sdl->gpu.index_transfer, true);
    if (!vertex_map || !index_map) {
        if (vertex_map) {
            SDL_UnmapGPUTransferBuffer(sdl->device, sdl->gpu.vertex_transfer);
        }
        if (index_map) {
            SDL_UnmapGPUTransferBuffer(sdl->device, sdl->gpu.index_transfer);
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "nuklear: failed to map transfer streams");
        nk_buffer_free(&vbuf);
        nk_buffer_free(&ebuf);
        nk_clear(ctx);
        nk_buffer_clear(&sdl->gpu.cmds);
        return;
    }

    SDL_memcpy(vertex_map + vertex_offset, nk_buffer_memory_const(&vbuf), vert_size);
    SDL_memcpy(index_map + index_offset_bytes, nk_buffer_memory_const(&ebuf), idx_size);

    SDL_UnmapGPUTransferBuffer(sdl->device, sdl->gpu.vertex_transfer);
    SDL_UnmapGPUTransferBuffer(sdl->device, sdl->gpu.index_transfer);

    SDL_GPUCopyPass *const copy = SDL_BeginGPUCopyPass(cmd);
    const SDL_GPUTransferBufferLocation src_v = {.transfer_buffer = sdl->gpu.vertex_transfer, .offset = vertex_offset};
    const SDL_GPUBufferRegion dst_v = {.buffer = sdl->gpu.vertex_buffer, .offset = vertex_offset, .size = vert_size};
    SDL_UploadToGPUBuffer(copy, &src_v, &dst_v, false);

    const SDL_GPUTransferBufferLocation src_i = {.transfer_buffer = sdl->gpu.index_transfer,
                                                 .offset = index_offset_bytes};
    const SDL_GPUBufferRegion dst_i = {.buffer = sdl->gpu.index_buffer, .offset = index_offset_bytes, .size = idx_size};
    SDL_UploadToGPUBuffer(copy, &src_i, &dst_i, false);
    SDL_EndGPUCopyPass(copy);

    /* Setup projection matrix */
    const Uint32 target_w = swapchain_width;
    const Uint32 target_h = swapchain_height;
    const float projection[16] = {2.0f / (float)target_w,
                                  0.0f,
                                  0.0f,
                                  0.0f,
                                  0.0f,
                                  -2.0f / (float)target_h,
                                  0.0f,
                                  0.0f,
                                  0.0f,
                                  0.0f,
                                  1.0f,
                                  0.0f,
                                  -1.0f,
                                  1.0f,
                                  0.0f,
                                  1.0f};

    /* Begin render pass */
    const SDL_GPUColorTargetInfo color_target = {
        .texture = swapchain, .load_op = SDL_GPU_LOADOP_LOAD, .store_op = SDL_GPU_STOREOP_STORE};
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
    const SDL_GPUViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .w = (float)target_w,
        .h = (float)target_h,
        .min_depth = 0.0f,
        .max_depth = 1.0f,
    };
    SDL_SetGPUViewport(pass, &viewport);

    SDL_BindGPUGraphicsPipeline(pass, sdl->gpu.pipeline);
    SDL_BindGPUVertexBuffers(
        pass, 0, &(SDL_GPUBufferBinding){.buffer = sdl->gpu.vertex_buffer, .offset = vertex_offset}, 1);
    SDL_BindGPUIndexBuffer(pass,
                           &(SDL_GPUBufferBinding){.buffer = sdl->gpu.index_buffer, .offset = index_offset_bytes},
                           sizeof(nk_draw_index) == 2 ? SDL_GPU_INDEXELEMENTSIZE_16BIT
                                                      : SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_PushGPUVertexUniformData(cmd, 0, projection, sizeof(projection));

    /* Process draw commands */
    Uint32 index_offset = 0;
    const struct nk_draw_command *draw_cmd;
    nk_draw_foreach(draw_cmd, ctx, &sdl->gpu.cmds) {
        if (!draw_cmd->elem_count)
            continue;

        /* Set scissor */
        SDL_Rect scissor = {.x = (int)draw_cmd->clip_rect.x,
                            .y = (int)draw_cmd->clip_rect.y,
                            .w = (int)draw_cmd->clip_rect.w,
                            .h = (int)draw_cmd->clip_rect.h};
        /* Clamp to window bounds */
        if (scissor.x < 0) {
            scissor.w += scissor.x;
            scissor.x = 0;
        }
        if (scissor.y < 0) {
            scissor.h += scissor.y;
            scissor.y = 0;
        }
        if (scissor.x + scissor.w > (int)target_w)
            scissor.w = (int)target_w - scissor.x;
        if (scissor.y + scissor.h > (int)target_h)
            scissor.h = (int)target_h - scissor.y;
        if (scissor.w <= 0 || scissor.h <= 0) {
            index_offset += draw_cmd->elem_count;
            continue;
        }
        SDL_SetGPUScissor(pass, &scissor);

        /* Bind texture */
        SDL_GPUTexture *const tex = (SDL_GPUTexture *)draw_cmd->texture.ptr;
        if (tex) {
            SDL_BindGPUFragmentSamplers(
                pass, 0, &(SDL_GPUTextureSamplerBinding){.texture = tex, .sampler = sdl->gpu.sampler}, 1);
        }

        /* Draw */
        SDL_DrawGPUIndexedPrimitives(pass, draw_cmd->elem_count, 1, index_offset, 0, 0);
        index_offset += draw_cmd->elem_count;
    }

    SDL_EndGPURenderPass(pass);

    /* Cleanup */
    nk_buffer_free(&vbuf);
    nk_buffer_free(&ebuf);
    nk_clear(ctx);
    nk_buffer_clear(&sdl->gpu.cmds);
}

NK_API void nk_sdl_gpu_shutdown(struct nk_context *const ctx) {
    struct nk_sdl_gpu *const sdl = (struct nk_sdl_gpu *)ctx->userdata.ptr;
    NK_ASSERT(sdl);

#ifdef NK_INCLUDE_FONT_BAKING
    if (sdl->atlas.font_num > 0)
        nk_font_atlas_clear(&sdl->atlas);
#endif

    nk_buffer_free(&sdl->gpu.cmds);
    nk_sdl_gpu_destroy_stream_buffers(sdl);

    if (sdl->gpu.font_tex)
        SDL_ReleaseGPUTexture(sdl->device, sdl->gpu.font_tex);
    if (sdl->gpu.pipeline)
        SDL_ReleaseGPUGraphicsPipeline(sdl->device, sdl->gpu.pipeline);
    if (sdl->gpu.sampler)
        SDL_ReleaseGPUSampler(sdl->device, sdl->gpu.sampler);

    nk_free(ctx);
    SDL_free(sdl);
}

#endif /* NK_SDL3_GPU_IMPLEMENTATION_ONCE */
#endif /* NK_SDL3_GPU_IMPLEMENTATION */
