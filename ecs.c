//
// Created by Arnau Sanz on 16/8/25.
//

#include "ecs.h"

#include "camera/camera.h"

ECSWorld ECS_create() {
    ECSWorld world = {0};
    world.cameras = ss_create(sizeof(Camera2D_Component), 2);
    world.smooth_zooms = ss_create(sizeof(SmoothZoom_Component), 2);
    return world;
}

void ECS_destroy(const ECSWorld *const world) {
    ss_destroy(&world->cameras);
    ss_destroy(&world->smooth_zooms);
}

Entity ECS_create_entity(ECSWorld *const world) {
    for (Entity e = 0; e < ENTITY_MAX; e++) {
        if (!world->entities[e]) {
            world->entities[e] = true;
            return e;
        }
    }
    return ENTITY_MAX; // No available entity
}
