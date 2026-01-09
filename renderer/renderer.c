#include "renderer.h"
#include "renderer_internal.h"
#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL_log.h>

// Internal state
static SDL_GPUDevice *gpu_device = nullptr;
static SDL_Window *render_window = nullptr;
static SDL_GPUGraphicsPipeline *pipeline = nullptr;
static SDL_GPUSampler *sampler = nullptr;

// UI State
static TTF_TextEngine *text_engine = nullptr;
static SDL_GPUGraphicsPipeline *text_pipeline = nullptr;
static SDL_GPUGraphicsPipeline *line_pipeline = nullptr;
static SDL_GPUGraphicsPipeline *geometry_pipeline = nullptr;

// Frame state
static SDL_GPUCommandBuffer *cmd_buffer = nullptr;
static SDL_GPUTexture *swapchain_texture = nullptr;
static SDL_GPUTexture *depth_texture = nullptr;
static SDL_GPURenderPass *render_pass = nullptr;

// Buffers
#define MAX_INSTANCES 10000
static SDL_GPUBuffer *instance_buffer = nullptr;

// Uniforms
static float view_projection_matrix[16];

// Shader bytecode loading helper
static SDL_GPUShader *LoadShader(SDL_GPUDevice *device, const char *path,
                                 const char *entrypoint, const int num_samplers,
                                 const int num_uniform_buffers,
                                 const int num_storage_buffers,
                                 int num_storage_textures,
                                 const SDL_GPUShaderStage stage) {
    size_t code_size;
    void *code = SDL_LoadFile(path, &code_size);
    if (!code) {
        SDL_Log("%s:%d Failed to load shader: %s in %s", __FILE__, __LINE__,
                entrypoint, path);
        return nullptr;
    }

    const SDL_GPUShaderCreateInfo create_info = {
        .code_size = code_size,
        .code = (const Uint8 *) code,
        .entrypoint = entrypoint,
        .format = SDL_GPU_SHADERFORMAT_MSL, // Assuming MSL for now as per plan
        .stage = stage,
        .num_samplers = (Uint32) num_samplers,
        .num_uniform_buffers = (Uint32) num_uniform_buffers,
        .num_storage_buffers = (Uint32) num_storage_buffers,
        .num_storage_textures = (Uint32) num_storage_textures
    };

    SDL_GPUShader *shader = SDL_CreateGPUShader(device, &create_info);
    SDL_free(code);
    return shader;
}

static void CreateDepthTexture(const Uint32 width, const Uint32 height) {
    if (depth_texture) {
        SDL_ReleaseGPUTexture(gpu_device, depth_texture);
    }

    const SDL_GPUTextureCreateInfo depth_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
    };
    depth_texture = SDL_CreateGPUTexture(gpu_device, &depth_info);
}

