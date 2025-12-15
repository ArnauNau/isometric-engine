//
// Created by Arnau Sanz
//

#ifndef GAME_CLOCK_H
#define GAME_CLOCK_H

#include <stdbool.h>

typedef struct GameClock {
    float total;      // total elapsed game time in seconds
    float delta;      // frame delta in seconds (respects pause/speed)
    float speed;      // time multiplier: 1.0 = normal, 2.0 = double, etc.
    bool paused;
} GameClock;

// Initialize game clock with default values
static inline GameClock GameClock_create(void) {
    return (GameClock){
        .total = 0.0f,
        .delta = 0.0f,
        .speed = 1.0f,
        .paused = false
    };
}

// Update the game clock with real elapsed time (in seconds)
static inline void GameClock_update(GameClock *clock, float real_dt) {
    if (clock->paused) {
        clock->delta = 0.0f;
    } else {
        clock->delta = real_dt * clock->speed;
        clock->total += clock->delta;
    }
}

// Pause/unpause the game clock
static inline void GameClock_setPaused(GameClock *clock, bool paused) {
    clock->paused = paused;
}

// Toggle pause state
static inline void GameClock_togglePause(GameClock *clock) {
    clock->paused = !clock->paused;
}

// Set time scale (1.0 = normal, 2.0 = double speed, etc.)
static inline void GameClock_setSpeed(GameClock *clock, float speed) {
    clock->speed = speed;
}

#endif //GAME_CLOCK_H
