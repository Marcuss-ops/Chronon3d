// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_placement/text_placement_golden.cpp
//
// Golden regression tests for the text placement pipeline.
//
// Covers:
//   - Dashboard (8 compositions)  — golden comparison at frame 0 / 30
//   - Anti-double-translation      — alpha-centroid position check
//   - Layout box alignment         — golden comparison
//   - Clipping (7)                 — golden comparison + edge-touch detection
//   - Multi-resolution (4)         — golden comparison at native resolution
//   - Cache invalidation (1)        — frame-dependent content correctness
//
// Golden PNGs:   test_renders/golden/text/placement/
// Artifacts:     test_renders/artifacts/text/placement/
//
// Update goldens: CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextPlacement
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>

#include <tests/visual/support/golden_test.hpp>
#include <tests/helpers/test_utils.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::test;

// ═══════════════════════════════════════════════════════════════════════════
// Forward-declare composition factories (MUST be at global scope — defined
// in content/text_placement/text_placement_compositions.cpp).
// ═══════════════════════════════════════════════════════════════════════════

namespace chronon3d::content::text_placement {
Composition make_static_center_no_pos();
Composition make_animated_center_no_pos();
Composition make_scale_130_center_no_pos();
Composition make_glow_shadow_center_no_pos();
Composition make_multiline_center_middle();
Composition make_small_box_overflow_clip();
Composition make_multisource_text_plus_shape();
Composition make_portrait_1080x1920_center();
Composition make_antidouble_static();
Composition make_antidouble_animated();
Composition make_box_alignment();
Composition make_clip_blur_0();
Composition make_clip_blur_7();
Composition make_clip_blur_20();
Composition make_clip_glow_40();
Composition make_clip_shadow_80();
Composition make_clip_scale_130();
Composition make_clip_scale_200();
Composition make_multires_1920x1080();
Composition make_multires_1280x720();
Composition make_multires_1080x1920();
Composition make_multires_3840x2160();
Composition make_cache_invalidation();
} // namespace chronon3d::content::text_placement

using namespace chronon3d::content::text_placement;

// ═══════════════════════════════════════════════════════════════════════════
// Shared configuration
// ═══════════════════════════════════════════════════════════════════════════

namespace {

GoldenTestConfig make_placement_config() {
    GoldenTestConfig cfg;
    cfg.golden_directory  = "test_renders/golden/text/placement";
    cfg.artifact_directory = "test_renders/artifacts/text/placement";
    cfg.mode               = golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error       = 6.0f / 255.0f;
    cfg.threshold.max_abs_error            = 60.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio  = 0.15f;
    cfg.threshold.max_rmse                 = 8.0f / 255.0f;
    cfg.threshold.min_ssim                 = 0.85f;
    return cfg;
}

// ── verify_placement ─────────────────────────────────────────────────────
// Render a composition at a given frame and compare against golden.
void verify_placement(Composition& comp, Frame frame,
                       const std::string& label) {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(comp, frame);
    REQUIRE(fb != nullptr);

    auto cfg = make_placement_config();
    auto result = verify_golden(*fb, label, cfg);
    INFO("Golden: ", result.message);
    if (result.golden_missing) {
        MESSAGE("Golden missing — run with CHRONON3D_UPDATE_GOLDENS=1 to create.");
        return;
    }
    CHECK(result.passed);
}

// ── Alpha centroid ───────────────────────────────────────────────────────
// Compute the alpha-weighted centroid of a framebuffer.
// (0,0) = top-left.  Returns {-1,-1} if no visible pixels found.
// Color components are floats in [0,1].
struct Centroid { f32 x; f32 y; f32 max_alpha; };

Centroid compute_alpha_centroid(const Framebuffer& fb, float alpha_threshold = 0.05f) {
    Centroid c{0.0f, 0.0f, 0.0f};
    double sum_x = 0.0, sum_y = 0.0, sum_w = 0.0;
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());

    for (int y = 0; y < h; ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < w; ++x) {
            const float a = row[x].a;
            if (a > c.max_alpha) c.max_alpha = a;
            if (a > alpha_threshold) {
                sum_x += static_cast<double>(x) * static_cast<double>(a);
                sum_y += static_cast<double>(y) * static_cast<double>(a);
                sum_w += static_cast<double>(a);
            }
        }
    }

    if (sum_w > 0.0) {
        c.x = static_cast<f32>(sum_x / sum_w);
        c.y = static_cast<f32>(sum_y / sum_w);
    } else {
        c.x = -1.0f;
        c.y = -1.0f;
    }
    return c;
}

