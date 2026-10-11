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

TEST_CASE("particle emitter shapes and angular motion remain deterministic") {
    auto params=dust_emitter();
    params.count=200;
    params.lifetime_min=params.lifetime_max=100.0f;
    params.shape=chronon3d::EmitterShape::Edge;
    params.velocity_min=params.velocity_max={0.0f,0.0f};
    params.gravity=0.0f; params.turbulence=0.0f;
    params.rotation_min=params.rotation_max=0.25f;
    params.angular_velocity_min=params.angular_velocity_max=2.0f;
    params.sprite_scale_min=params.sprite_scale_max=1.5f;
    const auto edge=chronon3d::sample_particles(params,0.1);
    const auto again=chronon3d::sample_particles(params,0.1);
    REQUIRE(edge.ok()); REQUIRE(again.ok());
    for(std::size_t i=0;i<edge.particles.size();++i) {
        const auto& p=edge.particles[i]; const auto& q=again.particles[i];
        CHECK(p.position.x==q.position.x); CHECK(p.rotation==q.rotation);
        CHECK(p.angular_velocity==2.0f); CHECK(p.sprite_scale==1.5f);
        const bool on_vertical=std::abs(std::abs(p.position.x)-params.emitter_size.x*0.5f)<1.0e-3f;
        const bool on_horizontal=std::abs(std::abs(p.position.y)-params.emitter_size.y*0.5f)<1.0e-3f;
        CHECK((on_vertical || on_horizontal));
    }
    const auto later=chronon3d::sample_particles(params,0.2);
    REQUIRE(later.ok());
    for(std::size_t i=0;i<edge.particles.size();++i) {
        if(later.particles[i].age_seconds>=edge.particles[i].age_seconds)
            CHECK(later.particles[i].rotation-edge.particles[i].rotation==
                  doctest::Approx(2.0f*(later.particles[i].age_seconds-edge.particles[i].age_seconds)));
    }
}

TEST_CASE("path particle emitter samples by polyline arc length") {
    chronon3d::ParticleEmitterParams params;
    params.count=1000; params.seed=73; params.lifetime_min=params.lifetime_max=100.0f;
    params.shape=chronon3d::EmitterShape::Path; params.path_id="source-path";
    params.path_points={{0,0},{10,0},{10,90}};
    const auto result=chronon3d::sample_particles(params,0.0);
    REQUIRE(result.ok());
    std::size_t on_short_segment=0;
    for(const auto& particle:result.particles)
        if(particle.position.y==doctest::Approx(0.0f)) ++on_short_segment;
    CHECK(on_short_segment>60);
    CHECK(on_short_segment<140);
}

TEST_CASE("environment particles keep bounded deterministic depth and bokeh") {
    auto params = dust_emitter();
    params.count = 96;
    params.environment = true;
    params.depth_min = 0.25f;
    params.depth_max = 0.75f;
    params.bokeh = true;
    const auto first = chronon3d::sample_particles(params, 2.5);
    const auto repeated = chronon3d::sample_particles(params, 2.5);
    REQUIRE(first.ok());
    REQUIRE(repeated.ok());
    REQUIRE(first.particles.size() == repeated.particles.size());
    for (std::size_t index = 0; index < first.particles.size(); ++index) {
        const auto& a = first.particles[index];
        const auto& b = repeated.particles[index];
        CHECK(a.depth == b.depth);
        CHECK(a.depth >= params.depth_min);
        CHECK(a.depth <= params.depth_max);
        CHECK(a.size == b.size);
        CHECK(a.color.a == b.color.a);
        CHECK(a.size >= params.size_min * 0.825f);
        CHECK(a.size <= params.size_max * 1.175f);
    }

    params.count = 4097;
    CHECK(chronon3d::validate_particle_emitter(params) ==
          "environment particle count exceeds the 4,096 particle budget");

    params.count = 4096;
    const auto full_budget = chronon3d::sample_particles(params, 3.25);
    const auto intervening_frame = chronon3d::sample_particles(params, 1.5);
    const auto full_budget_again = chronon3d::sample_particles(params, 3.25);
    REQUIRE(full_budget.ok());
    REQUIRE(intervening_frame.ok());
    REQUIRE(full_budget_again.ok());
    REQUIRE(full_budget.particles.size() == 4096);
    REQUIRE(intervening_frame.particles.size() == full_budget.particles.size());
    CHECK(full_budget.particles.size() * sizeof(chronon3d::ParticleSample) <=
          64ULL * 1024ULL * 1024ULL);
    for (std::size_t index = 0; index < full_budget.particles.size(); ++index) {
        const auto& a = full_budget.particles[index];
        const auto& b = full_budget_again.particles[index];
        CHECK(a.index == b.index);
        CHECK(a.position.x == b.position.x);
        CHECK(a.position.y == b.position.y);
        CHECK(a.velocity.x == b.velocity.x);
        CHECK(a.velocity.y == b.velocity.y);
        CHECK(a.color.r == b.color.r);
        CHECK(a.color.g == b.color.g);
        CHECK(a.color.b == b.color.b);
        CHECK(a.size == b.size);
        CHECK(a.color.a == b.color.a);
        CHECK(a.age_seconds == b.age_seconds);
        CHECK(a.life_fraction == b.life_fraction);
        CHECK(a.rotation == b.rotation);
        CHECK(a.angular_velocity == b.angular_velocity);
        CHECK(a.sprite_scale == b.sprite_scale);
        CHECK(a.depth == b.depth);
        CHECK(a.depth >= params.depth_min);
        CHECK(a.depth <= params.depth_max);
    }

    params.count = 0;
    const auto empty = chronon3d::sample_particles(params, 3.25);
    REQUIRE(empty.ok());
    CHECK(empty.particles.empty());
}
