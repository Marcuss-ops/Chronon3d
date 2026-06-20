// ═══════════════════════════════════════════════════════════════════════════
// test_text_path_composer.cpp — Text-on-path composer tests
//
// PR 5 covers:
//   1. Straight line path — clusters placed linearly
//   2. Alignment (Start, Center, End, Justify)
//   3. Reverse path
//   4. Perpendicular vs upright
//   5. Baseline offset
//   6. Overflow (clip)
//   7. Empty / degenerate
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_path_composer.hpp>
#include <chronon3d/text/path_sampler.hpp>
#include <doctest/doctest.h>
#include <glm/glm.hpp>

using namespace chronon3d;

// ── Helpers ──────────────────────────────────────────────────────────────

namespace {

PlacedGlyphRun make_shaped_run(const std::vector<float>& advances) {
    PlacedGlyphRun run;
    run.ascent = 50.0f;
    run.descent = 12.0f;
    run.baseline = 50.0f;
    run.total_height = 62.0f;
    float total = 0.0f;
    for (size_t i = 0; i < advances.size(); ++i) {
        PlacedGlyph g;
        g.glyph_id = static_cast<u32>(i);
        g.x = total;
        g.y = 0.0f;
        g.advance_x = advances[i];
        g.raw_advance_x = advances[i];
        g.byte_offset = i;
        g.byte_len = 1;
        g.cluster = static_cast<u32>(i);
        g.is_cluster_start = true;
        run.glyphs.push_back(g);
        PlacedGlyphRun::Cluster cl;
        cl.start_glyph = i;
        cl.end_glyph = i + 1;
        cl.byte_offset = i;
        cl.byte_len = 1;
        cl.advance = advances[i];
        cl.raw_advance = advances[i];
        run.clusters.push_back(cl);
        total += advances[i];
    }
    run.total_width = total;
    return run;
}

PathShape make_straight_path(Vec2 from, Vec2 to) {
    PathShape p;
    p.commands.push_back(PathCommand::move_to(from));
    p.commands.push_back(PathCommand::line_to(to));
    return p;
}

TextPathSpec make_spec(std::shared_ptr<const PathShape> path) {
    TextPathSpec spec;
    spec.path = path;
    return spec;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Straight line — basic placement
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPath: straight line places clusters linearly") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {200,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({40.0f, 40.0f, 40.0f, 40.0f, 40.0f}); // 5 × 40 = 200

    auto result = compose_text_on_path(shaped, sampler, make_spec(path));
    REQUIRE(result.clusters.size() == 5);

    // First cluster should be near x=20 (centre of first cluster at advance/2)
    CHECK(result.clusters[0].position.x == doctest::Approx(20.0f).epsilon(0.1f));
    // Last cluster near x=180
    CHECK(result.clusters[4].position.x == doctest::Approx(180.0f).epsilon(0.1f));
    // All on y=0 (no baseline offset)
    for (const auto& c : result.clusters) {
        CHECK(c.position.y == doctest::Approx(0.0f));
    }
}

TEST_CASE("TextPath: perpendicular rotation follows tangent") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {100,100}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({50.0f, 50.0f});
    auto spec = make_spec(path);
    spec.perpendicular_to_path = true;

    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 2);

    // Diagonal path at 45°, all clusters should rotate ~45°
    for (const auto& c : result.clusters) {
        CHECK(c.rotation_deg == doctest::Approx(45.0f).epsilon(1.0f));
    }
}

TEST_CASE("TextPath: perpendicular_to_path=false keeps upright") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {100,100}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({50.0f, 50.0f});
    auto spec = make_spec(path);
    spec.perpendicular_to_path = false;

    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 2);
    for (const auto& c : result.clusters) {
        CHECK(c.rotation_deg == doctest::Approx(0.0f));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Alignment
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPath: Center alignment centres text on path") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {300,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({50.0f, 50.0f, 50.0f, 50.0f}); // 200px
    auto spec = make_spec(path);
    spec.alignment = TextPathAlignment::Center;
    // usable = 300, text = 200, start = 0 + (300-200)/2 = 50

    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 4);
    // First cluster centre at 50 + 25 = 75
    CHECK(result.clusters[0].position.x == doctest::Approx(75.0f).epsilon(0.1f));
}

