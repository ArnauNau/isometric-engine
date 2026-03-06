//
// Created by Arnau Sanz on 1/12/25.
//

#include "ui.h"

#include "SDL3/SDL_render.h"
#include "SDL3_ttf/SDL_ttf.h"
#include "renderer.h"

#define UI_GEOMETRY_INITIAL_CAPACITY 4096
#define UI_TEXT_INITIAL_CAPACITY 2048
#define UI_TEXT_MAX_ATLASES 8

typedef struct {
    SDL_Vertex *vertices;
    int count;
    int capacity;
} GeometryBatch;

typedef struct {
    SDL_GPUTexture *atlas;
    int start_vertex;
    int vertex_count;
    int start_index;
    int index_count;
} TextAtlasRange;

typedef struct {
    float *vertices; // Interleaved: x, y, u, v (4 floats per vertex)
    int *indices;
    int vertex_count;
    int index_count;
    int vertex_capacity;
    int index_capacity;
    TextAtlasRange atlases[UI_TEXT_MAX_ATLASES];
    int atlas_count;
    SDL_FColor current_color;
} TextBatch;

// Global batch state
static GeometryBatch g_geometry = {};
static TextBatch g_text = {};
static UIBatchStats g_last_stats = {0};

// ============================================================================
// Internal Helpers
// ============================================================================

static void geometry_ensure_capacity(const int additional) {
    const int needed = g_geometry.count + additional;
    if (needed <= g_geometry.capacity)
        return;

    int new_capacity = g_geometry.capacity == 0 ? UI_GEOMETRY_INITIAL_CAPACITY : g_geometry.capacity * 2;
    while (new_capacity < needed)
        new_capacity *= 2;

    g_geometry.vertices = SDL_realloc(g_geometry.vertices, sizeof(SDL_Vertex) * (size_t)new_capacity);
    g_geometry.capacity = new_capacity;
}

static void text_ensure_capacity(const int add_verts, const int add_indices) {
    // Vertices
    const int needed_v = g_text.vertex_count + add_verts;
    if (needed_v > g_text.vertex_capacity) {
        int new_cap = g_text.vertex_capacity == 0 ? UI_TEXT_INITIAL_CAPACITY : g_text.vertex_capacity * 2;
        while (new_cap < needed_v)
            new_cap *= 2;
        g_text.vertices = SDL_realloc(g_text.vertices, sizeof(float) * 4 * (size_t)new_cap);
        g_text.vertex_capacity = new_cap;
    }

    // Indices
    const int needed_i = g_text.index_count + add_indices;
    if (needed_i > g_text.index_capacity) {
        int new_cap = g_text.index_capacity == 0 ? UI_TEXT_INITIAL_CAPACITY : g_text.index_capacity * 2;
        while (new_cap < needed_i)
            new_cap *= 2;
        g_text.indices = SDL_realloc(g_text.indices, sizeof(int) * (size_t)new_cap);
        g_text.index_capacity = new_cap;
    }
}

static TextAtlasRange *text_get_atlas_range(SDL_GPUTexture *const atlas) {
    // Look for existing range with this atlas
    for (int i = 0; i < g_text.atlas_count; i++) {
        if (g_text.atlases[i].atlas == atlas)
            return &g_text.atlases[i];
    }

    // Create new range
    if (g_text.atlas_count >= UI_TEXT_MAX_ATLASES) {
        SDL_Log("Warning: UI text batch exceeded max atlas count!");
        return nullptr;
    }

    TextAtlasRange *range = &g_text.atlases[g_text.atlas_count++];
    range->atlas = atlas;
    range->start_vertex = g_text.vertex_count;
    range->vertex_count = 0;
    range->start_index = g_text.index_count;
    range->index_count = 0;
    return range;
}

// ============================================================================
// Public API - Lifecycle
// ============================================================================