bool Renderer_Init(SDL_Window *window) {
    render_window = window;

    // 1. Create GPU Device
    // Prefer Metal on macOS, but we ask for SPIRV support too just in case we add
    // shadercross later
    gpu_device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
    if (!gpu_device) {
        SDL_Log("Failed to create SDL_GPU device: %s", SDL_GetError());
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(gpu_device, window)) {
        SDL_Log("Failed to claim window for GPU device: %s", SDL_GetError());
        return false;
    }

    if (!SDL_SetGPUSwapchainParameters(gpu_device, window,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
            SDL_GPU_PRESENTMODE_MAILBOX))
        {
        SDL_Log("Failed to set swapchain parameters: %s", SDL_GetError());
        SDL_Log("Retrying with GPU_PRESENTMODE_VSYNC...");
        if (!SDL_SetGPUSwapchainParameters(gpu_device, window,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
            SDL_GPU_PRESENTMODE_VSYNC))
            {
            SDL_Log("Failed to set swapchain parameters: %s", SDL_GetError());
            return false;
        }
    }

    // 2. Load Sprite Shaders
    SDL_GPUShader *vertex_shader =
            LoadShader(gpu_device, "shaders/sprite.metal", "vertex_main",
                       0, 1, 1, 0, SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *fragment_shader =
            LoadShader(gpu_device, "shaders/sprite.metal",
                       "fragment_main", 1, 0, 0, 0, SDL_GPU_SHADERSTAGE_FRAGMENT);

    if (!vertex_shader || !fragment_shader) {
        SDL_Log("Failed to create sprite shaders");
        return false;
    }

    // 3. Create Sprite Pipeline
    SDL_GPUColorTargetDescription color_target_desc = {
        .format = SDL_GetGPUSwapchainTextureFormat(gpu_device, window),
        .blend_state = {
            .enable_blend = true,
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD
        }
    };

    SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .target_info =
        {
            .num_color_targets = 1,
            .color_target_descriptions = &color_target_desc,
            .depth_stencil_format =
            SDL_GPU_TEXTUREFORMAT_D16_UNORM, // Enable depth testing
            .has_depth_stencil_target = true
        },
        .depth_stencil_state = {
            .enable_depth_test = true,
            .enable_depth_write = true,
            .compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
        },
        .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1},
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {.cull_mode = SDL_GPU_CULLMODE_NONE},
    };

    pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &pipeline_info);

    // Cleanup shaders after pipeline creation
    SDL_ReleaseGPUShader(gpu_device, vertex_shader);
    SDL_ReleaseGPUShader(gpu_device, fragment_shader);

    if (!pipeline) {
        SDL_Log("Failed to create graphics pipeline: %s", SDL_GetError());
        return false;
    }
    SDL_Log("DEBUG: Sprite pipeline created successfully: %p", (void *) pipeline);

    // 4. Create Buffers
    SDL_GPUBufferCreateInfo buffer_info = {
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = (Uint32) (sizeof(SpriteInstance) * MAX_INSTANCES)
    };
    instance_buffer = SDL_CreateGPUBuffer(gpu_device, &buffer_info);

    // 5. Create Sampler
    SDL_GPUSamplerCreateInfo sampler_info = {
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
    };
    sampler = SDL_CreateGPUSampler(gpu_device, &sampler_info);

    // 6. Create Depth Texture
    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    CreateDepthTexture((Uint32) w, (Uint32) h);

    // 7. Initialize UI (SDL_ttf)
    if (!TTF_Init()) {
        SDL_Log("Failed to init SDL_ttf: %s", SDL_GetError());
        return false;
    }
    text_engine = TTF_CreateGPUTextEngine(gpu_device);
    if (!text_engine) {
        SDL_Log("Failed to create text engine: %s", SDL_GetError());
        return false;
    }

    // 8. Create UI Pipelines
    SDL_GPUShader *text_vs =
            LoadShader(gpu_device, "shaders/ui.metal", "vertex_text", 0,
                       1, 0, 0, SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *text_fs =
            LoadShader(gpu_device, "shaders/ui.metal", "fragment_text", 1,
                       1, 0, 0, SDL_GPU_SHADERSTAGE_FRAGMENT);

    if (text_vs && text_fs) {
        SDL_GPUVertexAttribute text_attrs[] = {
            {
                .location = 0,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                .offset = 0
            }, // Pos
            {
                .location = 1,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                .offset = (Uint32) (sizeof(float) * 2)
            } // UV
        };
        SDL_GPUVertexBufferDescription text_binding = {
            .slot = 0,
            .pitch = (Uint32) (sizeof(float) * 4),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0
        };

        SDL_GPUGraphicsPipelineCreateInfo text_pipe_info = {
            .vertex_shader = text_vs,
            .fragment_shader = text_fs,
            .vertex_input_state = {
                .num_vertex_attributes = 2,
                .vertex_attributes = text_attrs,
                .num_vertex_buffers = 1,
                .vertex_buffer_descriptions = &text_binding
            },
            .target_info =
            {
                .num_color_targets = 1,
                .color_target_descriptions = &color_target_desc,
                .has_depth_stencil_target =
                false // Text usually drawn on top without depth test
            },
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1}
        };
        text_pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &text_pipe_info);
        SDL_ReleaseGPUShader(gpu_device, text_vs);
        SDL_ReleaseGPUShader(gpu_device, text_fs);
    }

    SDL_GPUShader *line_vs =
            LoadShader(gpu_device, "shaders/ui.metal", "vertex_line", 0,
                       1, 0, 0, SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *line_fs =
            LoadShader(gpu_device, "shaders/ui.metal", "fragment_line", 0,
                       1, 0, 0, SDL_GPU_SHADERSTAGE_FRAGMENT);

    if (line_vs && line_fs) {
        SDL_GPUVertexAttribute line_attrs[] = {
            {
                .location = 0,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                .offset = 0
            } // Pos
        };
        SDL_GPUVertexBufferDescription line_binding = {
            .slot = 0,
            .pitch = (Uint32) (sizeof(float) * 3),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0
        };

        SDL_GPUGraphicsPipelineCreateInfo line_pipe_info = {
            .vertex_shader = line_vs,
            .fragment_shader = line_fs,
            .vertex_input_state = {
                .num_vertex_attributes = 1,
                .vertex_attributes = line_attrs,
                .num_vertex_buffers = 1,
                .vertex_buffer_descriptions = &line_binding
            },
            .target_info =
            {
                .num_color_targets = 1,
                .color_target_descriptions = &color_target_desc,
                .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                .has_depth_stencil_target = true // Lines might need depth test
            },
            .depth_stencil_state = {
                .enable_depth_test = true,
                .enable_depth_write = true,
                .compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
            },
            .primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST,
            .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1}
        };
        line_pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &line_pipe_info);
        SDL_ReleaseGPUShader(gpu_device, line_vs);
        SDL_ReleaseGPUShader(gpu_device, line_fs);
    }

    SDL_GPUShader *geo_vs =
            LoadShader(gpu_device, "shaders/geometry.metal",
                       "vertex_geometry", 0, 1, 0, 0, SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader *geo_fs =
            LoadShader(gpu_device, "shaders/geometry.metal",
                       "fragment_geometry", 0, 0, 0, 0, SDL_GPU_SHADERSTAGE_FRAGMENT);

    if (geo_vs && geo_fs) {
        SDL_GPUVertexAttribute geo_attrs[] = {
            {
                .location = 0,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                .offset = 0
            }, // Pos
            {
                .location = 1,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
                .offset = (Uint32) (sizeof(float) * 2)
            }, // Color
            {
                .location = 2,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                .offset = (Uint32) (sizeof(float) * 6)
            } // UV
        };
        SDL_GPUVertexBufferDescription geo_binding = {
            .slot = 0,
            .pitch = (Uint32) sizeof(SDL_Vertex),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0
        };

        SDL_GPUGraphicsPipelineCreateInfo geo_pipe_info = {
            .vertex_shader = geo_vs,
            .fragment_shader = geo_fs,
            .vertex_input_state = {
                .num_vertex_attributes = 3,
                .vertex_attributes = geo_attrs,
                .num_vertex_buffers = 1,
                .vertex_buffer_descriptions = &geo_binding
            },
            .target_info = {
                .num_color_targets = 1,
                .color_target_descriptions = &color_target_desc,
                .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                .has_depth_stencil_target = true
            },
            .depth_stencil_state = {
                .enable_depth_test = true,
                .enable_depth_write = true,
                .compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
            },
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1}
        };
        geometry_pipeline =
                SDL_CreateGPUGraphicsPipeline(gpu_device, &geo_pipe_info);
        SDL_ReleaseGPUShader(gpu_device, geo_vs);
        SDL_ReleaseGPUShader(gpu_device, geo_fs);
    }

    return true;
}

void Renderer_Shutdown(void) {
    if (gpu_device) {
        if (text_engine)
            TTF_DestroyGPUTextEngine(text_engine);
        TTF_Quit();
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, pipeline);
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, text_pipeline);
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, line_pipeline);
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, geometry_pipeline);
        SDL_ReleaseGPUSampler(gpu_device, sampler);
        SDL_ReleaseGPUBuffer(gpu_device, instance_buffer);
        SDL_ReleaseGPUTexture(gpu_device, depth_texture);
        SDL_DestroyGPUDevice(gpu_device);
    }
}

