//
// Created by Arnau Sanz on 16/8/25.
//

#ifndef NAU_ENGINE_ECS_H
#define NAU_ENGINE_ECS_H
#include "sparse_set.h"

typedef struct ECSWorld_ {
    SparseSet cameras;          // Camera2D_Component
    SparseSet smooth_zooms;     // SmoothZoom_Component
    bool entities[ENTITY_MAX];
} ECSWorld;

ECSWorld ECS_create();
void ECS_destroy(const ECSWorld *world);
Entity ECS_create_entity(ECSWorld *world);


#endif //NAU_ENGINE_ECS_H