TEST_CASE("TextPath: End alignment places text at path end") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {300,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({50.0f, 50.0f}); // 100px
    auto spec = make_spec(path);
    spec.alignment = TextPathAlignment::End;
    // start = 0 + 300 - 100 = 200

    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 2);
    CHECK(result.clusters[0].position.x == doctest::Approx(225.0f).epsilon(0.1f));
}

TEST_CASE("TextPath: Justify alignment adds tracking between clusters") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {300,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({30.0f, 30.0f, 30.0f}); // 90px
    auto spec = make_spec(path);
    spec.alignment = TextPathAlignment::Justify;
    // usable = 300, text = 90, extra = (300-90)/(3-1) = 105 per gap
    // cluster centres: 15, 30+105+15=150, 30+105+30+105+15=285

    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 3);
    CHECK(result.clusters[0].position.x == doctest::Approx(15.0f).epsilon(0.1f));
    CHECK(result.clusters[2].position.x == doctest::Approx(285.0f).epsilon(0.1f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Margins
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPath: first_margin offsets text from start") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {400,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({40.0f, 40.0f, 40.0f}); // 120px
    auto spec = make_spec(path);
    spec.first_margin = 100.0f;
    // start = 100, cluster centres: 120, 200, 280

    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 3);
    CHECK(result.clusters[0].position.x == doctest::Approx(120.0f).epsilon(0.1f));
}

TEST_CASE("TextPath: margins reduce usable length for alignment") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {400,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({50.0f, 50.0f}); // 100px
    auto spec = make_spec(path);
    spec.first_margin = 100.0f;
    spec.last_margin = 100.0f;
    spec.alignment = TextPathAlignment::Center;
    // usable = 400 - 200 = 200, text = 100, start = 100 + (200-100)/2 = 150

    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 2);
    CHECK(result.clusters[0].position.x == doctest::Approx(175.0f).epsilon(0.1f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Reverse path
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPath: reverse_path flips text direction and rotation") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {300,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({50.0f, 50.0f, 50.0f});
    auto spec = make_spec(path);
    spec.reverse_path = true;

    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 3);
    CHECK(result.clusters[0].position.x > result.clusters[2].position.x);
    // Reversed path on horizontal line: rotation = 180°
    CHECK(result.clusters[0].rotation_deg == doctest::Approx(180.0f).epsilon(1.0f));
}

TEST_CASE("TextPath: reverse_path flips tangent and normal") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {200,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({100.0f});
    auto spec = make_spec(path);
    spec.baseline_offset = 10.0f;
    spec.reverse_path = true;

    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 1);
    // Normal is flipped by reverse_path, so baseline_offset direction changes
    // Normal without reverse: (tangent.y, -tangent.x) = (0, -1) for tangent (1,0)
    // With reverse: tangent=(-1,0), normal = -sample.normal = -(0,-1) = (0,1)
    // Position = sample + (0,1)*10 = sample + (0,10)
    // Sample at mid-cluster: distance=50 (reversed: 200-50=150), so position=(150,0)
    // Result: (150, 10)
    CHECK(result.clusters[0].position.y == doctest::Approx(10.0f).epsilon(0.1f));
    CHECK(result.clusters[0].position.x == doctest::Approx(150.0f).epsilon(0.1f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Baseline offset
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPath: baseline_offset shifts perpendicular to path") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {200,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({100.0f});
    auto spec = make_spec(path);
    spec.baseline_offset = 15.0f;
    // tangent(1,0), normal(0,-1), position = sample + (0,-1)*15 = (x, -15)

    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 1);
    CHECK(result.clusters[0].position.y == doctest::Approx(-15.0f).epsilon(0.1f));
}

TEST_CASE("TextPath: flip_normal negates baseline_offset direction") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {200,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({100.0f});
    auto spec = make_spec(path);
    spec.baseline_offset = 15.0f;
    spec.flip_normal = true;

    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 1);
    CHECK(result.clusters[0].position.y == doctest::Approx(15.0f).epsilon(0.1f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Overflow
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPath: Clip overflow drops clusters beyond path end") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {100,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({50.0f, 50.0f, 50.0f}); // 150px > 100
    auto spec = make_spec(path);
    spec.overflow = TextPathOverflow::Clip;

    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 3);
    // First 2 clusters fit, third is clipped (position=0,0)
    CHECK(result.clusters[0].position.x > 0.0f);
    CHECK(result.clusters[1].position.x > 0.0f);
    CHECK(result.clusters[2].position.x == doctest::Approx(0.0f));
    CHECK(result.clusters[2].position.y == doctest::Approx(0.0f));
}