void Renderer_Resize(const int width, const int height) {
    CreateDepthTexture((Uint32) width, (Uint32) height);
}


// Common present modes:
// SDL_GPU_PRESENTMODE_IMMEDIATE -> no vsync
// SDL_GPU_PRESENTMODE_VSYNC -> vsync (guaranteed)
// SDL_GPU_PRESENTMODE_MAILBOX -> low-latency vsync (if supported)
void Renderer_SetPresentMode(const SDL_GPUPresentMode mode) {
    if (!gpu_device || !render_window) return;

    // Finish any in-flight work to avoid changing swapchain mid-pass
    if (render_pass) {
        SDL_EndGPURenderPass(render_pass);
        render_pass = nullptr;
    }
    if (cmd_buffer) {
        SDL_SubmitGPUCommandBuffer(cmd_buffer);
        cmd_buffer = nullptr;
    }

    SDL_WaitForGPUIdle(gpu_device);

    // Apply new swapchain parameters
    if (!SDL_SetGPUSwapchainParameters(
            gpu_device, render_window,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) {
        SDL_Log("Failed to set present mode: %s", SDL_GetError());
        if (mode == SDL_GPU_PRESENTMODE_MAILBOX) {
            SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "Retrying with GPU_PRESENTMODE_VSYNC...");
            if (!SDL_SetGPUSwapchainParameters(gpu_device, render_window,
                SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                SDL_GPU_PRESENTMODE_VSYNC)) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to set present mode: %s", SDL_GetError());
            }
        }
    }

    // Invalidate cached swapchain texture so next BeginFrame reacquires it
    swapchain_texture = nullptr;
}

void Renderer_SetVSync(const bool enabled) {
    Renderer_SetPresentMode(enabled ? SDL_GPU_PRESENTMODE_MAILBOX
                                   : SDL_GPU_PRESENTMODE_IMMEDIATE);
}


SDL_GPUTexture *Renderer_LoadTexture(const char *path) {
    SDL_Surface *surface = IMG_Load(path);
    if (!surface) {
        SDL_Log("Failed to load image %s: %s", path, SDL_GetError());
        return nullptr;
    }

    const SDL_GPUTextureCreateInfo tex_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .width = (Uint32) surface->w,
        .height = (Uint32) surface->h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER
    };

    SDL_GPUTexture *texture = SDL_CreateGPUTexture(gpu_device, &tex_info);
    if (!texture) {
        SDL_Log("Failed to create GPU texture");
        SDL_DestroySurface(surface);
        return nullptr;
    }

    // Upload data
    const SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (Uint32) (surface->w * surface->h * 4)
    };
    SDL_GPUTransferBuffer *transfer_buffer =
            SDL_CreateGPUTransferBuffer(gpu_device, &transfer_info);

    Uint8 *map = SDL_MapGPUTransferBuffer(gpu_device, transfer_buffer, false);
    // Convert surface to RGBA8888 if needed
    // SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM expects R, G, B, A in memory.
    // SDL_PIXELFORMAT_ABGR8888 on Little Endian is R G B A in memory.
    SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ABGR8888);

    if (!converted) {
        SDL_Log("Failed to convert surface format: %s", SDL_GetError());
        SDL_UnmapGPUTransferBuffer(gpu_device, transfer_buffer);
        SDL_ReleaseGPUTransferBuffer(gpu_device, transfer_buffer);
        SDL_ReleaseGPUTexture(gpu_device, texture);
        SDL_DestroySurface(surface);
        return nullptr;
    }

    const Uint8 *src_pixels = (const Uint8 *) converted->pixels;
    Uint8 *dst_pixels = map;
    const int row_size = converted->w * 4;
    for (int i = 0; i < converted->h; i++) {
        SDL_memcpy(dst_pixels, src_pixels, row_size);
        src_pixels += converted->pitch;
        dst_pixels += row_size;
    }
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer_buffer);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(gpu_device);
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);

    const SDL_GPUTextureTransferInfo source = {
        .transfer_buffer = transfer_buffer,
        .offset = 0,
        .pixels_per_row = (Uint32) converted->w,
        .rows_per_layer = (Uint32) converted->h
    };

    const SDL_GPUTextureRegion destination = {
        .texture = texture,
        .w = (Uint32) converted->w,
        .h = (Uint32) converted->h,
        .d = 1
    };

    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(cmd);

    SDL_ReleaseGPUTransferBuffer(gpu_device, transfer_buffer);
    SDL_DestroySurface(converted);
    SDL_DestroySurface(surface);
    return texture;
}

void Renderer_DestroyTexture(SDL_GPUTexture *texture) {
    if (texture) {
        SDL_ReleaseGPUTexture(gpu_device, texture);
    }
}

void Renderer_BeginFrame(void) {
    cmd_buffer = SDL_AcquireGPUCommandBuffer(gpu_device);
    if (!cmd_buffer) {
        SDL_Log("DEBUG BeginFrame: Failed to acquire command buffer!");
        return;
    }

    if (!SDL_AcquireGPUSwapchainTexture(cmd_buffer, render_window,
                                        &swapchain_texture, nullptr, nullptr)) {
        SDL_Log("DEBUG BeginFrame: Failed to acquire swapchain texture: %s", SDL_GetError());
        // submit empty command buffer to avoid leak
        SDL_SubmitGPUCommandBuffer(cmd_buffer);
        cmd_buffer = nullptr;
        return;
    }

    if (!swapchain_texture) {
        // this can happen if window is minimized, not an error
        // submit empty command buffer to avoid leak
        SDL_SubmitGPUCommandBuffer(cmd_buffer);
        cmd_buffer = nullptr;
        return;
    }

    const SDL_GPUColorTargetInfo color_target = {
        .texture = swapchain_texture,
        .clear_color = {0.392f, 0.584f, 0.929f, 1.0f},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE
    };

    const SDL_GPUDepthStencilTargetInfo depth_target = {
        .texture = depth_texture,
        .clear_depth = 1.0f,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE
    };

    render_pass =
            SDL_BeginGPURenderPass(cmd_buffer, &color_target, 1, &depth_target);
}

