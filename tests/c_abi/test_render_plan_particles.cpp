#include <doctest/doctest.h>

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/scene/model/shape/particles.hpp>

#include <nlohmann/json.hpp>

#include <string>

#include "test_render_plan_shape_helpers.hpp"

namespace {

nlohmann::json particles_layer(nlohmann::json emitter) {
    return {
        {"id", "particles"},
        {"type", "particles"},
        {"position", {0.0, 0.0}},
        {"start_frame", 0},
        {"duration_frames", 12},
        {"particle_emitter", std::move(emitter)}
    };
}

nlohmann::json box_emitter() {
    return {
        {"count", 32},
        {"seed", 7},
        {"emitter_size", {320.0, 180.0}},
        {"velocity_min", {-4.0, -4.0}},
        {"velocity_max", {4.0, 4.0}},
        {"lifetime", {1.0, 2.0}},
        {"size", {1.0, 3.0}}
    };
}

nlohmann::json path_shape_layer() {
    return {
        {"id", "route"},
        {"type", "shape"},
        {"size", {200.0, 100.0}},
        {"position", {0.0, 0.0}},
        {"shape", {{"type", "path"},
                   {"path", nlohmann::json::array({
                       {{"type", "move_to"}, {"point", {-100.0, 0.0}}},
                       {{"type", "line_to"}, {"point", {0.0, -40.0}}},
                       {{"type", "line_to"}, {"point", {100.0, 0.0}}}
                   })}}}
    };
}

nlohmann::json path_emitter(const std::string& path_id) {
    auto emitter = box_emitter();
    emitter["shape"] = "path";
    emitter["path_id"] = path_id;
    // No drift: the samples must sit exactly on the resolved polyline, so any
    // off-path position would be the decoder's fault, not the integrator's.
    emitter["velocity_min"] = {0.0, 0.0};
    emitter["velocity_max"] = {0.0, 0.0};
    emitter["turbulence"] = 0.0;
    emitter["gravity"] = 0.0;
    return emitter;
}

} // namespace

TEST_CASE("V3 particles layer decodes its emitter and samples every particle") {
    const auto plan = render_plan_test::base_plan(nlohmann::json::array({particles_layer(box_emitter())}));
    const auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    if (!decoded) MESSAGE("particles decode failed: " << decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers.size() == 1);
    const auto& layer = decoded->layers[0];
    REQUIRE(layer.particle_emitter.has_value());
    const auto& emitter = *layer.particle_emitter;
    CHECK(emitter.count == 32U);
    CHECK(emitter.seed == 7U);
    CHECK(emitter.size_min == doctest::Approx(1.0f));
    CHECK(emitter.size_max == doctest::Approx(3.0f));
    CHECK(emitter.lifetime_max == doctest::Approx(2.0f));

    // The decoded emitter is directly samplable: the render path calls
    // sample_particles() with exactly this value each frame.
    const auto sampled = chronon3d::sample_particles(emitter, 0.5);
    REQUIRE(sampled.ok());
    CHECK(sampled.particles.size() == emitter.count);
}

TEST_CASE("a path emitter is resolved from the path shape layer it names") {
    // The contract is relational: path_id names another layer, the decoder
    // lifts that layer's inline path commands into path_points, and sampling
    // never re-derives geometry from anywhere else.
    const auto plan = render_plan_test::base_plan(nlohmann::json::array({
        path_shape_layer(),
        particles_layer(path_emitter("route"))
    }));
    const auto decoded = chronon3d::render_plan::decode_render_plan(plan);
    if (!decoded) MESSAGE("path emitter decode failed: " << decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[1].particle_emitter.has_value());
    const auto& emitter = *decoded->layers[1].particle_emitter;
    CHECK(emitter.shape == chronon3d::EmitterShape::Path);
    CHECK(emitter.path_id == "route");
    CHECK(emitter.path_points.size() >= 3U);

    // The resolved polyline is what sampling uses, and it stays on the path.
    const auto sampled = chronon3d::sample_particles(emitter, 1.25);
    REQUIRE(sampled.ok());
    CHECK(sampled.particles.size() == emitter.count);
    for (const auto& particle : sampled.particles) {
        CHECK(particle.position.x >= -100.5f);
        CHECK(particle.position.x <= 100.5f);
        CHECK(particle.position.y >= -40.5f);
        CHECK(particle.position.y <= 0.5f);
    }
}