TEST_CASE("TextPath: Continue overflow extrapolates past path end") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {100,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({50.0f, 50.0f, 50.0f}); // 150px > 100
    auto spec = make_spec(path);
    spec.overflow = TextPathOverflow::Continue;

    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 3);
    // Third cluster should be beyond x=100 (extrapolated)
    CHECK(result.clusters[2].position.x > 100.0f);
}

TEST_CASE("TextPath: WrapClosedPath wraps clusters past path end") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {200,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({100.0f, 100.0f, 100.0f}); // 300px > 200
    auto spec = make_spec(path);
    spec.overflow = TextPathOverflow::WrapClosedPath;
    // Cluster centres: 50, 150, 250. 250 wraps → 250-200=50.
    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 3);
    // Third cluster wraps to ~50
    CHECK(result.clusters[2].position.x == doctest::Approx(50.0f).epsilon(0.1f));
}

TEST_CASE("TextPath: force_alignment squishes text to fit path") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {120,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({60.0f, 60.0f, 60.0f}); // 180px > 120
    auto spec = make_spec(path);
    spec.alignment = TextPathAlignment::Justify;
    spec.force_alignment = true;
    // Without force_alignment: text=180 > usable=120, no tracking → overflow.
    // With force_alignment=true: extra_tracking = (120-180)/2 = -30.
    // Cursor: 0→30→60→30→90→60→120.  Last cluster center = 120-30=90.
    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 3);
    CHECK(result.clusters[2].position.x == doctest::Approx(90.0f).epsilon(0.1f));
}

TEST_CASE("TextPath: without force_alignment Justify only spreads when text fits") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {120,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({60.0f, 60.0f, 60.0f}); // 180px > 120
    auto spec = make_spec(path);
    spec.alignment = TextPathAlignment::Justify;
    spec.overflow = TextPathOverflow::Continue;  // don't clip — we want to see it overflow
    spec.force_alignment = false;
    // Without force_alignment, text is wider than usable → no tracking.
    // Cursor: 0→60→120→180.  Last cluster center = 180-30=150 (> path end).
    auto result = compose_text_on_path(shaped, sampler, spec);
    REQUIRE(result.clusters.size() == 3);
    CHECK(result.clusters[2].position.x == doctest::Approx(150.0f).epsilon(0.1f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. Empty / degenerate
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPath: empty shaped run returns empty result") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {100,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({});

    auto result = compose_text_on_path(shaped, sampler, make_spec(path));
    CHECK(result.clusters.empty());
}

TEST_CASE("TextPath: zero-length path returns empty result") {
    PathShape empty_path;
    auto path = std::make_shared<PathShape>(empty_path);
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({50.0f});

    auto result = compose_text_on_path(shaped, sampler, make_spec(path));
    CHECK(result.clusters.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. Determinism
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPath: same input produces same output") {
    auto path = std::make_shared<PathShape>(make_straight_path({0,0}, {300,0}));
    auto sampler = PathSampler::compile(*path);
    auto shaped = make_shaped_run({40.0f, 50.0f, 60.0f});
    auto spec = make_spec(path);
    spec.alignment = TextPathAlignment::Center;
    spec.baseline_offset = 10.0f;

    auto a = compose_text_on_path(shaped, sampler, spec);
    auto b = compose_text_on_path(shaped, sampler, spec);

    REQUIRE(a.clusters.size() == b.clusters.size());
    for (size_t i = 0; i < a.clusters.size(); ++i) {
        CHECK(a.clusters[i].position.x == doctest::Approx(b.clusters[i].position.x));
        CHECK(a.clusters[i].position.y == doctest::Approx(b.clusters[i].position.y));
        CHECK(a.clusters[i].rotation_deg == doctest::Approx(b.clusters[i].rotation_deg));
    }
}