void Renderer_EndFrame(void) {
    if (render_pass) {
        SDL_EndGPURenderPass(render_pass);
        render_pass = nullptr;
    }
    if (cmd_buffer) {
        const bool result = SDL_SubmitGPUCommandBuffer(cmd_buffer);
        cmd_buffer = nullptr;
        if (!result) {
            SDL_Log("DEBUG EndFrame: Failed to submit command buffer: %s", SDL_GetError());
        }
    }
}

void Renderer_SetViewProjection(const float *viewProjMatrix) {
    SDL_memcpy(view_projection_matrix, viewProjMatrix, sizeof(float) * 16);
}

void Renderer_DrawSprites(SDL_GPUTexture *texture,
                          const SpriteInstance *instances, const int count) {
    if (count == 0)
        return;
    if (!render_pass) {
        SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "DrawSprites: render_pass is NULL!");
        return;
    }
    if (!pipeline) {
        SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "DrawSprites: pipeline is NULL!");
        return;
    }
    if (!texture) {
        SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "DrawSprites: texture is NULL!");
        return;
    }
    if (!sampler) {
        SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "DrawSprites: sampler is NULL!");
        return;
    }

    // Upload instance data
    // In a real engine, use a ring buffer or transient buffers
    const SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (Uint32) (sizeof(SpriteInstance) * count)
    };
    SDL_GPUTransferBuffer *tb =
            SDL_CreateGPUTransferBuffer(gpu_device, &transfer_info);
    void *data = SDL_MapGPUTransferBuffer(gpu_device, tb, true);
    SDL_memcpy(data, instances, (size_t) sizeof(SpriteInstance) * count);
    SDL_UnmapGPUTransferBuffer(gpu_device, tb);

    // We need to close the render pass to do a copy (upload), then reopen it?
    // SDL_GPU allows uploading to buffers that are bound? No, we need a CopyPass.
    // This is inefficient (breaking render pass).
    // BETTER: Use `SDL_PushGPUVertexUniformData` for small things, but instances
    // are large. BETTER: Map the buffer directly if it was CPU_TO_GPU? SDL_GPU
    // buffers are usually device local. CORRECT WAY: Use a cycle of transfer
    // buffers and bind the transfer buffer directly as a storage buffer? OR: Just
    // break the pass for now.

    SDL_EndGPURenderPass(render_pass);

    // Create a transient storage buffer for this draw call
    const SDL_GPUBufferCreateInfo buffer_info = {
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = (Uint32) (sizeof(SpriteInstance) * count)
    };
    SDL_GPUBuffer *draw_instance_buffer =
            SDL_CreateGPUBuffer(gpu_device, &buffer_info);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd_buffer);
    const SDL_GPUTransferBufferLocation source = {.transfer_buffer = tb};
    const SDL_GPUBufferRegion dest = {
        .buffer = draw_instance_buffer,
        .size = (Uint32) (sizeof(SpriteInstance) * count)
    };
    SDL_UploadToGPUBuffer(copy, &source, &dest, true);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(gpu_device, tb);

    // Resume Render Pass
    const SDL_GPUColorTargetInfo color_target = {
        .texture = swapchain_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE
    };
    const SDL_GPUDepthStencilTargetInfo depth_target = {
        .texture = depth_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
        .cycle = false
    }; // Cycle depth to avoid hazards if we were writing to it
    // (we aren't, but good practice)
    // Actually, we are loading depth, so we want to keep it.

    render_pass =
            SDL_BeginGPURenderPass(cmd_buffer, &color_target, 1, &depth_target);

    SDL_BindGPUGraphicsPipeline(render_pass, pipeline);
    SDL_BindGPUFragmentSamplers(
        render_pass, 0,
        &((SDL_GPUTextureSamplerBinding){.texture = texture, .sampler = sampler}),
        1);
    SDL_BindGPUVertexStorageBuffers(render_pass, 0, &draw_instance_buffer, 1);

    // Push ViewProjection Matrix
    SDL_PushGPUVertexUniformData(cmd_buffer, 0, view_projection_matrix,
                                 sizeof(view_projection_matrix));

    SDL_DrawGPUPrimitives(render_pass, 6, count, 0, 0);

    // Release the buffer (it will be destroyed after command buffer completion)
    SDL_ReleaseGPUBuffer(gpu_device, draw_instance_buffer);
}

TTF_TextEngine *Renderer_GetTextEngine(void) { return text_engine; }

