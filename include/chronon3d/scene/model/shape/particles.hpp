#pragma once

#include <chronon3d/core/random/deterministic_random.hpp>
#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace chronon3d {

enum class EmitterShape : std::uint8_t { Box, Ellipse, Edge, Path };

/// Deterministic divergence-free ambient flow (dust, embers, motes).
///
/// The displacement is the curl of a scalar potential built from three sine
/// waves, so the field preserves density and can never run away: every particle
/// stays within `strength * 1.13` of its ballistic path. The result is a pure
/// function of spawn position, age, and phase, which keeps random-access and
/// distributed renders identical to sequential ones.
struct ParticleFlowParams {
    f32 strength{0.0f}; ///< peak displacement in canvas units; 0 disables the field
    f32 scale{0.012f};  ///< spatial frequency (1/units): larger = finer swirls
    f32 speed{0.35f};   ///< temporal frequency (rad/s)
};

/// Per-particle alpha shimmer over the particle lifetime. `amount == 0` keeps
/// the color ramp untouched; `amount == 1` can fade a mote fully out at the
/// trough. Speed is in cycles per second.
struct ParticleTwinkleParams {
    f32 amount{0.0f}; ///< [0, 1]
    f32 speed{0.75f};
};

/// Sample the divergence-free ambient flow displacement for one particle.
/// `phase` decorrelates neighbouring particles; pass a deterministic value.
[[nodiscard]] inline Vec2 particle_flow_displacement(
    Vec2 origin, f32 age_seconds, f32 phase, f32 depth,
    const ParticleFlowParams& flow) noexcept {
    if (flow.strength == 0.0f) return Vec2{0.0f, 0.0f};
    const f32 k = flow.scale;
    const f32 t = age_seconds * flow.speed + phase;
    const f32 z = depth * 3.0f + phase * 0.37f;
    // Three-component sine potential in a pseudo-3D space. The returned
    // vector is the (x, y) part of curl(Psi), so the field is divergence-free
    // in the plane and particles swirl without clumping into singularities.
    const f32 ax = k * (0.90f * origin.y + 0.35f * z) + 0.87f * t + phase;
    const f32 ay = k * (0.80f * z - 0.45f * origin.x) + 1.13f * t + 1.70f * phase;
    const f32 az = k * (0.70f * origin.x - 0.55f * origin.y) + 1.03f * t + 0.61f * phase;
    const f32 curl_x = -0.55f * std::cos(az) - 0.80f * std::cos(ay);
    const f32 curl_y = 0.35f * std::cos(ax) - 0.70f * std::cos(az);
    return Vec2{flow.strength * curl_x, flow.strength * curl_y};
}

/// Sample an optional over-life curve stored as a grayscale gradient. The red
/// channel is the scalar value; an empty curve means "no modulation".
[[nodiscard]] inline f32 particle_life_value(
    const graphics::GradientDefinition& curve, f32 t,
    f32 fallback = 1.0f) {
    if (curve.color_stops.empty()) return fallback;
    return graphics::sample_gradient(curve, t).r;
}

