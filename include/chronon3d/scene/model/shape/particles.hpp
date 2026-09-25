#pragma once

#include <chronon3d/core/random/deterministic_random.hpp>
#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace chronon3d {

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
};

struct ParticleSample {
    u32 index{0};
    Vec2 position{0.0f, 0.0f};
    Vec2 velocity{0.0f, 0.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    f32 size{1.0f};
    f32 age_seconds{0.0f};
    f32 life_fraction{0.0f};
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
        !std::isfinite(params.gravity) || !std::isfinite(params.turbulence))
        return "particle parameters must be finite";
    if (params.emitter_size.x < 0.0f || params.emitter_size.y < 0.0f ||
        params.lifetime_min <= 0.0f || params.lifetime_max < params.lifetime_min ||
        params.size_min < 0.0f || params.size_max < params.size_min ||
        params.velocity_max.x < params.velocity_min.x ||
        params.velocity_max.y < params.velocity_min.y || params.turbulence < 0.0f)
        return "particle ranges must be ordered and non-negative where required";
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

    result.particles.reserve(params.count);
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
        const Vec2 spawn{
            random::range(seed, detail::particle_random_index(index, cycle, 2U),
                          -params.emitter_size.x * 0.5f, params.emitter_size.x * 0.5f),
            random::range(seed, detail::particle_random_index(index, cycle, 3U),
                          -params.emitter_size.y * 0.5f, params.emitter_size.y * 0.5f)};
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
        const Vec2 position = spawn + velocity * age +
            Vec2{0.0f, 0.5f * params.gravity * age * age} + turbulence;
        const f32 size = random::range(seed,
            detail::particle_random_index(index, cycle, 8U),
            params.size_min, params.size_max);
        const Color color = params.color_ramp.color_stops.empty()
            ? Color{1.0f, 1.0f, 1.0f, 1.0f}
            : graphics::sample_gradient(params.color_ramp, life_fraction);
        if (!detail::finite_vec2(position) || !detail::finite_vec2(velocity) ||
            !std::isfinite(size) || !detail::finite_color(color))
            return fail("particle integration produced a non-finite sample");
        result.particles.push_back(ParticleSample{
            index, position, velocity, color, size, age, life_fraction});
    }
    return result;
}

} // namespace chronon3d