void Renderer_DrawText(TTF_Text *text, const float x, const float y) {
    if (!render_pass || !text_pipeline)
        return;

    TTF_GPUAtlasDrawSequence *seq = TTF_GetGPUTextDrawData(text);
    if (!seq)
        return;

    // Set ViewProjection for UI (Ortho)
    int w, h;
    SDL_GetWindowSizeInPixels(render_window, &w, &h);
    const float projection[16] = {
        2.0f / w, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / h, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f
    };
    // Translate to x, y
    // We can just add x, y to vertices or use matrix
    // Let's use matrix translation
    // But we need to combine with projection
    // Simplified: just pass projection and add x,y to vertices in shader?
    // Or update projection matrix to include translation?
    // Let's just pass projection and assume vertices are relative to 0,0
    // Wait, TTF_GetGPUTextDrawData returns vertices relative to (0,0) of the text
    // object? Or absolute? Docs say: "The positive X-axis is taken towards the
    // right... If you want to use a different coordinate system you will need to
    // transform the vertices yourself." It doesn't say if it applies the text
    // position. TTF_SetTextPosition sets position. So likely the vertices are
    // already transformed by position? If so, we just need the Ortho projection.

    SDL_PushGPUVertexUniformData(cmd_buffer, 0, projection, sizeof(projection));

    while (seq) {
        // Upload vertices and indices
        // We need a buffer for this.
        // Since it's dynamic, we use transfer buffer and bind it as vertex buffer?
        // No, vertex buffer must be BUFFERUSAGE_VERTEX.
        // So we need to upload to a vertex buffer.
        // This is slow inside a render pass.
        // We have to break the pass.
        SDL_EndGPURenderPass(render_pass);

        const Uint32 vert_size =
                (Uint32) (seq->num_vertices * sizeof(float) * 4); // xy + uv
        const Uint32 idx_size = (Uint32) (seq->num_indices * sizeof(int));

        SDL_GPUTransferBufferCreateInfo tb_info = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = vert_size + idx_size
        };
        SDL_GPUTransferBuffer *tb =
                SDL_CreateGPUTransferBuffer(gpu_device, &tb_info);
        Uint8 *ptr = SDL_MapGPUTransferBuffer(gpu_device, tb, true);

        // Interleave xy and uv?
        // My pipeline expects: 0: Float2 (Pos), 1: Float2 (UV)
        // Stride is sizeof(float)*4.
        // I need to interleave them.
        float *v_ptr = (float *) ptr;
        for (int i = 0; i < seq->num_vertices; i++) {
            v_ptr[i * 4 + 0] = seq->xy[i].x + x;
            v_ptr[i * 4 + 1] = -seq->xy[i].y + y;
            v_ptr[i * 4 + 2] = seq->uv[i].x;
            v_ptr[i * 4 + 3] = seq->uv[i].y;
        }

        SDL_memcpy(ptr + vert_size, seq->indices, idx_size);
        SDL_UnmapGPUTransferBuffer(gpu_device, tb);

        // Create GPU Buffers (transient)
        SDL_GPUBufferCreateInfo vb_info = {
            .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
            .size = vert_size
        };
        SDL_GPUBuffer *vb = SDL_CreateGPUBuffer(gpu_device, &vb_info);

        SDL_GPUBufferCreateInfo ib_info = {
            .usage = SDL_GPU_BUFFERUSAGE_INDEX,
            .size = idx_size
        };
        SDL_GPUBuffer *ib = SDL_CreateGPUBuffer(gpu_device, &ib_info);

        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd_buffer);

        SDL_GPUTransferBufferLocation src_v = {.transfer_buffer = tb, .offset = 0};
        SDL_GPUBufferRegion dst_v = {.buffer = vb, .size = vert_size};
        SDL_UploadToGPUBuffer(copy, &src_v, &dst_v, true);

        SDL_GPUTransferBufferLocation src_i = {
            .transfer_buffer = tb,
            .offset = vert_size
        };
        SDL_GPUBufferRegion dst_i = {.buffer = ib, .size = idx_size};
        SDL_UploadToGPUBuffer(copy, &src_i, &dst_i, true);

        SDL_EndGPUCopyPass(copy);
        SDL_ReleaseGPUTransferBuffer(gpu_device, tb);

        // Resume Render Pass
        SDL_GPUColorTargetInfo color_target = {
            .texture = swapchain_texture,
            .load_op = SDL_GPU_LOADOP_LOAD,
            .store_op = SDL_GPU_STOREOP_STORE
        };
        render_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_target, 1,
                                             nullptr); // No depth for UI

        SDL_BindGPUGraphicsPipeline(render_pass, text_pipeline);
        SDL_BindGPUVertexBuffers(
            render_pass, 0, &((SDL_GPUBufferBinding){.buffer = vb, .offset = 0}),
            1);
        SDL_BindGPUIndexBuffer(render_pass,
                               &((SDL_GPUBufferBinding){.buffer = ib, .offset = 0}),
                               SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_BindGPUFragmentSamplers(
            render_pass, 0,
            &(SDL_GPUTextureSamplerBinding){
                .texture = seq->atlas_texture,
                .sampler = sampler
            },
            1);

        // Color uniform
        constexpr float color[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // White
        SDL_PushGPUFragmentUniformData(cmd_buffer, 0, color, sizeof(color));

        SDL_DrawGPUIndexedPrimitives(render_pass, seq->num_indices, 1, 0, 0, 0);

        // Cleanup buffers (deferred release)
        SDL_ReleaseGPUBuffer(gpu_device, vb);
        SDL_ReleaseGPUBuffer(gpu_device, ib);

        seq = seq->next;
    }
}

void Renderer_DrawLine(const float x1, const float y1, const float z1,
                       const float x2, const float y2, const float z2,
                       const SDL_FColor color) {
    if (!render_pass || !line_pipeline)
        return;

    // Upload vertices
    const float vertices[] = {x1, y1, z1, x2, y2, z2};
    constexpr Uint32 size = (Uint32) sizeof(vertices);

    SDL_EndGPURenderPass(render_pass);

    const SDL_GPUTransferBufferCreateInfo tb_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(gpu_device, &tb_info);
    void *ptr = SDL_MapGPUTransferBuffer(gpu_device, tb, true);
    SDL_memcpy(ptr, vertices, size);
    SDL_UnmapGPUTransferBuffer(gpu_device, tb);

    const SDL_GPUBufferCreateInfo vb_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = size
    };
    SDL_GPUBuffer *vb = SDL_CreateGPUBuffer(gpu_device, &vb_info);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd_buffer);
    const SDL_GPUTransferBufferLocation src = {
        .transfer_buffer = tb,
        .offset = 0
    };
    const SDL_GPUBufferRegion dst = {.buffer = vb, .size = size};
    SDL_UploadToGPUBuffer(copy, &src, &dst, true);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(gpu_device, tb);

    // Resume Render Pass
    const SDL_GPUColorTargetInfo color_target = {
        .texture = swapchain_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op =
        SDL_GPU_STOREOP_STORE
    };
    const SDL_GPUDepthStencilTargetInfo depth_target = {
        .texture = depth_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE
    };
    render_pass =
            SDL_BeginGPURenderPass(cmd_buffer, &color_target, 1, &depth_target);

    SDL_BindGPUGraphicsPipeline(render_pass, line_pipeline);
    SDL_BindGPUVertexBuffers(
        render_pass, 0, &((SDL_GPUBufferBinding){.buffer = vb, .offset = 0}), 1);

    SDL_PushGPUVertexUniformData(cmd_buffer, 0, view_projection_matrix,
                                 sizeof(view_projection_matrix));
    SDL_PushGPUFragmentUniformData(cmd_buffer, 0, &color, sizeof(color));

    SDL_DrawGPUPrimitives(render_pass, 2, 1, 0, 0);

    SDL_ReleaseGPUBuffer(gpu_device, vb);
}

