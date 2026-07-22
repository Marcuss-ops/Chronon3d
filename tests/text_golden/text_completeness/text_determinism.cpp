// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_completeness/text_determinism.cpp
//
// P1-#10: Render Determinism — verifies that the text pipeline produces
// identical output for identical inputs, regardless of render order or
// parallelism.
//
// Cases:
//   1. Same scene 10 times → identical pixel hash
//   2. Frame 0→1→2→then back to 0 → same as original frame 0
//   3. Random frame order → all frames match their sequential counterparts
//   4. Two separate renderers in sequence → same output
//   5. Single renderer reused → consistent across calls
//   6. Different compositions produce different hashes (sanity check)
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/helpers/pixel_assertions.hpp>
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

#include <tbb/global_control.h>

#include <array>
#include <unordered_set>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::test;
using namespace chronon3d::test::completeness;

namespace {

/// Build a composition that varies by frame: frame 0 = small text,
/// frame 10 = medium, frame 20 = large, frame 30 = full pangram.
/// This gives us meaningful frame-to-frame differences to verify
/// determinism across frame transitions.
Composition build_frame_comp(SoftwareRenderer& renderer,
                              std::size_t frame_idx) {
    const char* texts[] = {
        "A",                                           // frame 0
        "SHORT",                                       // frame 10
        "DETERMINISM TEST",                            // frame 20
        "The quick brown fox jumps over the lazy dog", // frame 30
    };
    float sizes[] = { 72.0f, 72.0f, 72.0f, 48.0f };
    int idx;
    if (frame_idx < 10)       idx = 0;
    else if (frame_idx < 20)  idx = 1;
    else if (frame_idx < 30)  idx = 2;
    else                      idx = 3;

    std::string text = texts[idx];
    float font_size = sizes[idx];

    return composition(
        {.name = "TextDeterminism/frameComp",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, text, font_size](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("det_layer", [&renderer, text, font_size](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("det_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = text},.placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}},.font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = font_size
                        },.layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },.appearance = {.color = Color::white()},}
                }).commit();
            });
            return s.build();
        });
}

/// Simple static composition for repeated-render tests.
Composition build_static_comp(SoftwareRenderer& renderer) {
    return composition(
        {.name = "TextDeterminism/static",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("static_layer", [&renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("static_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = "DETERMINISTIC"},.placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}},.font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 96.0f
                        },.layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },.appearance = {.color = Color::white()},}
                }).commit();
            });
            return s.build();
        });
}