TEST_CASE("path emitters fail closed when the named source cannot serve them") {
    // No such layer.
    const auto missing = render_plan_test::base_plan(nlohmann::json::array({
        particles_layer(path_emitter("route"))
    }));
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(missing));

    // A layer that exists but is not a path shape.
    auto wrong_kind = render_plan_test::base_plan(nlohmann::json::array({
        {{"id", "route"}, {"type", "shape"}, {"size", {40.0, 40.0}},
         {"shape", {{"type", "rect"}}}},
        particles_layer(path_emitter("route"))
    }));
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(wrong_kind));

    // The sampler-level guard agrees with the decoder: an unresolved path
    // emitter is not samplable.
    chronon3d::ParticleEmitterParams params;
    params.shape = chronon3d::EmitterShape::Path;
    params.count = 4;
    CHECK_FALSE(chronon3d::validate_particle_emitter(params).empty());
    params.path_id = "route";
    params.path_points = {chronon3d::Vec2{0.0f, 0.0f}, chronon3d::Vec2{1.0f, 0.0f}};
    CHECK(chronon3d::validate_particle_emitter(params).empty());
}

TEST_CASE("particles layers fail closed without an emitter payload") {
    auto layer = particles_layer(box_emitter());
    layer.erase("particle_emitter");
    const auto plan = render_plan_test::base_plan(nlohmann::json::array({layer}));
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(plan));

    // An emitter attached to a non-particles layer is a shape/type mismatch.
    auto wrong_type = nlohmann::json{{"id", "shape"}, {"type", "shape"},
                                     {"size", {10.0, 10.0}},
                                     {"shape", {{"type", "rect"}}},
                                     {"particle_emitter", box_emitter()}};
    const auto wrong = render_plan_test::base_plan(nlohmann::json::array({wrong_type}));
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(wrong));
}

TEST_CASE("the particle seed participates in the content fingerprint") {
    const auto first = render_plan_test::base_plan(nlohmann::json::array({particles_layer(box_emitter())}));
    auto changed_emitter = box_emitter();
    changed_emitter["seed"] = 8;
    const auto second = render_plan_test::base_plan(nlohmann::json::array({particles_layer(changed_emitter)}));
    const auto decoded_first = chronon3d::render_plan::decode_render_plan(first);
    const auto decoded_second = chronon3d::render_plan::decode_render_plan(second);
    REQUIRE(decoded_first);
    REQUIRE(decoded_second);
    CHECK(decoded_first->content_fingerprint != decoded_second->content_fingerprint);
}

TEST_CASE("environment bokeh emitters decode and stay inside the particle budget") {
    auto emitter = box_emitter();
    emitter["environment"] = true;
    emitter["bokeh"] = true;
    emitter["depth_range"] = {0.0, 1.0};
    emitter["blend_mode"] = "add";
    const auto ok_plan = render_plan_test::base_plan(nlohmann::json::array({particles_layer(emitter)}));
    const auto decoded = chronon3d::render_plan::decode_render_plan(ok_plan);
    if (!decoded) MESSAGE("environment bokeh decode failed: " << decoded.error().message);
    REQUIRE(decoded);
    const auto& parsed = *decoded->layers[0].particle_emitter;
    CHECK(parsed.environment);
    CHECK(parsed.bokeh);
    CHECK(parsed.depth_max == doctest::Approx(1.0f));

    auto over_budget = emitter;
    over_budget["count"] = 4097;
    const auto rejected = render_plan_test::base_plan(nlohmann::json::array({particles_layer(over_budget)}));
    CHECK_FALSE(chronon3d::render_plan::decode_render_plan(rejected));
}