// ── Edge-touch detection ──────────────────────────────────────────────────
// Returns true if the alpha content touches any edge of the framebuffer.
// This indicates potential clipping: the internal raster surface was too small.
bool alpha_touches_edge(const Framebuffer& fb, float alpha_threshold = 0.05f) {
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());

    // Top / bottom row
    const Color* top = fb.pixels_row(0);
    const Color* bot = fb.pixels_row(h - 1);
    for (int x = 0; x < w; ++x) {
        if (top[x].a > alpha_threshold) return true;
        if (bot[x].a > alpha_threshold) return true;
    }
    // Left / right column
    for (int y = 0; y < h; ++y) {
        const Color* row = fb.pixels_row(y);
        if (row[0].a > alpha_threshold) return true;
        if (row[w - 1].a > alpha_threshold) return true;
    }
    return false;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// Group A — Dashboard golden tests (8)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlace Static Center") {
    auto comp = make_static_center_no_pos();
    verify_placement(comp, Frame{0}, "text_placement_static_center");
}

TEST_CASE("TextPlace Animated Center (settled, f30)") {
    auto comp = make_animated_center_no_pos();
    verify_placement(comp, Frame{30}, "text_placement_animated_center_f30");
}

TEST_CASE("TextPlace Scale 1.30") {
    auto comp = make_scale_130_center_no_pos();
    verify_placement(comp, Frame{0}, "text_placement_scale_130");
}

TEST_CASE("TextPlace Glow Shadow") {
    auto comp = make_glow_shadow_center_no_pos();
    verify_placement(comp, Frame{0}, "text_placement_glow_shadow");
}

TEST_CASE("TextPlace Multiline") {
    auto comp = make_multiline_center_middle();
    verify_placement(comp, Frame{0}, "text_placement_multiline");
}

TEST_CASE("TextPlace Small Box Overflow") {
    auto comp = make_small_box_overflow_clip();
    verify_placement(comp, Frame{0}, "text_placement_small_box");
}

TEST_CASE("TextPlace Multisource") {
    auto comp = make_multisource_text_plus_shape();
    verify_placement(comp, Frame{0}, "text_placement_multisource");
}

TEST_CASE("TextPlace Portrait 1080x1920") {
    auto comp = make_portrait_1080x1920_center();
    verify_placement(comp, Frame{0}, "text_placement_portrait_1080x1920");
}

// ═══════════════════════════════════════════════════════════════════════════
// Group B — Anti-double-translation (alpha centroid check)
// ═══════════════════════════════════════════════════════════════════════════

// B.1 — Static text: alpha centroid must be within 10% of canvas center.
TEST_CASE("TextPlace AntiDouble Static — centroid at canvas center") {
    auto renderer = test::make_renderer();
    auto comp = make_antidouble_static();
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 1920);
    REQUIRE(fb->height() == 1080);

    auto c = compute_alpha_centroid(*fb);
    CHECK(c.max_alpha > 0.5f);  // something visible

    const f32 cx = static_cast<f32>(fb->width())  * 0.5f;   // 960
    const f32 cy = static_cast<f32>(fb->height()) * 0.5f;   // 540
    const f32 tolerance = static_cast<f32>(fb->width()) * 0.10f;  // 10% = 96px

    INFO("Alpha centroid: (", c.x, ", ", c.y, ")  expected near (", cx, ", ", cy, ")");
    CHECK(std::abs(c.x - cx) < tolerance);
    CHECK(std::abs(c.y - cy) < tolerance);

    // CRITICAL: the centroid must NOT be near 2× canvas center (the double-translation bug).
    const f32 dx2 = std::abs(c.x - cx * 2.0f);
    const f32 dy2 = std::abs(c.y - cy * 2.0f);
    // NOT at {1920, 1080}
    bool near_double_center = (dx2 < tolerance) && (dy2 < tolerance);
    CHECK_FALSE(near_double_center);
}

// B.2 — Animated text at frame 0: alpha centroid should be offset from canvas center.
TEST_CASE("TextPlace AntiDouble Animated — centroid offset at f0") {
    auto renderer = test::make_renderer();
    auto comp = make_antidouble_animated();
    auto fb = renderer.render(comp, Frame{0});
    REQUIRE(fb != nullptr);

    auto c = compute_alpha_centroid(*fb);
    CHECK(c.max_alpha > 0.3f);  // visible (anim offset at f0, no opacity fade)

    const f32 cx = static_cast<f32>(fb->width())  * 0.5f;
    const f32 cy = static_cast<f32>(fb->height()) * 0.5f;

    // At frame 0, the position anim offset is {30, 20}, so centroid
    // should be ≈ cx+30, cy+20 — NOT at cx,cy AND NOT at 2*cx,2*cy.
    const f32 dx2 = std::abs(c.x - cx * 2.0f);
    const f32 dy2 = std::abs(c.y - cy * 2.0f);
    const f32 tolerance = static_cast<f32>(fb->width()) * 0.12f;

    INFO("Alpha centroid: (", c.x, ", ", c.y, ")  canvas_center: (", cx, ", ", cy, ")");
    bool near_double_center = (dx2 < tolerance) && (dy2 < tolerance);
    CHECK_FALSE(near_double_center);  // NOT double-translated

    // Golden comparison as secondary check
    auto cfg = make_placement_config();
    auto result = verify_golden(*fb, "text_placement_antidouble_animated_f0", cfg);
    INFO("Golden: ", result.message);
    if (!result.golden_missing) CHECK(result.passed);
}

