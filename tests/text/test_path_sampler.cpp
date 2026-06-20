// ═══════════════════════════════════════════════════════════════════════════
// test_path_sampler.cpp — PathSampler geometric tests
//
// PR 4 covers:
//   1. Straight line (LineTo) — correct length, tangent, sampling
//   2. Cubic Bézier curve — arc length approximation, endpoints
//   3. Closed path (Close command + closed flag)
//   4. Empty / degenerate paths
//   5. sample_distance at 0, midpoint, total_length
//   6. Tangent and normal correctness
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/path_sampler.hpp>
#include <doctest/doctest.h>
#include <glm/glm.hpp>

using namespace chronon3d;

// ── Helpers ──────────────────────────────────────────────────────────────

namespace {

PathShape make_line(Vec2 a, Vec2 b) {
    PathShape p;
    p.commands.push_back(PathCommand::move_to(a));
    p.commands.push_back(PathCommand::line_to(b));
    return p;
}

PathShape make_rect(Vec2 origin, float w, float h) {
    PathShape p;
    p.commands.push_back(PathCommand::move_to(origin));
    p.commands.push_back(PathCommand::line_to(Vec2{origin.x + w, origin.y}));
    p.commands.push_back(PathCommand::line_to(Vec2{origin.x + w, origin.y + h}));
    p.commands.push_back(PathCommand::line_to(Vec2{origin.x, origin.y + h}));
    p.closed = true;
    return p;
}

PathShape make_cubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3) {
    PathShape path;
    path.commands.push_back(PathCommand::move_to(p0));
    path.commands.push_back(PathCommand::cubic_to(p1, p2, p3));
    return path;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Straight line
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PathSampler: straight line length") {
    auto path = make_line({0.0f, 0.0f}, {100.0f, 0.0f});
    auto sampler = PathSampler::compile(path, 0.25f);
    CHECK(sampler.total_length() == doctest::Approx(100.0f));
}

TEST_CASE("PathSampler: straight line sample at 0") {
    auto path = make_line({0.0f, 0.0f}, {100.0f, 0.0f});
    auto sampler = PathSampler::compile(path, 0.25f);
    auto s = sampler.sample_distance(0.0f);
    CHECK(s.position.x == doctest::Approx(0.0f));
    CHECK(s.position.y == doctest::Approx(0.0f));
    CHECK(s.tangent.x == doctest::Approx(1.0f));
    CHECK(s.tangent.y == doctest::Approx(0.0f));
}

TEST_CASE("PathSampler: straight line sample at midpoint") {
    auto path = make_line({0.0f, 0.0f}, {100.0f, 0.0f});
    auto sampler = PathSampler::compile(path, 0.25f);
    auto s = sampler.sample_distance(50.0f);
    CHECK(s.position.x == doctest::Approx(50.0f));
    CHECK(s.position.y == doctest::Approx(0.0f));
    CHECK(s.distance == doctest::Approx(50.0f));
    CHECK(s.normalized_t == doctest::Approx(0.5f));
}

TEST_CASE("PathSampler: straight line sample at end") {
    auto path = make_line({0.0f, 0.0f}, {100.0f, 0.0f});
    auto sampler = PathSampler::compile(path, 0.25f);
    auto s = sampler.sample_distance(100.0f);
    CHECK(s.position.x == doctest::Approx(100.0f));
    CHECK(s.position.y == doctest::Approx(0.0f));
    CHECK(s.normalized_t == doctest::Approx(1.0f));
}

