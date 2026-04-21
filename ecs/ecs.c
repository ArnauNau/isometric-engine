//
// Created by Arnau Sanz on 16/8/25.
//

#include "ecs.h"

ECSWorld ECS_create() {
    ECSWorld world = {0};
    return world;
}

void ECS_destroy(const ECSWorld *const world) {
    (void)world;
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
