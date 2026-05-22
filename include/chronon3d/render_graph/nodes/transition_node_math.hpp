#pragma once

namespace chronon3d::graph {

namespace transition_math {

inline float smoothstep01(float val) {
    return val * val * (3.0f - 2.0f * val);
}

float procedural_remotion_mask(float u, float v, float t, float seed);
float remotion_mask(float u, float v, float t, float speed, float direction);
void evaluate_remotion_color(float u, float v, float t, float speed, float direction, float angle,
                             float& out_r, float& out_g, float& out_b);

} // namespace transition_math

} // namespace chronon3d::graph
