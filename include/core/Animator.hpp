#pragma once

#include <filament/Engine.h>
#include <filament/TransformManager.h>
#include <math/vec3.h>
#include <math/mat4.h>
#include <chrono>
#include <vector>
#include <functional>

namespace graphis3d {

struct Animation {
    utils::Entity entity;
    filament::math::float3 startPos;
    filament::math::float3 endPos;
    float duration; // seconds
    float elapsed = 0.0f;
    bool finished = false;
};

class Animator {
public:
    void animatePosition(utils::Entity entity, 
                         filament::math::float3 endPos, 
                         float duration) {
        // Simple position animation
        // We'd need to store the start position from TransformManager
    }

    void update(filament::Engine& engine, float deltaTime) {
        auto& tcm = engine.getTransformManager();
        // Update all active animations
    }
};

} // namespace graphis3d
