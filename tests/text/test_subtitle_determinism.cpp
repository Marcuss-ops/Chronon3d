// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_subtitle_determinism.cpp
//
// Verifies that the subtitle pipeline produces identical output for
// identical inputs under a variety of cache, threading, and access patterns.
//
// Cases:
//   1. Repeated renders of the same subtitle cue → identical pixel hash
//   2. Cold cache vs warm cache → identical output
//      Cache invalidation + rebuild → identical output
//   4. Two separate renderer instances → identical output
//   5. Single renderer reused → consistent across calls
//   6. 1-thread vs 4-thread scheduler → identical output
//   7. Sequential vs random frame order → identical output
//   8. Seek patterns 0→20→40 and 40→0→20 → identical output
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/presets/text/subtitle.hpp>
#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/helpers/pixel_assertions.hpp>

#include <tbb/global_control.h>

#include <array>
#include <vector>
#include <unordered_set>

using namespace chronon3d;
using namespace chronon3d::test;
using namespace chronon3d::presets::text;

namespace {

/// Build a subtitle track with two cues so we can exercise seek transitions.
presets::text::SubtitleTrack make_test_track() {
    presets::text::SubtitleTrack track;
    track.language = "en";

    SubtitleCue cue1;
    cue1.start_s = 0.0f;
    cue1.end_s = 2.0f;
    cue1.text = "First subtitle";
    cue1.word_timing_quality = WordTimingQuality::Estimated;
    cue1.words = {
        TimedWord{"First", 0.0f, 1.0f, "w1", 0u, 5u},
        TimedWord{"subtitle", 1.0f, 2.0f, "w2", 6u, 14u},
    };
    track.cues.push_back(cue1);

    SubtitleCue cue2;
    cue2.start_s = 3.0f;
    cue2.end_s = 5.0f;
    cue2.text = "Second line";
    cue2.word_timing_quality = WordTimingQuality::Estimated;
    cue2.words = {
        TimedWord{"Second", 3.0f, 4.0f, "w3", 0u, 6u},
        TimedWord{"line", 4.0f, 5.0f, "w4", 7u, 11u},
    };
    track.cues.push_back(cue2);

    return track;
}

/// Build a composition that renders the subtitle track.
Composition build_subtitle_comp(SoftwareRenderer& renderer) {
    auto track = make_test_track();

    return composition(
        {.name = "SubtitleDeterminism",
         .width = 640, .height = 200,
         .frame_rate = FrameRate{30, 1},
         .duration = 180},
        [&renderer, track](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("subtitle", [&track](LayerBuilder& lb) {
                lb.screen_dimensions(640.0f, 200.0f);
                CanvasInfo canvas =
                    CanvasInfo::with_safe_area(640.0f, 200.0f, SafeAreaPreset{});
                chronon3d::authoring::Layer layer{lb, canvas};
                layer.subtitles(track)
                    .preset("minimal_white")
                    .font(chronon3d::test::bundled_font_path("assets/fonts/Poppins-Bold.ttf"), 32.0f)
                    .color(Color::white())
                    .box({600.0f, 100.0f})
                    .align(TextAlign::Center)
                    .place(TextPlacementKind::CanvasCenter)
                    .allow_estimated_word_timing(true)
                    .build();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Repeated renders of the same cue → identical hash ═══════════
TEST_CASE("SubtitleDeterminism 01: repeated renders produce identical hash") {
    auto renderer = test::make_renderer_shared();
    auto comp = build_subtitle_comp(*renderer);

    std::unordered_set<std::uint64_t> hashes;
    for (int i = 0; i < 10; ++i) {
        auto fb = renderer->render(comp, Frame{15}); // Inside first cue
        REQUIRE(fb != nullptr);
        hashes.insert(framebuffer_hash(*fb));
    }

    INFO("unique hashes across 10 renders: ", hashes.size());
    CHECK(hashes.size() == 1);
}

// ═══ Test 2 — Cold cache vs warm cache produce identical output ════════════
TEST_CASE("SubtitleDeterminism 02: cold cache vs warm cache yields identical hash") {
    auto renderer = test::make_renderer_shared();
    auto comp = build_subtitle_comp(*renderer);

    auto fb_cold = renderer->render(comp, Frame{15});
    REQUIRE(fb_cold != nullptr);
    const auto hash_cold = framebuffer_hash(*fb_cold);

    auto fb_warm = renderer->render(comp, Frame{15});
    REQUIRE(fb_warm != nullptr);
    const auto hash_warm = framebuffer_hash(*fb_warm);

    CHECK(hash_cold == hash_warm);
}

// ═══ Test 3 — Cache invalidation + rebuild produce identical output ═══════
TEST_CASE("SubtitleDeterminism 03: cache invalidated then rebuilt yields identical hash") {
    auto renderer = test::make_renderer_shared();
    auto comp = build_subtitle_comp(*renderer);

    auto fb_cached = renderer->render(comp, Frame{15});
    REQUIRE(fb_cached != nullptr);
    const auto hash_cached = framebuffer_hash(*fb_cached);

    renderer->clear_caches();
    auto fb_rebuilt = renderer->render(comp, Frame{15});
    REQUIRE(fb_rebuilt != nullptr);
    const auto hash_rebuilt = framebuffer_hash(*fb_rebuilt);

    CHECK(hash_cached == hash_rebuilt);
}

// ═══ Test 4 — Two separate renderer instances produce same output ══════════
TEST_CASE("SubtitleDeterminism 04: two separate renderers produce identical hash") {
    auto r1 = test::make_renderer_shared();
    auto r2 = test::make_renderer_shared();

    auto comp1 = build_subtitle_comp(*r1);
    auto comp2 = build_subtitle_comp(*r2);

    auto fb1 = r1->render(comp1, Frame{15});
    auto fb2 = r2->render(comp2, Frame{15});
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    const auto h1 = framebuffer_hash(*fb1);
    const auto h2 = framebuffer_hash(*fb2);

    INFO("renderer1 hash=", h1, " renderer2 hash=", h2);
    CHECK(h1 == h2);
}

// ═══ Test 5 — Single renderer reused → consistent across calls ════════════
TEST_CASE("SubtitleDeterminism 05: reused renderer produces consistent output") {
    auto renderer = test::make_renderer_shared();
    auto comp = build_subtitle_comp(*renderer);

    auto fb1 = renderer->render(comp, Frame{15});
    auto fb2 = renderer->render(comp, Frame{15});
    auto fb3 = renderer->render(comp, Frame{15});

    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    REQUIRE(fb3 != nullptr);

    const auto h1 = framebuffer_hash(*fb1);
    const auto h2 = framebuffer_hash(*fb2);
    const auto h3 = framebuffer_hash(*fb3);

    INFO("call1 hash=", h1, " call2=", h2, " call3=", h3);
    CHECK(h1 == h2);
    CHECK(h2 == h3);
}

// ═══ Test 6 — 1-thread vs 4-thread scheduler produce identical output ═══════
TEST_CASE("SubtitleDeterminism 06: serial vs parallel scheduler yields identical hash") {
    auto render_with_parallelism = [](int parallelism) -> std::uint64_t {
        tbb::global_control gc(tbb::global_control::max_allowed_parallelism, parallelism);
        auto renderer = test::make_renderer_shared();
        auto comp = build_subtitle_comp(*renderer);
        auto fb = renderer->render(comp, Frame{15});
        REQUIRE(fb != nullptr);
        return framebuffer_hash(*fb);
    };

    const auto hash_1t = render_with_parallelism(1);
    const auto hash_4t = render_with_parallelism(4);

    CHECK(hash_1t == hash_4t);
}

// ══ Test 7 — Sequential vs random frame access produce identical output ══
TEST_CASE("SubtitleDeterminism 07: sequential vs random frame order yields identical hashes") {
    // Reference: render frames inside each cue sequentially.
    std::array<std::uint64_t, 2> ref_hashes{};
    {
        auto renderer = test::make_renderer_shared();
        auto comp = build_subtitle_comp(*renderer);
        // Frame 15 is inside first cue (0-2s); frame 105 is inside second cue (3-5s).
        auto fb1 = renderer->render(comp, Frame{15});
        auto fb2 = renderer->render(comp, Frame{105});
        REQUIRE(fb1 != nullptr);
        REQUIRE(fb2 != nullptr);
        ref_hashes[0] = framebuffer_hash(*fb1);
        ref_hashes[1] = framebuffer_hash(*fb2);
    }

    // Random order on a fresh renderer.
    std::array<std::uint64_t, 2> random_hashes{};
    {
        auto renderer = test::make_renderer_shared();
        auto comp = build_subtitle_comp(*renderer);
        auto fb2 = renderer->render(comp, Frame{105});
        auto fb1 = renderer->render(comp, Frame{15});
        REQUIRE(fb2 != nullptr);
        REQUIRE(fb1 != nullptr);
        random_hashes[0] = framebuffer_hash(*fb1);
        random_hashes[1] = framebuffer_hash(*fb2);
    }

    CHECK(ref_hashes[0] == random_hashes[0]);
    CHECK(ref_hashes[1] == random_hashes[1]);

    // Sanity: the two cue frames should actually differ.
    CHECK(ref_hashes[0] != ref_hashes[1]);
}

// ══ Test 8 — Seek patterns 0→20→40 and 40→0→20 produce identical output ═══
TEST_CASE("SubtitleDeterminism 08: seek patterns 0->20->40 and 40->0->20 agree") {
    auto render_sequence = [](const std::vector<int>& frames)
        -> std::vector<std::uint64_t> {
        auto renderer = test::make_renderer_shared();
        auto comp = build_subtitle_comp(*renderer);
        std::vector<std::uint64_t> out;
        out.reserve(frames.size());
        for (int f : frames) {
            auto fb = renderer->render(comp, Frame{f});
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

    // Sanity: frames inside different cues should differ (frame 15 is in the
    // first cue, frame 105 in the second).
    auto renderer = test::make_renderer_shared();
    auto comp = build_subtitle_comp(*renderer);
    auto fb_first = renderer->render(comp, Frame{15});
    auto fb_second = renderer->render(comp, Frame{105});
    REQUIRE(fb_first != nullptr);
    REQUIRE(fb_second != nullptr);
    CHECK(framebuffer_hash(*fb_first) != framebuffer_hash(*fb_second));
}

// ══ Test 9 — Karaoke-style preset (active_word_pop) is deterministic ══════
TEST_CASE("SubtitleDeterminism 09: karaoke active_word_pop preset is deterministic") {
    auto build_karaoke_comp = [](SoftwareRenderer& renderer) -> Composition {
        presets::text::SubtitleTrack track;
        SubtitleCue cue;
        cue.start_s = 0.0f;
        cue.end_s = 2.0f;
        cue.text = "One Two";
        cue.word_timing_quality = WordTimingQuality::Authoritative;
        cue.words = {
            TimedWord{"One", 0.0f, 1.0f, "w1", 0u, 3u},
            TimedWord{"Two", 1.0f, 2.0f, "w2", 4u, 7u},
        };
        track.cues.push_back(cue);

        return composition(
            {.name = "SubtitleDeterminismKaraoke",
             .width = 640, .height = 200,
             .frame_rate = FrameRate{30, 1},
             .duration = 90},
            [&renderer, track](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx);
                s.font_engine(&renderer.font_engine());
                s.layer("karaoke", [&track](LayerBuilder& lb) {
                    lb.screen_dimensions(640.0f, 200.0f);
                    CanvasInfo canvas =
                        CanvasInfo::with_safe_area(640.0f, 200.0f, SafeAreaPreset{});
                    chronon3d::authoring::Layer layer{lb, canvas};
                    layer.subtitles(track)
                        .preset("active_word_pop")
                        .font(chronon3d::test::bundled_font_path("assets/fonts/Poppins-Bold.ttf"), 48.0f)
                        .color(Color::white())
                        .box({600.0f, 100.0f})
                        .align(TextAlign::Center)
                        .place(TextPlacementKind::CanvasCenter)
                        .build();
                });
                return s.build();
            });
    };

    auto r1 = test::make_renderer_shared();
    auto r2 = test::make_renderer_shared();

    auto comp1 = build_karaoke_comp(*r1);
    auto comp2 = build_karaoke_comp(*r2);

    // Compare the active word frame (frame 15 = 0.5s, inside first word).
    auto fb1 = r1->render(comp1, Frame{15});
    auto fb2 = r2->render(comp2, Frame{15});
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    const auto h1 = framebuffer_hash(*fb1);
    const auto h2 = framebuffer_hash(*fb2);

    INFO("karaoke renderer1 hash=", h1, " renderer2 hash=", h2);
    CHECK(h1 == h2);

    // Sanity: active word frame should differ from inactive word frame.
    auto fb_inactive = r1->render(comp1, Frame{45}); // 1.5s, inside second word
    REQUIRE(fb_inactive != nullptr);
    CHECK(framebuffer_hash(*fb1) != framebuffer_hash(*fb_inactive));
}
