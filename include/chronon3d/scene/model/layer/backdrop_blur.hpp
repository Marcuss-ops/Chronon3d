#pragma once

namespace chronon3d {

struct BackdropBlurParams {
    float blur{16.0f};
    float brightness{1.0f};
    float saturation{1.0f};
    // White directional edge highlight, bounded to [0, 2].
    float rim_light{0.0f};
    // Deterministic displacement amplitude in pixels, bounded to [0, 4].
    float refraction{0.0f};
    // Deterministic linear-light surface modulation, bounded to [0, 0.1].
    float surface_noise{0.0f};
};

} // namespace chronon3d