TEST_CASE("PathSampler: straight line normal is perpendicular") {
    auto path = make_line({0.0f, 0.0f}, {0.0f, 50.0f});
    auto sampler = PathSampler::compile(path, 0.25f);
    auto s = sampler.sample_distance(25.0f);
    // Tangent = (0, 1), normal = (1, 0) (rotated 90° CW: ty, -tx)
    CHECK(s.tangent.x == doctest::Approx(0.0f));
    CHECK(s.tangent.y == doctest::Approx(1.0f));
    CHECK(s.normal.x == doctest::Approx(1.0f));
    CHECK(s.normal.y == doctest::Approx(0.0f));
    // Normal should be perpendicular to tangent
    CHECK(glm::dot(s.tangent, s.normal) == doctest::Approx(0.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Cubic Bézier
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// 2. Cubic Bézier
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PathSampler: quadratic Bézier endpoints match") {
    PathShape path;
    path.commands.push_back(PathCommand::move_to({0.0f, 0.0f}));
    path.commands.push_back(PathCommand::quadratic_to({100.0f, 200.0f}, {200.0f, 0.0f}));
    auto sampler = PathSampler::compile(path, 0.25f);
    // Arc length > chord (200) because the curve arcs upward.
    CHECK(sampler.total_length() > 200.0f);
    auto s0 = sampler.sample_distance(0.0f);
    CHECK(s0.position.x == doctest::Approx(0.0f));
    auto s1 = sampler.sample_distance(sampler.total_length());
    CHECK(s1.position.x == doctest::Approx(200.0f));
    CHECK(s1.position.y == doctest::Approx(0.0f));
}

TEST_CASE("PathSampler: cubic Bézier endpoints match") {
    auto path = make_cubic(
        {0.0f, 0.0f}, {50.0f, 100.0f}, {150.0f, 100.0f}, {200.0f, 0.0f}
    );
    auto sampler = PathSampler::compile(path, 0.25f);

    // Start
    auto s0 = sampler.sample_distance(0.0f);
    CHECK(s0.position.x == doctest::Approx(0.0f));
    CHECK(s0.position.y == doctest::Approx(0.0f));

    // End
    auto s1 = sampler.sample_distance(sampler.total_length());
    CHECK(s1.position.x == doctest::Approx(200.0f));
    CHECK(s1.position.y == doctest::Approx(0.0f));
}

TEST_CASE("PathSampler: cubic Bézier arc length > chord") {
    auto path = make_cubic(
        {0.0f, 0.0f}, {0.0f, 100.0f}, {100.0f, 100.0f}, {100.0f, 0.0f}
    );
    auto sampler = PathSampler::compile(path, 0.25f);
    // Chord = 100 (diagonal-ish? Actually chord from (0,0) to (100,0) = 100)
    // The curve arcs up, so length > 100.
    CHECK(sampler.total_length() > 100.0f);
    CHECK(sampler.total_length() < 300.0f);  // sanity upper bound
}

TEST_CASE("PathSampler: cubic Bézier tangent at start") {
    // A cubic that starts going straight up
    auto path = make_cubic(
        {0.0f, 0.0f}, {0.0f, 50.0f}, {100.0f, 50.0f}, {100.0f, 0.0f}
    );
    auto sampler = PathSampler::compile(path, 0.25f);
    auto s = sampler.sample_distance(0.0f);
    // Tangent should point roughly upward at start
    CHECK(s.tangent.y > 0.5f);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Closed path
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PathSampler: closed rectangle total length") {
    auto path = make_rect({0.0f, 0.0f}, 100.0f, 50.0f);
    auto sampler = PathSampler::compile(path, 0.25f);
    // Perimeter: 100+50+100+50 = 300
    CHECK(sampler.total_length() == doctest::Approx(300.0f));
}

TEST_CASE("PathSampler: closed path sample at end returns to start") {
    auto path = make_rect({0.0f, 0.0f}, 100.0f, 50.0f);
    auto sampler = PathSampler::compile(path, 0.25f);
    auto s = sampler.sample_distance(sampler.total_length());
    CHECK(s.position.x == doctest::Approx(0.0f));
    CHECK(s.position.y == doctest::Approx(0.0f));
}

TEST_CASE("PathSampler: explicit Close command closes to start") {
    PathShape path;
    path.commands.push_back(PathCommand::move_to({0.0f, 0.0f}));
    path.commands.push_back(PathCommand::line_to({100.0f, 0.0f}));
    path.commands.push_back(PathCommand::line_to({100.0f, 100.0f}));
    path.commands.push_back(PathCommand::close());
    auto sampler = PathSampler::compile(path, 0.25f);
    // Perimeter: 100+100+141.42 (diagonal close from (100,100) to (0,0))
    auto s = sampler.sample_distance(sampler.total_length());
    CHECK(s.position.x == doctest::Approx(0.0f).epsilon(0.01f));
    CHECK(s.position.y == doctest::Approx(0.0f).epsilon(0.01f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Empty / degenerate
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PathSampler: empty path has zero length") {
    PathShape empty;
    auto sampler = PathSampler::compile(empty, 0.25f);
    CHECK(sampler.total_length() == doctest::Approx(0.0f));
    CHECK(sampler.point_count() == 0);
}

TEST_CASE("PathSampler: empty path sample returns origin") {
    PathShape empty;
    auto sampler = PathSampler::compile(empty, 0.25f);
    auto s = sampler.sample_distance(10.0f);
    CHECK(s.position.x == doctest::Approx(0.0f));
    CHECK(s.position.y == doctest::Approx(0.0f));
}

TEST_CASE("PathSampler: single-point path has zero length") {
    PathShape path;
    path.commands.push_back(PathCommand::move_to({50.0f, 50.0f}));
    auto sampler = PathSampler::compile(path, 0.25f);
    CHECK(sampler.total_length() == doctest::Approx(0.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. sample_normalized
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PathSampler: sample_normalized at 0 and 1") {
    auto path = make_line({0.0f, 0.0f}, {200.0f, 0.0f});
    auto sampler = PathSampler::compile(path, 0.25f);

    auto s0 = sampler.sample_normalized(0.0f);
    CHECK(s0.position.x == doctest::Approx(0.0f));

    auto s1 = sampler.sample_normalized(1.0f);
    CHECK(s1.position.x == doctest::Approx(200.0f));
}

TEST_CASE("PathSampler: sample_normalized at 0.5 = sample_distance at half") {
    auto path = make_line({0.0f, 0.0f}, {200.0f, 0.0f});
    auto sampler = PathSampler::compile(path, 0.25f);

    auto sa = sampler.sample_normalized(0.5f);
    auto sb = sampler.sample_distance(sampler.total_length() * 0.5f);

    CHECK(sa.position.x == doctest::Approx(sb.position.x));
    CHECK(sa.distance == doctest::Approx(sb.distance));
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Multiple MoveTo commands (disconnected sub-paths)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PathSampler: multiple MoveTo creates disconnected segments") {
    PathShape path;
    path.commands.push_back(PathCommand::move_to({0.0f, 0.0f}));
    path.commands.push_back(PathCommand::line_to({50.0f, 0.0f}));
    path.commands.push_back(PathCommand::move_to({100.0f, 0.0f}));
    path.commands.push_back(PathCommand::line_to({150.0f, 0.0f}));
    auto sampler = PathSampler::compile(path, 0.25f);
    // Total: 50 + 50 = 100
    CHECK(sampler.total_length() == doctest::Approx(100.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. Tolerance affects point count
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PathSampler: looser tolerance produces fewer points") {
    auto path = make_cubic(
        {0.0f, 0.0f}, {0.0f, 200.0f}, {200.0f, 200.0f}, {200.0f, 0.0f}
    );
    auto tight = PathSampler::compile(path, 0.1f);
    auto loose = PathSampler::compile(path, 10.0f);
    CHECK(tight.point_count() >= loose.point_count());
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. Determinism
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("PathSampler: same input produces same output") {
    auto path = make_cubic({0,0}, {50,100}, {150,100}, {200,0});
    auto a = PathSampler::compile(path, 0.25f);
    auto b = PathSampler::compile(path, 0.25f);

    CHECK(a.total_length() == doctest::Approx(b.total_length()));
    CHECK(a.point_count() == b.point_count());

    for (float t = 0.0f; t <= 1.0f; t += 0.1f) {
        auto sa = a.sample_normalized(t);
        auto sb = b.sample_normalized(t);
        CHECK(sa.position.x == doctest::Approx(sb.position.x));
        CHECK(sa.position.y == doctest::Approx(sb.position.y));
    }
}
