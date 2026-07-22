// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_completeness/text_typewriter.cpp
//
// P0-7: Typewriter / Kinetic Typography — frame-by-frame verification.
//
// The typewriter effect progressively reveals text grapheme by grapheme.
// We approximate this via string slicing at composition-build time
// (same approach as ae_02_typewriter.cpp).
//
// Cases:
//   1. Frame 0 (1 char) has less ink than frame 30 (full text)
//   2. Visible pixels grow monotonically: f0 < f10 < f20 < f30
//   3. No ghost glyphs: frame 0 has significantly fewer pixels than final
//   4. Final frame has maximum ink coverage
//   5. Determinism: same frame → same hash across renders
//   6. Frame 0 has first character ink (reveal starts immediately)
//
// Uses string slicing (not per-glyph opacity animation) which is the
// current approximation used by the golden test suite.  When per-glyph
// opacity animation is available, these tests should be updated.
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
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

#include <chronon3d/backends/text/text_unicode_utils.hpp>

#include <string>
#include <cstring>

using namespace chronon3d;
using namespace chronon3d::test;
using namespace chronon3d::test::completeness;

namespace {

constexpr const char* kTypewriterFull = "TYPEWRITER REVEAL TEST";

// Frame-by-frame substring: simulates progressive grapheme/cluster reveal.
// Uses grapheme-aware slicing so that multi-byte characters (combining
// marks, ZWJ emoji, CJK) are never split in the middle.
std::string typewriter_text(std::string_view text,
                            std::size_t frame_idx,
                            std::size_t total_frames = 30) {
    using chronon3d::detail::grapheme_cluster_count;
    using chronon3d::detail::grapheme_byte_offset_at;

    const std::size_t total_graphemes = grapheme_cluster_count(text);
    if (total_graphemes == 0) return std::string();
    // For ASCII input the numeric count equals the byte count; the
    // grapheme-aware slicing matters for multi-byte scripts.
    if (frame_idx == 0) {
        const std::size_t first_len = grapheme_byte_offset_at(text, 1);
        return std::string(text.substr(0, first_len));
    }
    const std::size_t revealed = std::max<std::size_t>(
        1, (total_graphemes * frame_idx) / total_frames);
    const std::size_t safe_revealed = std::min(revealed, total_graphemes);
    const std::size_t byte_len = grapheme_byte_offset_at(text, safe_revealed);
    return std::string(text.substr(0, byte_len));
}

std::string typewriter_text(std::size_t frame_idx, std::size_t total_frames = 30) {
    return typewriter_text(kTypewriterFull, frame_idx, total_frames);
}

Composition build_typewriter_composition(
    SoftwareRenderer& renderer,
    std::size_t frame_idx
) {
    return composition(
        {.name = "TextTypewriter/test",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, frame_idx](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("tw_layer", [&renderer, frame_idx](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("tw_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = typewriter_text(frame_idx)},.placement = TextPlacement{TextPlacementKind::Absolute, {100.0f, 540.0f}},.font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 96.0f
                        },.layout = {
                            .box = {1800.0f, 400.0f},
                            .align = TextAlign::Left,
                            .vertical_align = VerticalAlign::Middle
                        },.appearance = {.color = Color::white()},}
                }).commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Frame 0 has less ink than final frame ═══════════════════════
TEST_CASE("TextTypewriter 01: frame 0 has less ink than final") {
    auto r0 = test::make_renderer();
    auto rF = test::make_renderer();

    auto fb0 = r0.render(build_typewriter_composition(r0, 0), Frame{0});
    auto fbF = rF.render(build_typewriter_composition(rF, 30), Frame{30});
    REQUIRE(fb0 != nullptr);
    REQUIRE(fbF != nullptr);

    const int vis0 = count_visible_pixels(*fb0);
    const int visF = count_visible_pixels(*fbF);
    INFO("frame0 visible=", vis0, " frame30 visible=", visF);

    // Both must have some ink.
    CHECK(vis0 > 0);
    CHECK(visF > 0);
    // Final frame must have significantly more ink than frame 0.
    CHECK(visF > vis0 * 3);
}

// ═══ Test 2 — Visible pixels grow monotonically ═══════════════════════════
// Render at f0, f10, f20, f30 and verify ink increases at each step.
TEST_CASE("TextTypewriter 02: visible pixels grow monotonically") {
    int prev_vis = 0;
    for (std::size_t f : {0u, 10u, 20u, 30u}) {
        auto renderer = test::make_renderer();
        auto fb = renderer.render(
            build_typewriter_composition(renderer, f), Frame{f});
        REQUIRE(fb != nullptr);

        const int vis = count_visible_pixels(*fb);
        INFO("frame=", f, " visible=", vis, " prev=", prev_vis);

        CHECK(vis > 0);
        if (prev_vis > 0) {
            // Ink should grow or stay the same (monotonic).
            CHECK(vis >= prev_vis);
        }
        prev_vis = vis;
    }
}

// ═══ Test 3 — No ghost glyphs: frame 0 ink << final frame ink ═════════════
TEST_CASE("TextTypewriter 03: no ghost glyphs frame 0 vs final") {
    auto r0 = test::make_renderer();
    auto rF = test::make_renderer();

    auto fb0 = r0.render(build_typewriter_composition(r0, 0), Frame{0});
    auto fbF = rF.render(build_typewriter_composition(rF, 30), Frame{30});
    REQUIRE(fb0 != nullptr);
    REQUIRE(fbF != nullptr);

    const int vis0 = count_visible_pixels(*fb0);
    const int visF = count_visible_pixels(*fbF);
    const AlphaBBox bbox0 = alpha_bbox(*fb0);
    const AlphaBBox bboxF = alpha_bbox(*fbF);

    INFO("frame0: visible=", vis0, " bbox_w=", bbox0.width());
    INFO("frame30: visible=", visF, " bbox_w=", bboxF.width());

    // Frame 0 (1 char) should have much less ink than frame 30 (full text).
    CHECK(vis0 < visF / 3);

    // Frame 0 bbox should be much narrower than final.
    CHECK(bbox0.width() < bboxF.width() / 2);
}

// ═══ Test 4 — Final frame has maximum ink coverage ════════════════════════
TEST_CASE("TextTypewriter 04: final frame has maximum ink") {
    int max_vis = 0;
    std::size_t max_frame = 0;

    for (std::size_t f : {0u, 5u, 10u, 15u, 20u, 25u, 30u}) {
        auto renderer = test::make_renderer();
        auto fb = renderer.render(
            build_typewriter_composition(renderer, f), Frame{f});
        REQUIRE(fb != nullptr);

        const int vis = count_visible_pixels(*fb);
        if (vis > max_vis) {
            max_vis = vis;
            max_frame = f;
        }
    }

    INFO("max_vis=", max_vis, " at frame=", max_frame);

    // The maximum should be at frame 30 (full text).
    CHECK(max_frame == 30);
}

// ═══ Test 5 — Determinism: same frame → same hash ═════════════════════════
TEST_CASE("TextTypewriter 05: deterministic across renders") {
    auto r1 = test::make_renderer();
    auto r2 = test::make_renderer();

    auto fb1 = r1.render(build_typewriter_composition(r1, 15), Frame{15});
    auto fb2 = r2.render(build_typewriter_composition(r2, 15), Frame{15});
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    CHECK(count_visible_pixels(*fb1) == count_visible_pixels(*fb2));
    CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
}

// ═══ Test 6 — Before start (frame 0 with empty content) ══════════════════
// The typewriter starts at frame 0 with 1 character.  Verify that even
// at frame 0, there's at least some ink (the first character).
TEST_CASE("TextTypewriter 06: frame 0 has first character ink") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_typewriter_composition(renderer, 0), Frame{0});
    REQUIRE(fb != nullptr);

    const int vis = count_visible_pixels(*fb);
    const AlphaBBox bbox = alpha_bbox(*fb);
    INFO("frame0: visible=", vis, " bbox_w=", bbox.width());

    // Frame 0 should have at least 1 character worth of ink.
    CHECK(vis > 0);
    CHECK_FALSE(bbox.empty());
    // The first character "T" at 96pt should produce some width.
    CHECK(bbox.width() > 20);
}

// ═══ Test 7 — TICKET-FALSE-GREEN-TEST-AUDIT Step 4: typewriter frame
//                 sequence F0/F1/F5/F10/F20/finale with monotonic
//                 cluster_visible_count + stable layout invariant.
// ═══════════════════════════════════════════════════════════════════════
TEST_CASE("TICKET-FALSE-GREEN-TEST-AUDIT 7: typewriter F0/F1/F5/F10/F20/finale monotonic + stable layout") {
    // Render at 6 frames spanning the typewriter reveal sequence.
    // TICKET-FALSE-GREEN-TEST-AUDIT §Accepted deviations: cluster_visible_count
    // == visible_pixels (proxy) holds ONLY for ASCII text (current text
    // "TYPEWRITER REVEAL TEST" is ASCII — grapheme==byte==char step).
    // For non-ASCII (combining marks, ZWJ emoji), the proxy under-counts
    // graphemes; this is acceptable as the test text is deliberately ASCII
    // per TICKET §Accepted deviations.
    std::vector<std::size_t> frames = {0u, 1u, 5u, 10u, 20u, 30u};
    std::vector<int>         visible_counts;
    std::vector<AlphaBBox>   bboxes;
    visible_counts.reserve(frames.size());
    bboxes.reserve(frames.size());

    int prev_vis = 0;
    for (std::size_t f : frames) {
        auto renderer = test::make_renderer();
        auto fb = renderer.render(
            build_typewriter_composition(renderer, f), Frame{f});
        REQUIRE(fb != nullptr);

        const int vis = count_visible_pixels(*fb);
        const AlphaBBox bbox = alpha_bbox(*fb);
        visible_counts.push_back(vis);
        bboxes.push_back(bbox);

        INFO("frame=", f, " visible=", vis, " prev=", prev_vis,
             " bbox=(", bbox.x0, ",", bbox.y0, ")-(",
             bbox.x1, ",", bbox.y1, ")");

        // TICKET-FALSE-GREEN-TEST-AUDIT Step 4 (a): cluster_visible_count
        // monotonic — visible_pixels never DECREASES as the typewriter
        // reveals more characters.
        CHECK(vis > 0);
        if (prev_vis > 0) {
            CHECK(vis >= prev_vis);
        }
        prev_vis = vis;
    }

    // TICKET-FALSE-GREEN-TEST-AUDIT Step 4 (b): stable layout invariant —
    // the bbox must grow LEFT-TO-RIGHT monotonically (x0 stays the same
    // since text starts at the same position; x1 increases as chars are
    // revealed; y0/y1 stay stable since line height is fixed).
    for (std::size_t i = 1; i < frames.size(); ++i) {
        const auto& prev = bboxes[i - 1];
        const auto& curr = bboxes[i];
        INFO("frame transition: prev.f=", frames[i - 1],
             " curr.f=", frames[i],
             " prev.x1=", prev.x1, " curr.x1=", curr.x1,
             " prev.y0=", prev.y0, " curr.y0=", curr.y0);
        // The right edge of the bbox must NOT retreat leftward
        // (no layout shift backwards as chars are revealed).
        CHECK(curr.x1 >= prev.x1);
        // The top edge must stay stable (line height fixed).
        CHECK(std::abs(curr.y0 - prev.y0) <= 2);  // 2px tolerance for AA
        // The bottom edge must stay stable (line height fixed).
        CHECK(std::abs(curr.y1 - prev.y1) <= 2);
    }
}

// ═══ Test 8 — Grapheme-aware slicing does not split multi-byte characters ═
TEST_CASE("TextTypewriter 08: grapheme-aware slicing preserves multi-byte clusters") {
    using chronon3d::detail::grapheme_cluster_count;
    using chronon3d::detail::grapheme_byte_offset_at;

    // "café" has 4 graphemes; 'é' is two bytes. Byte-based slicing at
    // 3 bytes would produce "caf" plus a broken UTF-8 tail. Grapheme-aware
    // slicing must return a valid prefix ending on a grapheme boundary.
    const char* multi = "café";
    const std::size_t total = grapheme_cluster_count(multi);
    CHECK(total == 4);

    const std::string slice = typewriter_text(multi, 3, 30);
    // We should have revealed at least one grapheme but fewer than all.
    CHECK(!slice.empty());
    CHECK(slice.size() < std::strlen(multi));
    // The slice must be valid UTF-8 and end at a grapheme boundary.
    const std::size_t slice_graphemes = grapheme_cluster_count(slice);
    CHECK(slice_graphemes > 0);
    CHECK(slice_graphemes < total);
}

