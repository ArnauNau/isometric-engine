//
// Created by Arnau Sanz on 16/8/25.
//

#include "camera.h"
#include <SDL3/SDL.h>

constexpr float min_zoom = CAMERA_MIN_ZOOM, max_zoom = CAMERA_MAX_ZOOM;
constexpr float zoom_scale = 0.1f; // scale factor for zooming

void CAMERA_zoom_set(Camera2D_Component *const camera, const float zoom, const SDL_FPoint mouse_position) {
    const SDL_FPoint oldPosition = cam_screen_to_world(&camera->camera, mouse_position.x, mouse_position.y);

    camera->camera.zoom = SDL_clamp(zoom, min_zoom, max_zoom);

    const SDL_FPoint newPosition = cam_screen_to_world(&camera->camera, mouse_position.x, mouse_position.y);

    camera->camera.position.x += (oldPosition.x - newPosition.x)/* * camera.zoom*/;
    camera->camera.position.y += (oldPosition.y - newPosition.y)/* * camera.zoom*/;
}

void CAMERA_pan(Camera2D *const camera, const float dx, const float dy, const float dt) {
    // Movement speed is inversely proportional to zoom:
    // - Zoomed in (high zoom): move slower in world units for fine control
    // - Zoomed out (low zoom): move faster in world units to cover more ground
    const float speed = CAMERA_PAN_SPEED / camera->zoom;
    camera->position.x += dx * speed * dt;
    camera->position.y += dy * speed * dt;
}

void CAMERA_smooth_zoom_system(ECSWorld *const world, const float dt, const SDL_FPoint mouse_position) {
    for (Entity component = 0; component < world->smooth_zooms.size; component++) {
        //iterate dense array, get entity from sparse
        //TODO: make iterating through dense arrays more ergonomic, maybe use a macro or something
        const SmoothZoom_Component *const sz = (SmoothZoom_Component *)((char *)world->smooth_zooms.dense + component * sizeof(SmoothZoom_Component));

        const Entity e = ss_get_entity(&world->smooth_zooms, component);
        Camera2D_Component *camera = ss_get(&world->cameras, e);
        if (camera) {
            const float dir = sz->target_zoom > camera->camera.zoom ? 1.0f : -1.0f;

            const float zoom_variation = sz->speed * camera->camera.zoom * dt * 0.01f;
            const float zoom_diff = SDL_fabsf(sz->target_zoom - camera->camera.zoom);


            if (zoom_variation > zoom_diff) {
                SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Smooth zoom entity %d: reached target_zoom=%.2f", e, sz->target_zoom);
                CAMERA_zoom_set(camera, sz->target_zoom, mouse_position);
            } else {
                SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Smooth zoom entity %d: target_zoom=%.2f speed=%.3f variaton=%.5f", e, sz->target_zoom, sz->speed, zoom_variation);
                CAMERA_zoom_set(camera, camera->camera.zoom + zoom_diff * dir * zoom_scale, mouse_position);
            }

            if ((dir > 0 && camera->camera.zoom >= sz->target_zoom) || (dir < 0 && camera->camera.zoom <= sz->target_zoom)) {
                camera->camera.zoom = sz->target_zoom;
                ss_remove(&world->smooth_zooms, e);
            }
        }
    }
}

void CAMERA_zoom_apply(ECSWorld *const world, const Entity entity, const float zoom_direction, const float speed) {

    if (!ss_has(&world->smooth_zooms, entity)) {
        const Camera2D_Component *const camera = ss_get(&world->cameras, entity);
        const float target_zoom = SDL_clamp(camera->camera.zoom + zoom_scale * zoom_direction, min_zoom, max_zoom);

        SmoothZoom_Component sz = {0};
        sz.target_zoom = target_zoom;
        sz.speed = speed;

        if (target_zoom != camera->camera.zoom) {
            ss_add(&world->smooth_zooms, entity, &sz);
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Added smooth zoom component for entity %d: target_zoom=%.2f, speed=%.4f", entity, sz.target_zoom, sz.speed);
        }
    } else {
        SmoothZoom_Component *const existing_sz = ss_get(&world->smooth_zooms, entity);
        existing_sz->target_zoom = SDL_clamp(existing_sz->target_zoom + zoom_scale * zoom_direction, min_zoom, max_zoom);
        existing_sz->speed = SDL_clamp(speed, 0.001f, 0.5f);
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Updated existing smooth zoom component for entity %d: target_zoom=%.2f, speed=%.4f", entity, existing_sz->target_zoom, existing_sz->speed);
    }
}

void CAMERA_add(ECSWorld *const world, const Entity entity, const Camera2D camera) {
    Camera2D_Component camera_component = {0};
    // camera_component.entity = entity;
    camera_component.camera = camera;

    ss_add(&world->cameras, entity, &camera_component);
}