/// Alternative composition for "different comps → different hash" check.
Composition build_alt_comp(SoftwareRenderer& renderer) {
    return composition(
        {.name = "TextDeterminism/alt",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("alt_layer", [&renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("alt_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = "DIFFERENT"},.placement = TextPlacement{TextPlacementKind::Absolute, {200.0f, 200.0f}},.font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 48.0f
                        },.layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Left,
                            .vertical_align = VerticalAlign::Top
                        },.appearance = {.color = Color{1.0f, 0.0f, 0.0f, 1.0f}},}
                }).commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Same scene 10 times → identical hash ════════════════════════
TEST_CASE("TextDeterminism 01: 10 renders of same scene produce identical hash") {
    std::unordered_set<std::uint64_t> hashes;
    for (int i = 0; i < 10; ++i) {
        auto renderer = test::make_renderer_shared();
        auto fb = renderer->render(build_static_comp(*renderer), Frame{0});
        REQUIRE(fb != nullptr);
        hashes.insert(framebuffer_hash(*fb));
    }
    INFO("unique hashes across 10 renders: ", hashes.size());
    CHECK(hashes.size() == 1);
}

// ═══ Test 2 — Frame 0→1→2→back to 0 = same as original ══════════════════
TEST_CASE("TextDeterminism 02: frame 0 after round-trip equals original") {
    auto r_orig = test::make_renderer_shared();
    auto r_rt   = test::make_renderer_shared();

    // Original frame 0.
    auto fb_orig = r_orig->render(build_frame_comp(*r_orig, 0), Frame{0});
    REQUIRE(fb_orig != nullptr);
    const auto hash_orig = framebuffer_hash(*fb_orig);

    // Render frame 1, 2 — then back to 0.
    auto fb1 = r_rt->render(build_frame_comp(*r_rt, 1), Frame{1});
    auto fb2 = r_rt->render(build_frame_comp(*r_rt, 2), Frame{2});
    auto fb0_rt = r_rt->render(build_frame_comp(*r_rt, 0), Frame{0});

    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    REQUIRE(fb0_rt != nullptr);

    const auto hash_rt = framebuffer_hash(*fb0_rt);

    INFO("original hash=", hash_orig, " round-trip hash=", hash_rt);

    // Round-trip frame 0 must match original.
    CHECK(hash_rt == hash_orig);

    // Frames 1 and 2 must differ from frame 0 (sanity: frames vary).
    CHECK(framebuffer_hash(*fb1) != hash_orig);
    CHECK(framebuffer_hash(*fb2) != hash_orig);
}

// ═══ Test 3 — Random frame order: all frames match sequential reference ═══
TEST_CASE("TextDeterminism 03: random frame order produces same output") {
    // Sequential reference: render frames 0, 1, 2, 3 in order.
    std::array<std::uint64_t, 4> ref_hashes;
    for (std::size_t f = 0; f < 4; ++f) {
        auto renderer = test::make_renderer_shared();
        auto fb = renderer->render(build_frame_comp(*renderer, f), Frame{f});
        REQUIRE(fb != nullptr);
        ref_hashes[f] = framebuffer_hash(*fb);
    }

    // Random order: render frames 3, 0, 2, 1
    std::array<int, 4> random_order = {3, 0, 2, 1};
    std::array<std::uint64_t, 4> rand_hashes;
    auto r2 = test::make_renderer_shared();
    for (int idx : random_order) {
        auto fb = r2->render(
            build_frame_comp(*r2, static_cast<std::size_t>(idx)),
            Frame{static_cast<std::size_t>(idx)});
        REQUIRE(fb != nullptr);
        rand_hashes[static_cast<std::size_t>(idx)] = framebuffer_hash(*fb);
    }

    // Each frame's random-order hash must match sequential reference.
    for (std::size_t f = 0; f < 4; ++f) {
        INFO("frame=", f, " ref=", ref_hashes[f], " rand=", rand_hashes[f]);
        CHECK(ref_hashes[f] == rand_hashes[f]);
    }
}

// ═══ Test 4 — Two separate renderers produce same output ═════════════════
TEST_CASE("TextDeterminism 04: two separate renderers produce same hash") {
    auto r1 = test::make_renderer_shared();
    auto r2 = test::make_renderer_shared();

    auto fb1 = r1->render(build_static_comp(*r1), Frame{0});
    auto fb2 = r2->render(build_static_comp(*r2), Frame{0});
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    const auto h1 = framebuffer_hash(*fb1);
    const auto h2 = framebuffer_hash(*fb2);

    INFO("renderer1 hash=", h1, " renderer2 hash=", h2);
    CHECK(h1 == h2);
    CHECK(count_visible_pixels(*fb1) == count_visible_pixels(*fb2));
}

// ═══ Test 5 — Single renderer reused → consistent across calls ═══════════
TEST_CASE("TextDeterminism 05: reused renderer produces consistent output") {
    auto renderer = test::make_renderer_shared();

    auto fb1 = renderer->render(build_static_comp(*renderer), Frame{0});
    auto fb2 = renderer->render(build_static_comp(*renderer), Frame{0});
    auto fb3 = renderer->render(build_static_comp(*renderer), Frame{0});

    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    REQUIRE(fb3 != nullptr);

    const auto h1 = framebuffer_hash(*fb1);
    const auto h2 = framebuffer_hash(*fb2);
    const auto h3 = framebuffer_hash(*fb3);

    INFO("call1 hash=", h1, " call2=", h2, " call3=", h3);
    CHECK(h1 == h2);
    CHECK(h2 == h3);
    const int v1 = count_visible_pixels(*fb1);
    const int v2 = count_visible_pixels(*fb2);
    const int v3 = count_visible_pixels(*fb3);
    CHECK(v1 == v2);
    CHECK(v2 == v3);
}

// ═══ Test 6 — Different compositions produce different hashes ═════════════
// Sanity check: determinism means same→same, but different→different.
TEST_CASE("TextDeterminism 06: different compositions give different hashes") {
    auto r1 = test::make_renderer_shared();
    auto r2 = test::make_renderer_shared();

    auto fb_static = r1->render(build_static_comp(*r1), Frame{0});
    auto fb_alt = r2->render(build_alt_comp(*r2), Frame{0});
    REQUIRE(fb_static != nullptr);
    REQUIRE(fb_alt != nullptr);

    // Sanity guard: both compositions must render SOMETHING before we trust
    // the hash comparison.  Without this, two empty framebuffers (e.g. from
    // a setup bug like the deprecated make_renderer() dangling-pointer
    // issue) would produce identical hashes and pass a "different !=
    // different" check by accident, masking real renderer regressions.
    const int vis_static = count_visible_pixels(*fb_static);
    const int vis_alt = count_visible_pixels(*fb_alt);
    INFO("static visible_pixels=", vis_static,
         " alt visible_pixels=", vis_alt);
    CHECK(vis_static > 0);
    CHECK(vis_alt > 0);

    const auto h_static = framebuffer_hash(*fb_static);
    const auto h_alt = framebuffer_hash(*fb_alt);

    INFO("static hash=", h_static, " alt hash=", h_alt);
    CHECK(h_static != h_alt);
}

// ═══ Test 7 — Cold cache vs hot cache produce identical output ═══════════
TEST_CASE("TextDeterminism 07: cold cache vs hot cache yields identical hash") {
    auto renderer = test::make_renderer_shared();

    // First render populates caches.
    auto fb_cold = renderer->render(build_static_comp(*renderer), Frame{0});
    REQUIRE(fb_cold != nullptr);
    const auto hash_cold = framebuffer_hash(*fb_cold);

    // Subsequent render reuses cached layout/raster data.
    auto fb_warm = renderer->render(build_static_comp(*renderer), Frame{0});
    REQUIRE(fb_warm != nullptr);
    const auto hash_warm = framebuffer_hash(*fb_warm);

    CHECK(hash_cold == hash_warm);

    // Invalidate caches and force a rebuild.
    renderer->clear_caches();
    auto fb_rebuilt = renderer->render(build_static_comp(*renderer), Frame{0});
    REQUIRE(fb_rebuilt != nullptr);
    const auto hash_rebuilt = framebuffer_hash(*fb_rebuilt);

    CHECK(hash_cold == hash_rebuilt);
}

// ═══ Test 8 — 1-thread vs 4-thread scheduler produces identical output ═════
TEST_CASE("TextDeterminism 08: serial vs parallel scheduler yields identical hash") {
    auto make_hash = [](int parallelism) -> std::uint64_t {
        tbb::global_control gc(tbb::global_control::max_allowed_parallelism, parallelism);
        auto renderer = test::make_renderer_shared();
        auto fb = renderer->render(build_static_comp(*renderer), Frame{0});
        REQUIRE(fb != nullptr);
        return framebuffer_hash(*fb);
    };

    const auto hash_1t = make_hash(1);
    const auto hash_4t = make_hash(4);

    CHECK(hash_1t == hash_4t);
}

// ══ Test 9 — Sequential vs random frame access produces identical output ═══
TEST_CASE("TextDeterminism 09: sequential vs random frame access yields identical hashes") {
    // Build reference hashes by rendering frames 0..3 sequentially.
    std::array<std::uint64_t, 4> ref_hashes{};
    {
        auto renderer = test::make_renderer_shared();
        for (std::size_t f = 0; f < 4; ++f) {
            auto fb = renderer->render(build_frame_comp(*renderer, f), Frame{f});
            REQUIRE(fb != nullptr);
            ref_hashes[f] = framebuffer_hash(*fb);
        }
    }

    // Render the same frames in random order on a fresh renderer.
    std::array<int, 4> random_order = {2, 0, 3, 1};
    std::array<std::uint64_t, 4> random_hashes{};
    {
        auto renderer = test::make_renderer_shared();
        for (int idx : random_order) {
            auto fb = renderer->render(
                build_frame_comp(*renderer, static_cast<std::size_t>(idx)),
                Frame{static_cast<std::size_t>(idx)});
            REQUIRE(fb != nullptr);
            random_hashes[static_cast<std::size_t>(idx)] = framebuffer_hash(*fb);
        }
    }

    for (std::size_t f = 0; f < 4; ++f) {
        INFO("frame=", f, " ref=", ref_hashes[f], " random=", random_hashes[f]);
        CHECK(ref_hashes[f] == random_hashes[f]);
    }

    // Sanity: frames should actually differ from each other.
    CHECK(ref_hashes[0] != ref_hashes[1]);
    CHECK(ref_hashes[1] != ref_hashes[2]);
    CHECK(ref_hashes[2] != ref_hashes[3]);
}

// ═══ Test 10 — Seek patterns 0→20→40 and 40→0→20 produce identical output ══
TEST_CASE("TextDeterminism 10: seek patterns 0->20->40 and 40->0->20 agree") {
    auto render_sequence = [](const std::vector<std::size_t>& frames)
        -> std::vector<std::uint64_t> {
        auto renderer = test::make_renderer_shared();
        std::vector<std::uint64_t> out;
        out.reserve(frames.size());
        for (std::size_t f : frames) {
            auto fb = renderer->render(
                build_frame_comp(*renderer, f), Frame{f});
            REQUIRE(fb != nullptr);
            out.push_back(framebuffer_hash(*fb));
        }
        return out;
    };

    const auto forward = render_sequence({0, 20, 40});
    const auto reverse = render_sequence({40, 0, 20});

    // forward[0] (frame 0) must match reverse[1] (frame 0).
    CHECK(forward[0] == reverse[1]);
    // forward[1] (frame 20) must match reverse[2] (frame 20).
    CHECK(forward[1] == reverse[2]);
    // forward[2] (frame 40) must match reverse[0] (frame 40).
    CHECK(forward[2] == reverse[0]);

    // Sanity: the three frames should differ from each other.
    CHECK(forward[0] != forward[1]);
    CHECK(forward[1] != forward[2]);
}

// ═══ Test 11 — Visual ink bbox (proxy for layout geometry) is deterministic ══
TEST_CASE("TextDeterminism 11: visual ink bounding box is reproducible") {
    auto renderer = test::make_renderer_shared();

    auto fb1 = renderer->render(build_static_comp(*renderer), Frame{0});
    auto fb2 = renderer->render(build_static_comp(*renderer), Frame{0});
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    const auto bb1 = bounding_box(*fb1, Color::white(), 0.1f);
    const auto bb2 = bounding_box(*fb2, Color::white(), 0.1f);

    INFO("bb1=", bb1.x0, ",", bb1.y0, "-", bb1.x1, ",", bb1.y1);
    INFO("bb2=", bb2.x0, ",", bb2.y0, "-", bb2.x1, ",", bb2.y1);

    CHECK(bb1.empty == bb2.empty);
    CHECK(bb1.x0 == bb2.x0);
    CHECK(bb1.y0 == bb2.y0);
    CHECK(bb1.x1 == bb2.x1);
    CHECK(bb1.y1 == bb2.y1);
    CHECK(bb1.area() > 0);
}
