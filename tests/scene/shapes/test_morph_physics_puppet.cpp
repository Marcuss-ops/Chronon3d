#include <chronon3d/animation/physics/physics2d.hpp>
#include <chronon3d/scene/model/shape/morph_normalize.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/scene/model/shape/path_operator.hpp>
#include <chronon3d/scene/model/shape/puppet_deform.hpp>
#include <doctest/doctest.h>
#include <cmath>
#include <limits>

using namespace chronon3d;

TEST_CASE("Morph normalize: circle -> rect yields valid morph topology") {
    const auto circle = morph::circle_to_path({0.0f, 0.0f}, 50.0f, 64);
    const auto rect = morph::rect_to_path({-50.0f, -50.0f}, {50.0f, 50.0f}, 64);
    PathShape a, b;
    a.commands = circle.commands;
    a.closed = true;
    b.commands = rect.commands;
    b.closed = true;
    const auto pair = morph::make_morph_compatible_pair(a, b, 64);
    CHECK(pair.source.size() == 65); // MoveTo + 63 LineTo + Close
    CHECK(pair.target.size() == 65);
    CHECK(validate_morph_path_topology(pair.source, pair.target) == std::nullopt);

    // Endpoints: progress 0 == source, 1 == target (existing engine reuse).
    PathShape src;
    src.commands = pair.source;
    src.closed = true;
    MorphPathParams p0{pair.target, 0.0f};
    const auto start =
        apply_path_operators(src, {{PathOperatorKind::Morph, p0, 0}});
    CHECK(start[0].commands[1].p0.x == doctest::Approx(pair.source[1].p0.x));
    MorphPathParams p1{pair.target, 1.0f};
    const auto end = apply_path_operators(src, {{PathOperatorKind::Morph, p1, 0}});
    CHECK(end[0].commands[1].p0.x == doctest::Approx(pair.target[1].p0.x));
    // Midpoint is the average (convex interpolation contract).
    MorphPathParams pm{pair.target, 0.5f};
    const auto mid = apply_path_operators(src, {{PathOperatorKind::Morph, pm, 0}});
    const float expected = (pair.source[1].p0.x + pair.target[1].p0.x) * 0.5f;
    CHECK(mid[0].commands[1].p0.x == doctest::Approx(expected).epsilon(1e-4));
}

TEST_CASE("Morph normalize: triangle -> star-ish polygon + alignment") {
    const auto tri = morph::polygon_to_path({{0.0f, -50.0f}, {50.0f, 40.0f}, {-50.0f, 40.0f}}, 48);
    PathShape b;
    b.commands = {PathCommand::move_to({-40.0f, -40.0f}),
                  PathCommand::line_to({40.0f, -40.0f}),
                  PathCommand::line_to({40.0f, 40.0f}),
                  PathCommand::line_to({-40.0f, 40.0f}),
                  PathCommand::close()};
    b.closed = true;
    PathShape a;
    a.commands = tri.commands;
    a.closed = true;
    const auto pair = morph::make_morph_compatible_pair(a, b, 48);
    CHECK(validate_morph_path_topology(pair.source, pair.target) == std::nullopt);
    // Alignment must not be degenerate: target bbox stays near the rect.
    float min_x = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    for (const auto& c : pair.target) {
        if (c.type == PathCommandType::Close)
            continue;
        min_x = std::min(min_x, c.p0.x);
        max_x = std::max(max_x, c.p0.x);
    }
    CHECK(min_x >= -41.0f);
    CHECK(max_x <= 41.0f);
    CHECK_THROWS(([&] { (void)morph::make_morph_compatible_pair(a, b, 2); }()));
    PathShape empty;
    CHECK_THROWS(([&] { (void)morph::normalize_path_for_morph(empty, 32); }()));
}

TEST_CASE("Physics2D: ball falls, bounces on floor, settles") {
    physics2d::PhysicsWorldConfig cfg;
    cfg.gravity = {0.0f, 980.0f};
    cfg.floor_y = 400.0f;
    auto ball = physics2d::PhysicsBody::circle({160.0f, 40.0f}, 20.0f, 1.0f, 0.65f);
    ball.velocity = {60.0f, 0.0f};
    const auto tracks = physics2d::bake_physics_simulation(cfg, {ball}, 2.0, 60.0);
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].position.size() == 121); // frames+1
    // Never tunnels through the floor.
    for (const auto& k : tracks[0].position.keys())
        CHECK(k.value.y <= 400.0f - 20.0f + 1e-3f);
    // Falls first: y increases from the start.
    CHECK(tracks[0].position.keys()[30].value.y >
          tracks[0].position.keys()[0].value.y);
    // Determinism: re-bake is bit-identical.
    const auto again = physics2d::bake_physics_simulation(cfg, {ball}, 2.0, 60.0);
    CHECK(again[0].position.keys()[60].value.x ==
          doctest::Approx(tracks[0].position.keys()[60].value.x));
    // Random access: sampling frame 500-equivalent without re-stepping.
    const Vec2 s = physics2d::sample_baked_position(tracks[0], 60.5);
    const Vec2 k0 = tracks[0].position.keys()[60].value;
    const Vec2 k1 = tracks[0].position.keys()[61].value;
    CHECK(s.x == doctest::Approx((k0.x + k1.x) * 0.5f).epsilon(1e-4));
}