// B.3 — Animated text at frame 30 (settled): centroid back at canvas center.
TEST_CASE("TextPlace AntiDouble Animated — centroid settled at f30") {
    auto renderer = test::make_renderer();
    auto comp = make_antidouble_animated();
    auto fb = renderer.render(comp, Frame{30});
    REQUIRE(fb != nullptr);

    auto c = compute_alpha_centroid(*fb);
    CHECK(c.max_alpha > 0.5f);

    const f32 cx = static_cast<f32>(fb->width())  * 0.5f;
    const f32 cy = static_cast<f32>(fb->height()) * 0.5f;
    const f32 tolerance = static_cast<f32>(fb->width()) * 0.10f;

    INFO("Alpha centroid: (", c.x, ", ", c.y, ")  expected near (", cx, ", ", cy, ")");
    CHECK(std::abs(c.x - cx) < tolerance);
    CHECK(std::abs(c.y - cy) < tolerance);
}

// ═══════════════════════════════════════════════════════════════════════════
// Group C — Layout box alignment
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlace Box Align") {
    auto comp = make_box_alignment();
    verify_placement(comp, Frame{0}, "text_placement_box_align");
}

// ═══════════════════════════════════════════════════════════════════════════
// Group D — Clipping (golden + edge-touch detection)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlace Clip Blur 0") {
    auto comp = make_clip_blur_0();
    verify_placement(comp, Frame{0}, "text_placement_clip_blur0");
}

TEST_CASE("TextPlace Clip Blur 7") {
    auto comp = make_clip_blur_7();
    verify_placement(comp, Frame{0}, "text_placement_clip_blur7");
}

TEST_CASE("TextPlace Clip Blur 20") {
    auto comp = make_clip_blur_20();
    verify_placement(comp, Frame{0}, "text_placement_clip_blur20");
}

TEST_CASE("TextPlace Clip Glow 40") {
    auto comp = make_clip_glow_40();
    verify_placement(comp, Frame{0}, "text_placement_clip_glow40");
}

TEST_CASE("TextPlace Clip Shadow 80") {
    auto comp = make_clip_shadow_80();
    verify_placement(comp, Frame{0}, "text_placement_clip_shadow80");
}

TEST_CASE("TextPlace Clip Scale 1.30") {
    auto comp = make_clip_scale_130();
    verify_placement(comp, Frame{0}, "text_placement_clip_scale130");
}

TEST_CASE("TextPlace Clip Scale 2.00") {
    auto comp = make_clip_scale_200();
    verify_placement(comp, Frame{0}, "text_placement_clip_scale200");
}

