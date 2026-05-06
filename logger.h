//
// Created by Arnau Sanz on 30/1/26.
//

#ifndef MISO_LOGGER_H
#define MISO_LOGGER_H

#include "SDL3/SDL_log.h"

#include <stddef.h>
#include <stdio.h>

static inline const char *color_for_priority(const SDL_LogPriority p) {
    switch (p) {
    case SDL_LOG_PRIORITY_ERROR:
        return "\x1b[31m"; // red
    case SDL_LOG_PRIORITY_WARN:
        return "\x1b[33m"; // yellow
    case SDL_LOG_PRIORITY_INFO:
        return "\x1b[32m"; // green
    case SDL_LOG_PRIORITY_DEBUG:
        return "\x1b[36m"; // cyan
    case SDL_LOG_PRIORITY_VERBOSE:
        return "\x1b[90m"; // gray
    default:
        return "\x1b[0m"; // reset
    }
}

static inline const char *name_for_priority(const SDL_LogPriority p) {
    switch (p) {
    case SDL_LOG_PRIORITY_ERROR:
        return "ERROR";
    case SDL_LOG_PRIORITY_WARN:
        return "WARN ";
    case SDL_LOG_PRIORITY_INFO:
        return "INFO ";
    case SDL_LOG_PRIORITY_DEBUG:
        return "DEBUG";
    case SDL_LOG_PRIORITY_VERBOSE:
        return "VERB ";
    default:
        return " LOG ";
    }
}

static inline const char *name_for_category(const int category) {
    switch (category) {
    case SDL_LOG_CATEGORY_APPLICATION:
        return "APP";
    case SDL_LOG_CATEGORY_ERROR:
        return "ERR";
    case SDL_LOG_CATEGORY_ASSERT:
        return "AST";
    case SDL_LOG_CATEGORY_SYSTEM:
        return "SYS";
    case SDL_LOG_CATEGORY_AUDIO:
        return "AUD";
    case SDL_LOG_CATEGORY_VIDEO:
        return "VID";
    case SDL_LOG_CATEGORY_RENDER:
        return "RND";
    case SDL_LOG_CATEGORY_INPUT:
        return "INP";
    case SDL_LOG_CATEGORY_TEST:
        return "TST";
    case SDL_LOG_CATEGORY_GPU:
        return "GPU";
    default:
        return "UNK";
    }
}

static inline void
MyLogOutput(void *userdata, const int category, const SDL_LogPriority priority, const char *const message) {
    (void)userdata;
    const char *const color = color_for_priority(priority);
    const char *const reset = "\x1b[0m";

    fprintf(
        stderr, "%s[%s][%s]%s %s\n", color, name_for_priority(priority), name_for_category(category), reset, message);
}

static inline void LOG_init(void) {
    SDL_SetLogOutputFunction(MyLogOutput, NULL);
}

#endif // MISO_LOGGER_H