/// Stateless CPU particle-emitter description. Samples are a pure function
/// of this value, particle index, and absolute time; rendering frame order
/// never affects particle state.
struct ParticleEmitterParams {
    u32 count{0};
    u32 seed{0};
    Vec2 emitter_size{0.0f, 0.0f};
    Vec2 velocity_min{0.0f, 0.0f};
    Vec2 velocity_max{0.0f, 0.0f};
    f32 lifetime_min{1.0f};
    f32 lifetime_max{1.0f};
    graphics::GradientDefinition color_ramp{};
    f32 size_min{1.0f};
    f32 size_max{1.0f};
    f32 gravity{0.0f};
    f32 turbulence{0.0f};
    /// Quadratic aerodynamic drag (1/units). 0 keeps the legacy ballistic
    /// integration; a positive value brakes fast particles into the flow.
    f32 air_drag{0.0f};
    /// HDR multiplier on the colour ramp RGB. Values above 1 make additive
    /// cores bloom without changing the authored [0, 1] ramp.
    f32 emissive{1.0f};
    /// Emitter velocity in canvas units/second. A particle born at an earlier
    /// sub-frame is spawned at `-emitter_velocity * age`, which turns a moving
    /// emitter into a continuous trail instead of a per-frame necklace.
    Vec2 emitter_velocity{0.0f, 0.0f};
    /// Optional over-life multipliers (red channel). Empty means 1.0.
    graphics::GradientDefinition size_over_life{};
    graphics::GradientDefinition opacity_over_life{};
    ParticleFlowParams flow{};
    ParticleTwinkleParams twinkle{};
    EmitterShape shape{EmitterShape::Box};
    std::string path_id;
    std::vector<Vec2> path_points;
    std::string sprite_asset;
    f32 sprite_scale_min{1.0f}, sprite_scale_max{1.0f};
    f32 rotation_min{0.0f}, rotation_max{0.0f};
    f32 angular_velocity_min{0.0f}, angular_velocity_max{0.0f};
    BlendMode blend_mode{BlendMode::Normal};
    // Environment emitters use bounded canonical per-frame state and a
    // normalized fake depth for scale, opacity, and camera DoF.
    bool environment{false};
    f32 depth_min{0.0f}, depth_max{1.0f};
    bool bokeh{false};
};

