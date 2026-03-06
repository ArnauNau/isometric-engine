//
// Created by Arnau Sanz on 1/12/25.
//

#ifndef MISO_UI_H
#define MISO_UI_H

#define UI_COLOR_BACKGROUND_DEFAULT (SDL_FColor){0.0f, 0.0f, 0.0f, 0.6f}

/* ============================================================================
   High-Level UI Rendering API: Batched UI Primitives and Text Rendering
   ============================================================================
 */

// Collects UI draw commands and renders them efficiently with minimal GPU
// calls. All coordinates are screen-space pixels: (0,0) = top-left, Y increases
// downward.
//
// Usage:
//   UI_Text(text, 10, 10);
//   UI_FillRect(100, 100, 50, 50, color);
//   UI_Flush();  // Renders everything in ~2 draw calls
//

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

// Initialize the UI batch system (call once at startup)
void UI_Init(void);

// Shutdown and free resources (call once at cleanup)
void UI_Shutdown(void);

// ============================================================================
// Batch Collection API - Queue draw commands (no GPU work)
// ============================================================================
// Text with background - draws semi-transparent rect behind text
void UI_TextWithBackground(TTF_Text *text, float x, float y);
void UI_TextWithBackgroundEx(TTF_Text *text, float x, float y, SDL_FColor bg_color, float padding);

// Text rendering
void UI_Text(TTF_Text *text, float x, float y);
void UI_TextColored(TTF_Text *text, float x, float y, SDL_FColor color);

// Rectangles
void UI_FillRect(float x, float y, float w, float h, SDL_FColor color);
void UI_RectOutline(float x, float y, float w, float h, SDL_FColor color, float thickness);

// Lines
void UI_Line(float x1, float y1, float x2, float y2, SDL_FColor color, float thickness);

// ============================================================================
// Flush API - Execute all queued commands
// ============================================================================

// Render all queued UI commands with minimal draw calls
// Call once per frame after all UI_* calls
void UI_Flush(void);

// Get statistics for debugging/profiling
typedef struct {
    int geometry_vertices;
    int geometry_draw_calls;
    int text_vertices;
    int text_indices;
    int text_atlas_count;
    int text_draw_calls;
} UIBatchStats;

UIBatchStats UI_GetStats(void);

#endif // MISO_UI_H