void UI_Init(void) {
    // Pre-allocate some capacity to avoid early reallocs
    geometry_ensure_capacity(UI_GEOMETRY_INITIAL_CAPACITY);
    text_ensure_capacity(UI_TEXT_INITIAL_CAPACITY, UI_TEXT_INITIAL_CAPACITY);

    g_text.current_color = (SDL_FColor){1.0f, 1.0f, 1.0f, 1.0f};
}

void UI_Shutdown(void) {
    if (g_geometry.vertices) {
        SDL_free(g_geometry.vertices);
        g_geometry.vertices = nullptr;
        g_geometry.count = 0;
        g_geometry.capacity = 0;
    }
    if (g_text.vertices) {
        SDL_free(g_text.vertices);
        g_text.vertices = nullptr;
    }
    if (g_text.indices) {
        SDL_free(g_text.indices);
        g_text.indices = nullptr;
    }
    g_text.vertex_count = 0;
    g_text.index_count = 0;
    g_text.vertex_capacity = 0;
    g_text.index_capacity = 0;
    g_text.atlas_count = 0;
}

// ============================================================================
// Public API - Geometry Primitives
// ============================================================================

void UI_FillRect(const float x, const float y, const float w, const float h, const SDL_FColor color) {
    geometry_ensure_capacity(6);
    SDL_Vertex *v = g_geometry.vertices + g_geometry.count;

    // Two triangles for a quad
    v[0] = (SDL_Vertex){{x, y}, color, {0, 0}};
    v[1] = (SDL_Vertex){{x + w, y}, color, {0, 0}};
    v[2] = (SDL_Vertex){{x, y + h}, color, {0, 0}};
    v[3] = (SDL_Vertex){{x + w, y}, color, {0, 0}};
    v[4] = (SDL_Vertex){{x + w, y + h}, color, {0, 0}};
    v[5] = (SDL_Vertex){{x, y + h}, color, {0, 0}};

    g_geometry.count += 6;
}

void UI_RectOutline(
    const float x, const float y, const float w, const float h, const SDL_FColor color, const float thickness) {
    // Four thin rectangles for the outline
    UI_FillRect(x, y, w, thickness, color);                 // Top
    UI_FillRect(x, y + h - thickness, w, thickness, color); // Bottom
    UI_FillRect(x, y, thickness, h, color);                 // Left
    UI_FillRect(x + w - thickness, y, thickness, h, color); // Right
}

void UI_Line(
    const float x1, const float y1, const float x2, const float y2, const SDL_FColor color, const float thickness) {
    // Convert line to thin quad
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float len = SDL_sqrtf(dx * dx + dy * dy);
    if (len < 0.001f)
        return;

    // Perpendicular direction
    const float nx = -dy / len * thickness * 0.5f;
    const float ny = dx / len * thickness * 0.5f;

    geometry_ensure_capacity(6);
    SDL_Vertex *v = g_geometry.vertices + g_geometry.count;

    v[0] = (SDL_Vertex){{x1 + nx, y1 + ny}, color, {0, 0}};
    v[1] = (SDL_Vertex){{x2 + nx, y2 + ny}, color, {0, 0}};
    v[2] = (SDL_Vertex){{x2 - nx, y2 - ny}, color, {0, 0}};
    v[3] = (SDL_Vertex){{x2 - nx, y2 - ny}, color, {0, 0}};
    v[4] = (SDL_Vertex){{x1 - nx, y1 - ny}, color, {0, 0}};
    v[5] = (SDL_Vertex){{x1 + nx, y1 + ny}, color, {0, 0}};

    g_geometry.count += 6;
}

// ============================================================================
// Public API - Text
// ============================================================================

void UI_TextWithBackground(TTF_Text *text, const float x, const float y) {
    // Default: semi-transparent black with 4px padding
    UI_TextWithBackgroundEx(text, x, y, UI_COLOR_BACKGROUND_DEFAULT, 0.0f);
}