struct ParticleSample {
    u32 index{0};
    Vec2 position{0.0f, 0.0f};
    Vec2 velocity{0.0f, 0.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    f32 size{1.0f};
    f32 age_seconds{0.0f};
    f32 life_fraction{0.0f};
    f32 rotation{0.0f};
    f32 angular_velocity{0.0f};
    f32 sprite_scale{1.0f};
    f32 depth{0.0f};
};

struct ParticleSampleResult {
    std::vector<ParticleSample> particles;
    std::string error;
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

namespace detail {

[[nodiscard]] inline bool finite_vec2(Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] inline bool finite_color(Color value) noexcept {
    return std::isfinite(value.r) && std::isfinite(value.g) &&
           std::isfinite(value.b) && std::isfinite(value.a);
}

[[nodiscard]] inline std::uint64_t particle_random_index(
    u32 particle, std::int64_t cycle, std::uint64_t slot) noexcept {
    return (static_cast<std::uint64_t>(particle) << 32U) ^
           (static_cast<std::uint64_t>(cycle) * 0x9e3779b97f4a7c15ULL) ^ slot;
}

} // namespace detail

[[nodiscard]] inline std::string validate_particle_emitter(
    const ParticleEmitterParams& params) {
    if (params.count > 1'000'000U)
        return "particle count exceeds the 1,000,000 CPU sampler limit";
    if (!detail::finite_vec2(params.emitter_size) ||
        !detail::finite_vec2(params.velocity_min) ||
        !detail::finite_vec2(params.velocity_max) ||
        !std::isfinite(params.lifetime_min) || !std::isfinite(params.lifetime_max) ||
        !std::isfinite(params.size_min) || !std::isfinite(params.size_max) ||
        !std::isfinite(params.gravity) || !std::isfinite(params.turbulence) ||
        !std::isfinite(params.sprite_scale_min) || !std::isfinite(params.sprite_scale_max) ||
        !std::isfinite(params.rotation_min) || !std::isfinite(params.rotation_max) ||
        !std::isfinite(params.angular_velocity_min) || !std::isfinite(params.angular_velocity_max) ||
        !std::isfinite(params.depth_min) || !std::isfinite(params.depth_max) ||
        !std::isfinite(params.flow.strength) || !std::isfinite(params.flow.scale) ||
        !std::isfinite(params.flow.speed) || !std::isfinite(params.twinkle.amount) ||
        !std::isfinite(params.twinkle.speed) || !std::isfinite(params.air_drag) ||
        !std::isfinite(params.emissive) ||
        !detail::finite_vec2(params.emitter_velocity))
        return "particle parameters must be finite";
    if (params.air_drag < 0.0f || params.emissive < 0.0f || params.emissive > 64.0f)
        return "particle air_drag and emissive must be non-negative (emissive <= 64)";
    const auto valid_life_curve = [](const graphics::GradientDefinition& curve) {
        if (curve.color_stops.empty()) return true;
        if (curve.color_stops.size() < 2) return false;
        for (const auto& stop : curve.color_stops) {
            if (!std::isfinite(stop.position) || stop.position < 0.0f ||
                stop.position > 1.0f || !std::isfinite(stop.color.r) ||
                stop.color.r < 0.0f)
                return false;
        }
        return true;
    };
    if (!valid_life_curve(params.size_over_life) ||
        !valid_life_curve(params.opacity_over_life))
        return "particle over-life curves require finite stops in [0, 1] with non-negative values";
    if (params.emitter_size.x < 0.0f || params.emitter_size.y < 0.0f ||
        params.lifetime_min <= 0.0f || params.lifetime_max < params.lifetime_min ||
        params.size_min < 0.0f || params.size_max < params.size_min ||
        params.sprite_scale_min < 0.0f || params.sprite_scale_max < params.sprite_scale_min ||
        params.rotation_max < params.rotation_min ||
        params.angular_velocity_max < params.angular_velocity_min ||
        params.velocity_max.x < params.velocity_min.x ||
        params.velocity_max.y < params.velocity_min.y || params.turbulence < 0.0f ||
        params.flow.strength < 0.0f || params.flow.scale <= 0.0f || params.flow.speed < 0.0f ||
        params.twinkle.amount < 0.0f || params.twinkle.amount > 1.0f ||
        params.twinkle.speed < 0.0f ||
        params.depth_min < 0.0f || params.depth_max > 1.0f || params.depth_max < params.depth_min)
        return "particle ranges must be ordered and non-negative where required";
    if (params.environment && params.count > 4096U)
        return "environment particle count exceeds the 4,096 particle budget";
    if (params.environment && static_cast<std::uint64_t>(params.count) * sizeof(ParticleSample) > 64ULL * 1024ULL * 1024ULL)
        return "environment particle state exceeds the 64 MiB per-frame budget";
    if (params.shape == EmitterShape::Path && params.path_id.empty())
        return "path emitter requires a path id";
    if (params.shape == EmitterShape::Path && !params.path_points.empty() && params.path_points.size() < 2)
        return "path emitter requires at least two resolved points";
    for (const auto point : params.path_points)
        if (!detail::finite_vec2(point)) return "particle path points must be finite";
    return {};
}

/// Sample all particles at an absolute time in seconds. Each particle loops
/// on its deterministic lifetime; birth pose and velocity vary by cycle.
[[nodiscard]] inline ParticleSampleResult sample_particles(
    const ParticleEmitterParams& params, double seconds) {
    ParticleSampleResult result;
    const auto fail = [&result](const char* message) {
        result.error = message;
        return result;
    };
    if (!std::isfinite(seconds)) return fail("particle sample time must be finite");
    if (const auto error = validate_particle_emitter(params); !error.empty())
        return fail(error.c_str());
    if (params.shape == EmitterShape::Path && params.path_points.size() < 2)
        return fail("path emitter has no resolved path points");

    result.particles.reserve(params.count);
    std::vector<float> path_lengths(params.path_points.size(), 0.0f);
    for (std::size_t i=1; i<params.path_points.size(); ++i) {
        const Vec2 d=params.path_points[i]-params.path_points[i-1];
        path_lengths[i]=path_lengths[i-1]+std::sqrt(d.x*d.x+d.y*d.y);
    }
    const random::Seed seed{params.seed};
    for (u32 index = 0; index < params.count; ++index) {
        const std::uint64_t stable_base = static_cast<std::uint64_t>(index) * 8U;
        const f32 lifetime = random::range(seed, stable_base,
                                            params.lifetime_min, params.lifetime_max);
        const f32 phase = random::unit(seed, stable_base + 1U) * lifetime;
        const double cycles = std::floor((seconds + phase) / lifetime);
        if (cycles < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
            cycles > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
            return fail("particle sample time exceeds the deterministic cycle range");
        const auto cycle = static_cast<std::int64_t>(cycles);
        const double elapsed = seconds + phase - cycles * lifetime;
        const f32 age = static_cast<f32>(std::clamp(elapsed, 0.0, static_cast<double>(lifetime)));
        const auto random_at = [&](std::uint64_t slot) {
            return random::unit(seed, detail::particle_random_index(index, cycle, slot));
        };
        const f32 u = random_at(2U), v = random_at(3U);
        Vec2 spawn{(u-0.5f)*params.emitter_size.x, (v-0.5f)*params.emitter_size.y};
        if (params.shape == EmitterShape::Ellipse) {
            const f32 angle = u * 6.2831853071795864769f, radius = std::sqrt(v);
            spawn = {std::cos(angle)*radius*params.emitter_size.x*0.5f,
                     std::sin(angle)*radius*params.emitter_size.y*0.5f};
        } else if (params.shape == EmitterShape::Edge) {
            const f32 perimeter = 2.0f*(params.emitter_size.x+params.emitter_size.y);
            const f32 distance = u*perimeter;
            if (distance < params.emitter_size.x) spawn = {-params.emitter_size.x*0.5f+distance,-params.emitter_size.y*0.5f};
            else if (distance < params.emitter_size.x+params.emitter_size.y) spawn = {params.emitter_size.x*0.5f,-params.emitter_size.y*0.5f+(distance-params.emitter_size.x)};
            else if (distance < 2.0f*params.emitter_size.x+params.emitter_size.y) spawn = {params.emitter_size.x*0.5f-(distance-params.emitter_size.x-params.emitter_size.y),params.emitter_size.y*0.5f};
            else spawn = {-params.emitter_size.x*0.5f,params.emitter_size.y*0.5f-(distance-2.0f*params.emitter_size.x-params.emitter_size.y)};
        } else if (params.shape == EmitterShape::Path) {
            const float target=u*path_lengths.back();
            const auto upper=std::lower_bound(path_lengths.begin(), path_lengths.end(), target);
            const std::size_t hi=std::clamp<std::size_t>(static_cast<std::size_t>(upper-path_lengths.begin()),1,path_lengths.size()-1);
            const float span=path_lengths[hi]-path_lengths[hi-1];
            const float t=span>0.0f?(target-path_lengths[hi-1])/span:0.0f;
            spawn=params.path_points[hi-1]+(params.path_points[hi]-params.path_points[hi-1])*t;
        }
        // Sub-frame emission: the particle was born earlier in the frame, when
        // a moving emitter was at `spawn - emitter_velocity * age`.
        spawn -= params.emitter_velocity * age;
        const Vec2 velocity{
            random::range(seed, detail::particle_random_index(index, cycle, 4U),
                          params.velocity_min.x, params.velocity_max.x),
            random::range(seed, detail::particle_random_index(index, cycle, 5U),
                          params.velocity_min.y, params.velocity_max.y)};
        const f32 life_fraction = age / lifetime;
        const f32 turbulence_phase_x = random_at(6U) * 6.2831853071795864769f;
        const f32 turbulence_phase_y = random_at(7U) * 6.2831853071795864769f;
        const f32 turbulence_time = age * 6.2831853071795864769f;
        const Vec2 turbulence{
            params.turbulence * std::sin(turbulence_time + turbulence_phase_x),
            params.turbulence * std::sin(turbulence_time * 0.73f + turbulence_phase_y)};
        const f32 depth = random::range(seed,
            detail::particle_random_index(index, cycle, 12U),
            params.depth_min, params.depth_max);
        // Exact solution of dv/dt = -k * v * |v| per axis (quadratic drag).
        // k == 0 reduces exactly to the legacy v * t displacement.
        const auto drag_axis = [](f32 value, f32 k, f32 time) {
            if (k <= 0.0f || value == 0.0f) return value * time;
            return std::copysign(
                std::log1p(k * std::abs(value) * time) / k, value);
        };
        const Vec2 drag_velocity{
            velocity.x / (1.0f + params.air_drag * std::abs(velocity.x) * age),
            velocity.y / (1.0f + params.air_drag * std::abs(velocity.y) * age)};
        Vec2 position = spawn +
            Vec2{drag_axis(velocity.x, params.air_drag, age),
                 drag_axis(velocity.y, params.air_drag, age)} +
            Vec2{0.0f, 0.5f * params.gravity * age * age} + turbulence;
        if (params.flow.strength > 0.0f) {
            // Far motes drift less: the field reads as parallax, not a sheet.
            const f32 flow_phase = random_at(13U) * 6.2831853071795864769f;
            const f32 parallax = params.environment ? 1.0f - 0.25f * depth : 1.0f;
            position = position +
                particle_flow_displacement(spawn, age, flow_phase, depth, params.flow) * parallax;
        }
        f32 size = random::range(seed,
            detail::particle_random_index(index, cycle, 8U),
            params.size_min, params.size_max);
        size *= particle_life_value(params.size_over_life, life_fraction);
        const f32 rotation = random::range(seed, detail::particle_random_index(index, cycle, 9U), params.rotation_min, params.rotation_max);
        const f32 angular_velocity = random::range(seed, detail::particle_random_index(index, cycle, 10U), params.angular_velocity_min, params.angular_velocity_max);
        const f32 sprite_scale = random::range(seed, detail::particle_random_index(index, cycle, 11U), params.sprite_scale_min, params.sprite_scale_max);
        Color color = params.color_ramp.color_stops.empty()
            ? Color{1.0f, 1.0f, 1.0f, 1.0f}
            : graphics::sample_gradient(params.color_ramp, life_fraction);
        color.r *= params.emissive;
        color.g *= params.emissive;
        color.b *= params.emissive;
        color.a *= particle_life_value(params.opacity_over_life, life_fraction);
        if (params.twinkle.amount > 0.0f) {
            const f32 twinkle_phase = random_at(14U) * 6.2831853071795864769f;
            const f32 shimmer = 0.5f + 0.5f * std::sin(
                6.2831853071795864769f * params.twinkle.speed * age + twinkle_phase);
            color.a *= 1.0f - params.twinkle.amount * shimmer;
        }
        if (params.bokeh) {
            const f32 depth_scale = 1.35f - depth * 0.7f;
            size *= depth_scale;
            // Energy conservation: the same luminous flux is spread over the
            // larger bokeh disc, so defocus cannot brighten a particle.
            color.a = std::clamp(color.a / (depth_scale * depth_scale), 0.0f, 1.0f);
        }
        color.a = std::clamp(color.a, 0.0f, 1.0f);
        if (!detail::finite_vec2(position) || !detail::finite_vec2(drag_velocity) ||
            !std::isfinite(size) || !std::isfinite(rotation) || !std::isfinite(angular_velocity) ||
            !std::isfinite(sprite_scale) || !std::isfinite(depth) ||
            !detail::finite_color(color))
            return fail("particle integration produced a non-finite sample");
        result.particles.push_back(ParticleSample{
            index, position, drag_velocity, color, size, age, life_fraction,
            rotation + angular_velocity*age, angular_velocity, sprite_scale, depth});
    }
    return result;
}

} // namespace chronon3d
