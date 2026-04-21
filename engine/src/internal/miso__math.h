#ifndef MISO__MATH_H
#define MISO__MATH_H

#include <SDL3/SDL_stdinc.h>

static inline float miso__lerpf(const float a, const float b, const float t) {
    return a + (b - a) * t;
}

static inline float miso__exp_decayf(const float current, const float target, const float speed, const float dt) {
    return miso__lerpf(current, target, 1.0f - SDL_expf(-speed * dt));
}

#endif