TEST_CASE("Physics2D: circle-circle collision separates, walls hold") {
    physics2d::PhysicsWorldConfig cfg;
    cfg.gravity = {0.0f, 0.0f};
    cfg.wall_left = 0.0f;
    cfg.wall_right = 400.0f;
    auto a = physics2d::PhysicsBody::circle({100.0f, 200.0f}, 20.0f, 1.0f, 0.9f);
    auto b = physics2d::PhysicsBody::circle({300.0f, 200.0f}, 20.0f, 1.0f, 0.9f);
    a.velocity = {200.0f, 0.0f};
    b.velocity = {-200.0f, 0.0f};
    const auto tracks = physics2d::bake_physics_simulation(cfg, {a, b}, 1.0, 60.0);
    REQUIRE(tracks.size() == 2);
    // At the end the circles must not interpenetrate.
    const Vec2 pa = tracks[0].position.keys().back().value;
    const Vec2 pb = tracks[1].position.keys().back().value;
    CHECK(glm::length(pb - pa) >= 40.0f - 1e-2f);
    for (const auto& t : tracks)
        for (const auto& k : t.position.keys()) {
            CHECK(k.value.x >= 20.0f - 1e-3f);
            CHECK(k.value.x <= 380.0f + 1e-3f);
        }
    CHECK_THROWS(([&] { (void)physics2d::bake_physics_simulation(cfg, {}, 1.0, 60.0); }()));
}

TEST_CASE("Puppet: single pin drags neighborhood with falloff") {
    PathShape flag;
    flag.commands = {PathCommand::move_to({0.0f, 0.0f}),
                     PathCommand::line_to({100.0f, 0.0f}),
                     PathCommand::line_to({100.0f, 50.0f}),
                     PathCommand::line_to({0.0f, 50.0f}),
                     PathCommand::close()};
    flag.closed = true;
    puppet::PuppetDeformParams params;
    params.pins.push_back({{100.0f, 25.0f}, {120.0f, 25.0f}, 150.0f, 1.0f});
    const auto out = puppet::apply_puppet_deform(flag, params);
    REQUIRE(out.commands.size() == flag.commands.size());
    // Pinned corner side moves right; far edge barely moves.
    CHECK(out.commands[1].p0.x > 100.0f);
    CHECK(out.commands[1].p0.x <= 120.0f);
    CHECK(out.commands[3].p0.x >= 0.0f);
    CHECK(out.commands[3].p0.x < out.commands[1].p0.x);
    // Command types preserved.
    CHECK(out.commands[4].type == PathCommandType::Close);
    // Empty pins = identity.
    puppet::PuppetDeformParams none;
    const auto same = puppet::apply_puppet_deform(flag, none);
    CHECK(same.commands[1].p0.x == doctest::Approx(100.0f));
    // Rigid snap: vertex exactly at rest follows exactly.
    puppet::PuppetDeformParams snap;
    snap.pins.push_back({{100.0f, 0.0f}, {130.0f, 10.0f}, 0.0f, 1.0f});
    const auto snapped = puppet::apply_puppet_deform(flag, snap);
    CHECK(snapped.commands[1].p0.x == doctest::Approx(130.0f));
    CHECK(snapped.commands[1].p0.y == doctest::Approx(10.0f));
}

TEST_CASE("Puppet: two-bone IK reaches and clamps") {
    const auto sol =
        puppet::solve_two_bone_ik({0.0f, 0.0f}, 100.0f, 80.0f, {120.0f, 40.0f});
    CHECK(!sol.clamped);
    CHECK(glm::length(sol.elbow - Vec2{0.0f, 0.0f}) == doctest::Approx(100.0f).epsilon(1e-4));
    CHECK(glm::length(sol.end - sol.elbow) == doctest::Approx(80.0f).epsilon(1e-4));
    CHECK(sol.end.x == doctest::Approx(120.0f).epsilon(1e-3));
    const auto far =
        puppet::solve_two_bone_ik({0.0f, 0.0f}, 100.0f, 80.0f, {500.0f, 0.0f});
    CHECK(far.clamped);
    CHECK(glm::length(far.end - Vec2{0.0f, 0.0f}) == doctest::Approx(180.0f).epsilon(1e-3));
    CHECK_THROWS(([&] {
        (void)puppet::solve_two_bone_ik({0.0f, 0.0f}, 0.0f, 80.0f, {10.0f, 0.0f});
    }()));
}