void UI_TextWithBackgroundEx(
    TTF_Text *const text, const float x, const float y, const SDL_FColor bg_color, const float padding) {
    if (!text)
        return;

    // Get text dimensions
    int text_w, text_h;
    if (!TTF_GetTextSize(text, &text_w, &text_h)) {
        // Fallback if size query fails
        text_w = 100;
        text_h = 24;
    }

    // Draw background rect (will be rendered before text due to batch order)
    UI_FillRect(x - padding, y - padding, (float)text_w + padding * 2.0f, (float)text_h + padding * 2.0f, bg_color);

    // Queue text
    UI_Text(text, x, y);
}

void UI_Text(TTF_Text *const text, const float x, const float y) {
    UI_TextColored(text, x, y, g_text.current_color);
}

void UI_TextColored(TTF_Text *const text, const float x, const float y, const SDL_FColor color) {
    (void)color; // TODO: Per-text coloring would require shader changes

    if (!text)
        return;

    const TTF_GPUAtlasDrawSequence *seq = TTF_GetGPUTextDrawData(text);

    while (seq) {
        TextAtlasRange *range = text_get_atlas_range(seq->atlas_texture);
        if (!range) {
            seq = seq->next;
            continue;
        }

        text_ensure_capacity(seq->num_vertices, seq->num_indices);

        // Copy vertex data with position offset and Y flip
        float *dst = g_text.vertices + g_text.vertex_count * 4;
        for (int i = 0; i < seq->num_vertices; i++) {
            dst[i * 4 + 0] = seq->xy[i].x + x;
            dst[i * 4 + 1] = -seq->xy[i].y + y; // Flip Y for screen-space
            dst[i * 4 + 2] = seq->uv[i].x;
            dst[i * 4 + 3] = seq->uv[i].y;
        }

        // Copy indices with offset adjustment
        const int base_vertex = g_text.vertex_count;
        int *idx_dst = g_text.indices + g_text.index_count;
        for (int i = 0; i < seq->num_indices; i++) {
            idx_dst[i] = seq->indices[i] + base_vertex;
        }

        g_text.vertex_count += seq->num_vertices;
        g_text.index_count += seq->num_indices;
        range->vertex_count += seq->num_vertices;
        range->index_count += seq->num_indices;

        seq = seq->next;
    }
}

// ============================================================================
// Public API - Flush
// ============================================================================

void UI_Flush(void) {
    // Record stats before flushing
    g_last_stats.geometry_vertices = g_geometry.count;
    g_last_stats.geometry_draw_calls = (g_geometry.count > 0) ? 1 : 0;
    g_last_stats.text_vertices = g_text.vertex_count;
    g_last_stats.text_indices = g_text.index_count;
    g_last_stats.text_atlas_count = g_text.atlas_count;
    g_last_stats.text_draw_calls = g_text.atlas_count;

    // Flush geometry batch
    if (g_geometry.count > 0) {
        Renderer_FlushUIGeometry(g_geometry.vertices, g_geometry.count);
        g_geometry.count = 0;
    }

    // Flush text batch
    if (g_text.vertex_count > 0) {
        // Build atlas info for renderer
        UITextAtlasInfo atlas_info[UI_TEXT_MAX_ATLASES];
        for (int i = 0; i < g_text.atlas_count; i++) {
            atlas_info[i].atlas = g_text.atlases[i].atlas;
            atlas_info[i].start_index = g_text.atlases[i].start_index;
            atlas_info[i].index_count = g_text.atlases[i].index_count;
        }

        Renderer_FlushUIText(
            g_text.vertices, g_text.vertex_count, g_text.indices, g_text.index_count, atlas_info, g_text.atlas_count);

        g_text.vertex_count = 0;
        g_text.index_count = 0;
        g_text.atlas_count = 0;
    }
}

UIBatchStats UI_GetStats(void) {
    return g_last_stats;
}
