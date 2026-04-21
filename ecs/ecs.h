//
// Created by Arnau Sanz on 16/8/25.
//

#ifndef MISO_ECS_H
#define MISO_ECS_H
#include "entity.h"

typedef struct ECSWorld_ {
    bool entities[ENTITY_MAX];
} ECSWorld;

ECSWorld ECS_create();
void ECS_destroy(const ECSWorld *world);
Entity ECS_create_entity(ECSWorld *world);

#endif //MISO_ECS_H