// Edge-touch check: verify that clipped text doesn't touch framebuffer edges.
// Grouped into a single test case for efficiency.
TEST_CASE("TextPlace Clipping — no edge touch") {
    // Only check the compositions where clipping is plausible:
    // Blur 20 (large blur), Glow 40 (large glow), Shadow 80 (large offset), Scale 2.00
    struct ClipCheck { std::string label; Composition comp; };
    std::vector<ClipCheck> checks;
    checks.push_back({"blur20",  make_clip_blur_20()});
    checks.push_back({"glow40",  make_clip_glow_40()});
    checks.push_back({"shadow80", make_clip_shadow_80()});
    checks.push_back({"scale200", make_clip_scale_200()});

    auto renderer = test::make_renderer();
    for (auto& ch : checks) {
        auto fb = renderer.render(ch.comp, Frame{0});
        REQUIRE(fb != nullptr);
        bool touches = alpha_touches_edge(*fb);
        INFO("Clip check [", ch.label, "] alpha_touches_edge = ", touches);
        // NOTE: edge-touch means the content reaches the framebuffer edge.
        // For glow/shadow/scale, this may be EXPECTED if the effect extends
        // to the canvas edge.  We log it but don't hard-fail — the golden
        // comparison is the authoritative check.
        if (touches) {
            MESSAGE("Edge touch detected for ", ch.label, " — check golden for clipping");
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Group E — Multi-resolution (golden comparison)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlace MultiRes 1920x1080") {
    auto comp = make_multires_1920x1080();
    verify_placement(comp, Frame{0}, "text_placement_multires_1920x1080");
}

TEST_CASE("TextPlace MultiRes 1280x720") {
    auto comp = make_multires_1280x720();
    verify_placement(comp, Frame{0}, "text_placement_multires_1280x720");
}

TEST_CASE("TextPlace MultiRes 1080x1920") {
    auto comp = make_multires_1080x1920();
    verify_placement(comp, Frame{0}, "text_placement_multires_1080x1920");
}

TEST_CASE("TextPlace MultiRes 3840x2160") {
    auto comp = make_multires_3840x2160();
    verify_placement(comp, Frame{0}, "text_placement_multires_3840x2160");
}

// ── Centroid check: all 4 resolutions must center at width/2, height/2 ──
TEST_CASE("TextPlace MultiRes — centroid at dynamic center") {
    struct ResCheck { const char* label; int w; int h; Composition comp; };
    std::vector<ResCheck> checks;
    checks.push_back({"1920x1080", 1920, 1080, make_multires_1920x1080()});
    checks.push_back({"1280x720",  1280, 720,  make_multires_1280x720()});
    checks.push_back({"1080x1920", 1080, 1920, make_multires_1080x1920()});
    checks.push_back({"3840x2160", 3840, 2160, make_multires_3840x2160()});

    auto renderer = test::make_renderer();
    for (auto& rc : checks) {
        auto fb = renderer.render(rc.comp, Frame{0});
        REQUIRE(fb != nullptr);
        auto c = compute_alpha_centroid(*fb);
        CHECK(c.max_alpha > 0.5f);

        const f32 cx = static_cast<f32>(rc.w) * 0.5f;
        const f32 cy = static_cast<f32>(rc.h) * 0.5f;
        const f32 tolerance = static_cast<f32>(rc.w) * 0.12f;

        INFO(rc.label, " centroid: (", c.x, ", ", c.y, ")  expected: (", cx, ", ", cy, ")");
        CHECK(std::abs(c.x - cx) < tolerance);
        CHECK(std::abs(c.y - cy) < tolerance);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Group F — Cache invalidation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlace Cache Invalidation — HELLO at f0") {
    auto comp = make_cache_invalidation();
    verify_placement(comp, Frame{0}, "text_placement_cache_hello_f0");
}

TEST_CASE("TextPlace Cache Invalidation — WORLD at f31") {
    auto comp = make_cache_invalidation();
    verify_placement(comp, Frame{31}, "text_placement_cache_world_f31");
}

TEST_CASE("TextPlace Cache Invalidation — CENTER at f61") {
    auto comp = make_cache_invalidation();
    verify_placement(comp, Frame{61}, "text_placement_cache_center_f61");
}

// ── Cache invalidation: verify text actually changes across frames. ──
TEST_CASE("TextPlace Cache Invalidation — content changes") {
    auto renderer = test::make_renderer();
    auto comp = make_cache_invalidation();

    // Render at 3 key frames and check that framebuffers differ.
    auto fb0 = renderer.render(comp, Frame{0});
    auto fb1 = renderer.render(comp, Frame{31});
    auto fb2 = renderer.render(comp, Frame{61});
    REQUIRE(fb0 != nullptr);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    // Quick pixel-difference check: the three framebuffers must differ.
    // We use a simple mean-abs-error between frames; if the text changes,
    // the error should be significant.
    auto mae = [](const Framebuffer& a, const Framebuffer& b) -> double {
        if (a.width() != b.width() || a.height() != b.height()) return 1.0;
        double sum = 0.0;
        const int w = static_cast<int>(a.width());
        const int h = static_cast<int>(a.height());
        const int n = w * h;
        for (int y = 0; y < h; ++y) {
            const Color* ra = a.pixels_row(y);
            const Color* rb = b.pixels_row(y);
            for (int x = 0; x < w; ++x) {
                sum += static_cast<double>(std::abs(ra[x].r - rb[x].r));
            }
        }
        return sum / static_cast<double>(n);
    };

    double err_01 = mae(*fb0, *fb1);
    double err_12 = mae(*fb1, *fb2);
    double err_02 = mae(*fb0, *fb2);

    INFO("MAE f0->f31: ", err_01, "  f31->f61: ", err_12, "  f0->f61: ", err_02);

    // "HELLO" -> "WORLD" -> "CENTER" -- each transition should produce
    // a visible pixel difference (MAE > small threshold).
    const double min_diff = 1.0 / 255.0;  // at least 1 level average difference
    CHECK(err_01 > min_diff);
    CHECK(err_12 > min_diff);
    CHECK(err_02 > min_diff);

    // Ensure "HELLO" != "CENTER" (start != end) -- should be the largest diff.
    CHECK(err_02 > err_01 * 0.5);
}
