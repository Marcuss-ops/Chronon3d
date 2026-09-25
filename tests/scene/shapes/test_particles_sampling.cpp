#include <doctest/doctest.h>

#include <chronon3d/scene/model/shape/particles.hpp>

#include <limits>

namespace {

chronon3d::ParticleEmitterParams dust_emitter() {
    using namespace chronon3d;
    ParticleEmitterParams params;
    params.count = 128;
    params.seed = 4728;
    params.emitter_size = {1920.0f, 1080.0f};
    params.velocity_min = {-8.0f, -20.0f};
    params.velocity_max = {8.0f, -4.0f};
    params.lifetime_min = 3.0f;
    params.lifetime_max = 8.0f;
    params.color_ramp = graphics::GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {{0.0f, Color{0.6f, 0.5f, 0.4f, 0.0f}},
         {1.0f, Color{0.8f, 0.7f, 0.6f, 1.0f}}});
    params.size_min = 2.0f;
    params.size_max = 14.0f;
    params.gravity = 1.5f;
    params.turbulence = 0.35f;
    return params;
}

} // namespace

TEST_CASE("particle sampler is deterministic for absolute time and seed") {
    const auto params = dust_emitter();
    const auto first = chronon3d::sample_particles(params, 3.25);
    const auto repeated = chronon3d::sample_particles(params, 3.25);
    REQUIRE(first.ok());
    REQUIRE(repeated.ok());
    REQUIRE(first.particles.size() == params.count);
    REQUIRE(repeated.particles.size() == first.particles.size());
    for (std::size_t index = 0; index < first.particles.size(); ++index) {
        const auto& a = first.particles[index];
        const auto& b = repeated.particles[index];
        CHECK(a.index == b.index);
        CHECK(a.position.x == b.position.x);
        CHECK(a.position.y == b.position.y);
        CHECK(a.velocity.x == b.velocity.x);
        CHECK(a.velocity.y == b.velocity.y);
        CHECK(a.age_seconds == b.age_seconds);
        CHECK(a.color.r == b.color.r);
        CHECK(a.color.a == b.color.a);
    }
    auto changed_seed = params;
    ++changed_seed.seed;
    const auto different = chronon3d::sample_particles(changed_seed, 3.25);
    REQUIRE(different.ok());
    CHECK(first.particles[0].position.x != different.particles[0].position.x);
}

TEST_CASE("particle sampler supports frame-seek and warm-cold equivalence") {
    const auto params = dust_emitter();
    const auto cold = chronon3d::sample_particles(params, 12.0 / 24.0);
    REQUIRE(cold.ok());
    // Sampling a later timestamp does not mutate any shared emitter state.
    REQUIRE(chronon3d::sample_particles(params, 8.0).ok());
    const auto warm = chronon3d::sample_particles(params, 12.0 / 24.0);
    REQUIRE(warm.ok());
    REQUIRE(warm.particles.size() == cold.particles.size());
    for (std::size_t index = 0; index < cold.particles.size(); ++index) {
        CHECK(warm.particles[index].position.x == cold.particles[index].position.x);
        CHECK(warm.particles[index].position.y == cold.particles[index].position.y);
        CHECK(warm.particles[index].size == cold.particles[index].size);
        CHECK(warm.particles[index].life_fraction == cold.particles[index].life_fraction);
    }
}

TEST_CASE("particle sampler validates ranges before sampling") {
    auto params = dust_emitter();
    params.lifetime_min = 0.0f;
    CHECK_FALSE(chronon3d::sample_particles(params, 0.0).ok());
    params = dust_emitter();
    params.velocity_max.x = params.velocity_min.x - 1.0f;
    CHECK_FALSE(chronon3d::sample_particles(params, 0.0).ok());
    params = dust_emitter();
    CHECK_FALSE(chronon3d::sample_particles(params,
        std::numeric_limits<double>::infinity()).ok());
}

TEST_CASE("particle sampler rejects finite parameters that overflow integration") {
    auto params = dust_emitter();
    params.lifetime_min = params.lifetime_max = 100.0f;
    params.velocity_min = params.velocity_max = chronon3d::Vec2{
        std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    const auto result = chronon3d::sample_particles(params, 99.0);
    CHECK_FALSE(result.ok());
    CHECK(result.error == "particle integration produced a non-finite sample");
}