void Renderer_DrawGeometry(const SDL_Vertex *vertices, const int count) {
    if (!render_pass || !geometry_pipeline || count == 0)
        return;

    const Uint32 size = (Uint32) (sizeof(SDL_Vertex) * count);

    SDL_EndGPURenderPass(render_pass);

    const SDL_GPUTransferBufferCreateInfo tb_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(gpu_device, &tb_info);
    void *ptr = SDL_MapGPUTransferBuffer(gpu_device, tb, true);
    SDL_memcpy(ptr, vertices, size);
    SDL_UnmapGPUTransferBuffer(gpu_device, tb);

    const SDL_GPUBufferCreateInfo vb_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = size
    };
    SDL_GPUBuffer *vb = SDL_CreateGPUBuffer(gpu_device, &vb_info);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd_buffer);
    const SDL_GPUTransferBufferLocation src = {
        .transfer_buffer = tb,
        .offset = 0
    };
    const SDL_GPUBufferRegion dst = {.buffer = vb, .size = size};
    SDL_UploadToGPUBuffer(copy, &src, &dst, true);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(gpu_device, tb);

    const SDL_GPUColorTargetInfo color_target = {
        .texture = swapchain_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op =
        SDL_GPU_STOREOP_STORE
    };
    const SDL_GPUDepthStencilTargetInfo depth_target = {
        .texture = depth_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE
    };
    render_pass =
            SDL_BeginGPURenderPass(cmd_buffer, &color_target, 1, &depth_target);

    SDL_BindGPUGraphicsPipeline(render_pass, geometry_pipeline);
    SDL_BindGPUVertexBuffers(
        render_pass, 0, &((SDL_GPUBufferBinding){.buffer = vb, .offset = 0}), 1);

    SDL_PushGPUVertexUniformData(cmd_buffer, 0, view_projection_matrix,
                                 sizeof(view_projection_matrix));

    SDL_DrawGPUPrimitives(render_pass, (Uint32) count, 1, 0, 0);

    SDL_ReleaseGPUBuffer(gpu_device, vb);
}

void Renderer_DrawGeometryScreenSpace(const SDL_Vertex *vertices,
                                      const int count) {
    if (!render_pass || !geometry_pipeline || count == 0)
        return;

    // Screen-space projection: (0,0) = top-left, Y increases downward
    int w, h;
    SDL_GetWindowSizeInPixels(render_window, &w, &h);
    const float screen_proj[16] = {
        2.0f / w, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / h, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f
    };

    const Uint32 size = (Uint32) (sizeof(SDL_Vertex) * count);

    SDL_EndGPURenderPass(render_pass);

    const SDL_GPUTransferBufferCreateInfo tb_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(gpu_device, &tb_info);
    void *ptr = SDL_MapGPUTransferBuffer(gpu_device, tb, true);
    SDL_memcpy(ptr, vertices, size);
    SDL_UnmapGPUTransferBuffer(gpu_device, tb);

    const SDL_GPUBufferCreateInfo vb_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = size
    };
    SDL_GPUBuffer *vb = SDL_CreateGPUBuffer(gpu_device, &vb_info);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd_buffer);
    const SDL_GPUTransferBufferLocation src = {.transfer_buffer = tb, .offset = 0};
    const SDL_GPUBufferRegion dst = {.buffer = vb, .size = size};
    SDL_UploadToGPUBuffer(copy, &src, &dst, true);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(gpu_device, tb);

    // Use depth target since geometry_pipeline requires it
    const SDL_GPUColorTargetInfo color_target = {
        .texture = swapchain_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE
    };
    const SDL_GPUDepthStencilTargetInfo depth_target = {
        .texture = depth_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE
    };
    render_pass =
            SDL_BeginGPURenderPass(cmd_buffer, &color_target, 1, &depth_target);

    SDL_BindGPUGraphicsPipeline(render_pass, geometry_pipeline);
    SDL_BindGPUVertexBuffers(
        render_pass, 0, &((SDL_GPUBufferBinding){.buffer = vb, .offset = 0}), 1);

    SDL_PushGPUVertexUniformData(cmd_buffer, 0, screen_proj, sizeof(screen_proj));

    SDL_DrawGPUPrimitives(render_pass, (Uint32) count, 1, 0, 0);

    SDL_ReleaseGPUBuffer(gpu_device, vb);
}

