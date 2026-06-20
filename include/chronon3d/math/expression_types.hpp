#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <cmath>

namespace chronon3d::math {

/// Carries all context needed for expression evaluation: time, frame, fps,
/// composition dimensions, current layer index, and a cross-layer resolver.
struct ExpressionContext {
    // Time
    double frame{0.0};
    double time{0.0};         // seconds
    double fps{30.0};

    // Composition
    double width{1280.0};
    double height{720.0};
    double num_layers{0.0};

    // Current layer
    double index{0.0};         // 1-based layer index
    double in_point{0.0};      // layer in-point in seconds
    double out_point{0.0};     // layer out-point in seconds
    double width_0{0.0};       // layer source width (pixels)
    double height_0{0.0};      // layer source height (pixels)

    // Current property value (set by AnimatedValue before eval)
    double value{0.0};         // current base value (keyframed)
    double value0{0.0};        // scalar component 0 (same as value for f32)
    double value1{0.0};        // scalar component 1 (for Vec2/Vec3)
    double value2{0.0};        // scalar component 2 (for Vec3)

    /// Cross-layer resolver: given a layer name and a dot-separated property path
    /// (e.g. "transform.opacity"), returns the evaluated value. NaN on failure.
    using LayerPropertyResolver =
        std::function<double(const std::string& layer_name,
                             const std::string& property_path,
                             double time)>;

    LayerPropertyResolver layer_resolver;
};

/// Per-evaluation mutable state (seedRandom, etc.)
struct ExpressionState {
    unsigned int random_seed{0};
    bool seed_random_active{false};

    double posterize_fps{0.0};
    bool posterize_active{false};

    // Cached resolved values for layer("name") lookups within one expression
    // Key: "layer_name::property_path"
    std::unordered_map<std::string, double> layer_cache;
};

namespace expr_detail {

inline constexpr double seeded_random(unsigned int seed) {
    // Simple hash-based PRNG — deterministic for a given seed.
    unsigned int x = seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return static_cast<double>(x % 100000u) / 100000.0;
}

inline constexpr double random_range(double lo, double hi, unsigned int seed) {
    return lo + (hi - lo) * seeded_random(seed);
}

// Wiggle: multi-octave smooth noise, identical to wiggle.hpp but double precision.
inline double wiggle_impl(double freq, double amp, double time, unsigned int seed) {
    if (amp <= 0.0 || freq <= 0.0) return 0.0;

    auto hash_noise = [](int n, unsigned int s) -> double {
        unsigned int x = static_cast<unsigned int>(n) + s;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return static_cast<double>(x % 10000u) / 5000.0 - 1.0;
    };

    auto smooth = [&](double t, unsigned int s) -> double {
        const int i = static_cast<int>(std::floor(t));
        const double frac = t - static_cast<double>(i);
        const double u = frac * frac * (3.0 - 2.0 * frac);
        const double a = hash_noise(i, s);
        const double b = hash_noise(i + 1, s);
        return a + (b - a) * u;
    };

    const double n1 = smooth(time * freq, seed);
    const double n2 = smooth(time * freq * 2.0, seed + 1000u) * 0.5;
    const double n3 = smooth(time * freq * 4.0, seed + 2000u) * 0.25;

    constexpr double kNorm = 1.0 / 1.75;
    return (n1 + n2 + n3) * amp * kNorm;
}

// linear(t, t_min, t_max, v_min, v_max) — AE-style remapping.
inline constexpr double linear_t(double t, double t_min, double t_max, double v_min, double v_max) {
    if (std::abs(t_max - t_min) < 1e-12) return v_min;
    double frac = (t - t_min) / (t_max - t_min);
    frac = std::clamp(frac, 0.0, 1.0);
    return v_min + frac * (v_max - v_min);
}

// ease(t, t_min, t_max, v_min, v_max) — smoothstep remapping.
inline constexpr double ease_t(double t, double t_min, double t_max, double v_min, double v_max) {
    if (std::abs(t_max - t_min) < 1e-12) return v_min;
    double frac = (t - t_min) / (t_max - t_min);
    frac = std::clamp(frac, 0.0, 1.0);
    frac = frac * frac * (3.0 - 2.0 * frac);  // smoothstep
    return v_min + frac * (v_max - v_min);
}

// loopHelper — computes looped time fraction for loopOut/loopIn.
inline double loop_helper(double t, double t_min, double t_max, int num_keyframes,
                          const std::string& type) {
    if (t <= t_min) return t_min;
    if (t_max <= t_min) return t_min;

    const double dur = t_max - t_min;
    double frac = (t - t_min) / dur;

    if (type == "cycle" || type.empty()) {
        frac = frac - std::floor(frac);
    } else if (type == "pingpong") {
        double cycle = std::floor(frac);
        frac = frac - cycle;
        if (static_cast<int>(cycle) % 2 == 1) {
            frac = 1.0 - frac;
        }
    } else if (type == "offset") {
        frac = frac - std::floor(frac);
    } else {
        frac = frac - std::floor(frac);
    }

    return t_min + frac * dur;
}

} // namespace expr_detail

} // namespace chronon3d::math
