// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_completeness/text_determinism.cpp
//
// P1-10: Text Determinism — verifies that identical inputs produce
// identical outputs across multiple renders.
//
// Determinism is critical for cache, resolver, and sampler correctness.
// If the same composition produces different pixel output on consecutive
// renders, golden tests become flaky and cache invalidation breaks.
//
// Cases:
//   1. Same scene rendered 10 times → all hashes identical
//   2. Frame 0 rendered after frames 1,2 → frame 0 still identical
//   3. Different renderer instances → same output
//   4. Determinism with text wrapping (multi-line)
//   5. Determinism with different font sizes
//   6. Visible pixel count is identical across renders
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
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
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

using namespace chronon3d;
using namespace chronon3d::test;
using namespace chronon3d::test::completeness;

namespace {

Composition build_det_composition(
    SoftwareRenderer& renderer,
    std::string_view text = "DETERMINISM TEST",
    float font_size = 96.0f,
    float box_w = 1800.0f,
    float box_h = 400.0f
) {
    return composition(
        {.name = "TextDet/test",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, text, font_size, box_w, box_h](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("det_layer", [&renderer, text, font_size, box_w, box_h](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("det_test", TextRunParams{
                    .text = TextSpec{
                        .content = {.value = std::string{text}},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = font_size
                        },
                        .layout = {
                            .box = {box_w, box_h},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()},
                        .position = {960.0f, 540.0f, 0.0f}
                    }
                }).commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Same scene rendered 10 times → all hashes identical ══════════
TEST_CASE("TextDeterminism 01: same scene 10 times all hashes identical") {
    constexpr int kRuns = 10;
    u64 hashes[kRuns];
    int visibles[kRuns];

    for (int i = 0; i < kRuns; ++i) {
        auto renderer = test::make_renderer();
        auto fb = renderer.render(
            build_det_composition(renderer), Frame{0});
        REQUIRE(fb != nullptr);
        hashes[i] = framebuffer_hash(*fb);
        visibles[i] = count_visible_pixels(*fb);
    }

    for (int i = 1; i < kRuns; ++i) {
        INFO("run 0 hash=", hashes[0], " run ", i, " hash=", hashes[i]);
        CHECK(hashes[i] == hashes[0]);
        CHECK(visibles[i] == visibles[0]);
    }
}

// ═══ Test 2 — Frame 0 after frames 1,2 → still identical ═════════════════
// Renders frames 1, 2, then 0 using the SAME renderer.  Verifies that
// rendering later frames doesn't contaminate frame 0's output (cache
// pollution test).
TEST_CASE("TextDeterminism 02: frame 0 after frames 1 2 is identical") {
    // Baseline: frame 0 on a fresh renderer.
    auto r_baseline = test::make_renderer();
    auto fb_baseline = r_baseline.render(
        build_det_composition(r_baseline), Frame{0});
    REQUIRE(fb_baseline != nullptr);
    const u64 hash_baseline = framebuffer_hash(*fb_baseline);

    // Test: render frames 1, 2, then 0 on a single renderer.
    auto r_test = test::make_renderer();
    auto fb1 = r_test.render(build_det_composition(r_test), Frame{1});
    REQUIRE(fb1 != nullptr);
    auto fb2 = r_test.render(build_det_composition(r_test), Frame{2});
    REQUIRE(fb2 != nullptr);
    auto fb0_after = r_test.render(build_det_composition(r_test), Frame{0});
    REQUIRE(fb0_after != nullptr);

    const u64 hash_after = framebuffer_hash(*fb0_after);
    INFO("baseline hash=", hash_baseline, " after hash=", hash_after);
    CHECK(hash_baseline == hash_after);
}

// ═══ Test 3 — Different renderer instances → same output ══════════════════
TEST_CASE("TextDeterminism 03: different renderer instances same output") {
    constexpr int kRenderers = 5;
    u64 hashes[kRenderers];

    for (int i = 0; i < kRenderers; ++i) {
        auto renderer = test::make_renderer();
        auto fb = renderer.render(
            build_det_composition(renderer), Frame{0});
        REQUIRE(fb != nullptr);
        hashes[i] = framebuffer_hash(*fb);
    }

    for (int i = 1; i < kRenderers; ++i) {
        INFO("renderer 0 hash=", hashes[0], " renderer ", i, " hash=", hashes[i]);
        CHECK(hashes[i] == hashes[0]);
    }
}

// ═══ Test 4 — Determinism with text wrapping (multi-line) ═════════════════
TEST_CASE("TextDeterminism 04: deterministic with text wrapping") {
    constexpr int kRuns = 5;
    u64 hashes[kRuns];

    for (int i = 0; i < kRuns; ++i) {
        auto renderer = test::make_renderer();
        auto fb = renderer.render(
            build_det_composition(renderer,
                "The quick brown fox jumps over the lazy dog. "
                "Pack my box with five dozen liquor jugs.",
                48.0f, 400.0f, 600.0f),
            Frame{0});
        REQUIRE(fb != nullptr);
        hashes[i] = framebuffer_hash(*fb);
    }

    for (int i = 1; i < kRuns; ++i) {
        CHECK(hashes[i] == hashes[0]);
    }
}

// ═══ Test 5 — Determinism with different font sizes ═══════════════════════
TEST_CASE("TextDeterminism 05: deterministic across font sizes") {
    for (float sz : {24.0f, 48.0f, 96.0f, 144.0f}) {
        auto r1 = test::make_renderer();
        auto r2 = test::make_renderer();

        auto fb1 = r1.render(
            build_det_composition(r1, "SIZE TEST", sz), Frame{0});
        auto fb2 = r2.render(
            build_det_composition(r2, "SIZE TEST", sz), Frame{0});

        REQUIRE(fb1 != nullptr);
        REQUIRE(fb2 != nullptr);

        INFO("font_size=", sz);
        CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
    }
}

// ═══ Test 6 — Visible pixel count identical across renders ═════════════════
TEST_CASE("TextDeterminism 06: visible pixel count identical") {
    constexpr int kRuns = 5;
    int visibles[kRuns];

    for (int i = 0; i < kRuns; ++i) {
        auto renderer = test::make_renderer();
        auto fb = renderer.render(
            build_det_composition(renderer), Frame{0});
        REQUIRE(fb != nullptr);
        visibles[i] = count_visible_pixels(*fb);
    }

    for (int i = 1; i < kRuns; ++i) {
        INFO("run 0 visible=", visibles[0], " run ", i, " visible=", visibles[i]);
        CHECK(visibles[i] == visibles[0]);
    }
}