void Renderer_DrawTextureDebug(SDL_GPUTexture *texture,
                               const float x, const float y,
                               const float width, const float height) {
    if (!render_pass || !pipeline)
        return;

    // Create a simple orthographic projection for screen-space rendering
    int w, h;
    SDL_GetWindowSizeInPixels(render_window, &w, &h);

    // Orthographic projection: maps screen coords to NDC
    // X: 0..w -> -1..1
    // Y: 0..h -> 1..-1 (flip Y so 0 is top)
    const float screen_proj[16] = {
        2.0f / w, 0.0f, 0.0f, 0.0f, // column 0
        0.0f, -2.0f / h, 0.0f, 0.0f, // column 1
        0.0f, 0.0f, 1.0f, 0.0f, // column 2
        -1.0f, 1.0f, 0.0f, 1.0f // column 3
    };

    // Create a single sprite instance
    const SpriteInstance instance = {
        .x = x,
        .y = y,
        .z = 0.0f, // On top
        .p1 = 0.0f,
        .w = width,
        .h = height,
        .p2 = 0.0f,
        .p3 = 0.0f,
        .u = 0.0f,
        .v = 0.0f,
        .uw = 1.0f, // Full texture width
        .vh = 1.0f // Full texture height
    };

    // Upload instance data
    const SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (Uint32) sizeof(SpriteInstance)
    };
    SDL_GPUTransferBuffer *tb =
            SDL_CreateGPUTransferBuffer(gpu_device, &transfer_info);
    void *data = SDL_MapGPUTransferBuffer(gpu_device, tb, true);
    SDL_memcpy(data, &instance, sizeof(SpriteInstance));
    SDL_UnmapGPUTransferBuffer(gpu_device, tb);

    SDL_EndGPURenderPass(render_pass);

    // Create storage buffer
    const SDL_GPUBufferCreateInfo buffer_info = {
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = (Uint32) sizeof(SpriteInstance)
    };
    SDL_GPUBuffer *instance_buf = SDL_CreateGPUBuffer(gpu_device, &buffer_info);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd_buffer);
    const SDL_GPUTransferBufferLocation source = {.transfer_buffer = tb};
    const SDL_GPUBufferRegion dest = {
        .buffer = instance_buf,
        .size = (Uint32) sizeof(SpriteInstance)
    };
    SDL_UploadToGPUBuffer(copy, &source, &dest, true);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(gpu_device, tb);

    // Resume render pass - MUST include depth target because sprite pipeline requires it
    const SDL_GPUColorTargetInfo color_target = {
        .texture = swapchain_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE
    };
    const SDL_GPUDepthStencilTargetInfo depth_target = {
        .texture = depth_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE
    };
    render_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_target, 1, &depth_target);

    SDL_BindGPUGraphicsPipeline(render_pass, pipeline);
    SDL_BindGPUFragmentSamplers(
        render_pass, 0,
        &((SDL_GPUTextureSamplerBinding){.texture = texture, .sampler = sampler}),
        1);
    SDL_BindGPUVertexStorageBuffers(render_pass, 0, &instance_buf, 1);

    SDL_PushGPUVertexUniformData(cmd_buffer, 0, screen_proj, sizeof(screen_proj));

    SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);

    SDL_ReleaseGPUBuffer(gpu_device, instance_buf);

    // render_pass is left in valid state with depth target
}

void Renderer_DrawFilledQuadDebug(const float x, const float y, const float width, const float height,
                                  const SDL_FColor color) {
    if (!render_pass || !geometry_pipeline)
        return;

    // Use screen-space projection (same as text)
    int w, h;
    SDL_GetWindowSizeInPixels(render_window, &w, &h);
    const float screen_proj[16] = {
        2.0f / w, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / h, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f
    };

    // Two triangles forming a quad
    // SDL_Vertex has: position (float2), color (SDL_FColor), tex_coord (float2)
    const SDL_Vertex vertices[6] = {
        // Triangle 1
        {{x, y}, color, {0, 0}},
        {{x + width, y}, color, {1, 0}},
        {{x, y + height}, color, {0, 1}},
        // Triangle 2
        {{x + width, y}, color, {1, 0}},
        {{x + width, y + height}, color, {1, 1}},
        {{x, y + height}, color, {0, 1}},
    };

    constexpr Uint32 size = sizeof(vertices);

    SDL_EndGPURenderPass(render_pass);

    const SDL_GPUTransferBufferCreateInfo tb_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(gpu_device, &tb_info);
    void *ptr = SDL_MapGPUTransferBuffer(gpu_device, tb, true);
    SDL_memcpy(ptr, vertices, size);
    SDL_UnmapGPUTransferBuffer(gpu_device, tb);

    const SDL_GPUBufferCreateInfo vb_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = size
    };
    SDL_GPUBuffer *vb = SDL_CreateGPUBuffer(gpu_device, &vb_info);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd_buffer);
    const SDL_GPUTransferBufferLocation src = {.transfer_buffer = tb, .offset = 0};
    const SDL_GPUBufferRegion dst = {.buffer = vb, .size = size};
    SDL_UploadToGPUBuffer(copy, &src, &dst, true);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(gpu_device, tb);

    // Render pass WITH depth (geometry pipeline requires depth target)
    const SDL_GPUColorTargetInfo color_target = {
        .texture = swapchain_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE
    };
    const SDL_GPUDepthStencilTargetInfo depth_target = {
        .texture = depth_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE
    };
    render_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_target, 1, &depth_target);

    SDL_BindGPUGraphicsPipeline(render_pass, geometry_pipeline);
    SDL_BindGPUVertexBuffers(
        render_pass, 0, &((SDL_GPUBufferBinding){.buffer = vb, .offset = 0}), 1);

    SDL_PushGPUVertexUniformData(cmd_buffer, 0, screen_proj, sizeof(screen_proj));

    SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);

    SDL_ReleaseGPUBuffer(gpu_device, vb);

    // render_pass left in valid state with depth
}

// ============================================================================
// Low-Level UI Batch Flush Implementation
// ============================================================================
// These are called by ui_batch.c to render collected UI data efficiently.

void Renderer_FlushUIGeometry(const SDL_Vertex *vertices, const int count) {
    if (!swapchain_texture || !cmd_buffer || !geometry_pipeline || count == 0)
        return;

    // Screen-space projection matrix
    int w, h;
    SDL_GetWindowSizeInPixels(render_window, &w, &h);
    const float screen_proj[16] = {
        2.0f / (float) w, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / (float) h, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f
    };

    // End current render pass if active
    if (render_pass) {
        SDL_EndGPURenderPass(render_pass);
        render_pass = nullptr;
    }

    const Uint32 size = (Uint32) (sizeof(SDL_Vertex) * count);

    // Upload to GPU
    const SDL_GPUTransferBufferCreateInfo tb_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(gpu_device, &tb_info);
    void *ptr = SDL_MapGPUTransferBuffer(gpu_device, tb, true);
    SDL_memcpy(ptr, vertices, size);
    SDL_UnmapGPUTransferBuffer(gpu_device, tb);

    const SDL_GPUBufferCreateInfo vb_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = size
    };
    SDL_GPUBuffer *vb = SDL_CreateGPUBuffer(gpu_device, &vb_info);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd_buffer);
    const SDL_GPUTransferBufferLocation src = {.transfer_buffer = tb, .offset = 0};
    const SDL_GPUBufferRegion dst = {.buffer = vb, .size = size};
    SDL_UploadToGPUBuffer(copy, &src, &dst, true);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(gpu_device, tb);

    // Render (with depth target - geometry_pipeline requires it)
    SDL_GPUColorTargetInfo color_target = {
        .texture = swapchain_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE
    };
    SDL_GPUDepthStencilTargetInfo depth_target = {
        .texture = depth_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_DONT_CARE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE
    };
    render_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_target, 1, &depth_target);

    SDL_BindGPUGraphicsPipeline(render_pass, geometry_pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0,
                             &((SDL_GPUBufferBinding){.buffer = vb, .offset = 0}), 1);
    SDL_PushGPUVertexUniformData(cmd_buffer, 0, screen_proj, sizeof(screen_proj));
    SDL_DrawGPUPrimitives(render_pass, (Uint32) count, 1, 0, 0);

    SDL_EndGPURenderPass(render_pass);
    render_pass = nullptr;
    SDL_ReleaseGPUBuffer(gpu_device, vb);

    // Restore render pass for any subsequent drawing
    color_target.load_op = SDL_GPU_LOADOP_LOAD;
    depth_target.load_op = SDL_GPU_LOADOP_LOAD;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    render_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_target, 1, &depth_target);
}

void Renderer_FlushUIText(const float *vertices, const int vertex_count,
                          const int *indices, const int index_count,
                          const UITextAtlasInfo *atlases, const int atlas_count) {
    if (!swapchain_texture || !cmd_buffer || !text_pipeline || vertex_count == 0)
        return;

    // Screen-space projection matrix
    int w, h;
    SDL_GetWindowSizeInPixels(render_window, &w, &h);
    const float screen_proj[16] = {
        2.0f / (float) w, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / (float) h, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f
    };

    // End current render pass if active
    if (render_pass) {
        SDL_EndGPURenderPass(render_pass);
        render_pass = nullptr;
    }

    const Uint32 vert_size = (Uint32) (sizeof(float) * 4 * vertex_count);
    const Uint32 idx_size = (Uint32) (sizeof(int) * index_count);

    // Upload vertices and indices
    const SDL_GPUTransferBufferCreateInfo tb_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = vert_size + idx_size
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(gpu_device, &tb_info);
    Uint8 *ptr = SDL_MapGPUTransferBuffer(gpu_device, tb, true);
    SDL_memcpy(ptr, vertices, vert_size);
    SDL_memcpy(ptr + vert_size, indices, idx_size);
    SDL_UnmapGPUTransferBuffer(gpu_device, tb);

    const SDL_GPUBufferCreateInfo vb_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = vert_size
    };
    SDL_GPUBuffer *vb = SDL_CreateGPUBuffer(gpu_device, &vb_info);

    const SDL_GPUBufferCreateInfo ib_info = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = idx_size
    };
    SDL_GPUBuffer *ib = SDL_CreateGPUBuffer(gpu_device, &ib_info);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd_buffer);
    const SDL_GPUTransferBufferLocation src_v = {.transfer_buffer = tb, .offset = 0};
    const SDL_GPUBufferRegion dst_v = {.buffer = vb, .size = vert_size};
    SDL_UploadToGPUBuffer(copy, &src_v, &dst_v, true);

    const SDL_GPUTransferBufferLocation src_i = {.transfer_buffer = tb, .offset = vert_size};
    const SDL_GPUBufferRegion dst_i = {.buffer = ib, .size = idx_size};
    SDL_UploadToGPUBuffer(copy, &src_i, &dst_i, true);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(gpu_device, tb);

    // Begin render pass for text (no depth needed)
    SDL_GPUColorTargetInfo color_target = {
        .texture = swapchain_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE
    };
    render_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_target, 1, nullptr);

    SDL_BindGPUGraphicsPipeline(render_pass, text_pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0,
                             &((SDL_GPUBufferBinding){.buffer = vb, .offset = 0}), 1);
    SDL_BindGPUIndexBuffer(render_pass,
                           &((SDL_GPUBufferBinding){.buffer = ib, .offset = 0}),
                           SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_PushGPUVertexUniformData(cmd_buffer, 0, screen_proj, sizeof(screen_proj));

    constexpr float text_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    SDL_PushGPUFragmentUniformData(cmd_buffer, 0, text_color, sizeof(text_color));

    // Draw each atlas range with its texture
    for (int i = 0; i < atlas_count; i++) {
        const UITextAtlasInfo *range = &atlases[i];
        if (range->index_count == 0)
            continue;

        SDL_BindGPUFragmentSamplers(render_pass, 0,
                                    &((SDL_GPUTextureSamplerBinding){
                                        .texture = range->atlas, .sampler = sampler
                                    }),
                                    1);

        SDL_DrawGPUIndexedPrimitives(render_pass, (Uint32) range->index_count, 1,
                                     (Uint32) range->start_index, 0, 0);
    }

    SDL_EndGPURenderPass(render_pass);
    render_pass = nullptr;
    SDL_ReleaseGPUBuffer(gpu_device, vb);
    SDL_ReleaseGPUBuffer(gpu_device, ib);

    // Restore render pass with depth for any subsequent drawing
    color_target.load_op = SDL_GPU_LOADOP_LOAD;
    const SDL_GPUDepthStencilTargetInfo depth_target = {
        .texture = depth_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE
    };
    render_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_target, 1, &depth_target);
}

// ============================================================================
// Internal Accessors (for renderer subsystems like debug_ui)
// ============================================================================

SDL_Window *Renderer_GetWindow(void) { return render_window; }
SDL_GPUDevice *Renderer_GetDevice(void) { return gpu_device; }
SDL_GPUCommandBuffer *Renderer_GetCommandBuffer(void) { return cmd_buffer; }
SDL_GPUTexture *Renderer_GetSwapchainTexture(void) { return swapchain_texture; }

void Renderer_EndRenderPass(void) {
    if (render_pass) {
        SDL_EndGPURenderPass(render_pass);
        render_pass = nullptr;
    }
}

void Renderer_ResumeRenderPass(void) {
    if (render_pass || !swapchain_texture || !cmd_buffer) return;

    const SDL_GPUColorTargetInfo color_target = {
        .texture = swapchain_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE
    };
    const SDL_GPUDepthStencilTargetInfo depth_target = {
        .texture = depth_texture,
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE
    };
    render_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_target, 1, &depth_target);
}
