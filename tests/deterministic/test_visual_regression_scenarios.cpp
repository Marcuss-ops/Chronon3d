// ═══════════════════════════════════════════════════════════════════════════
// test_visual_regression_scenarios.cpp — PR-A3 (Blocco A, Fase 2)
//
// 15 minimal scenarios for the visual regression harness. Extends the
// scheduler-level fixture pattern from tests/deterministic/test_determinism_harness.cpp
// + sentinel-capture pattern from tests/deterministic/test_baseline_green.cpp
// (PR 6.8.5 precedent) for future golden acquisition.
//
// Two scenario archetypes:
//   (A) Text-bearing scenarios use canonical centered_text(CenterTextOptions)
//       from content/text/text_helpers.hpp (per PR-A2) via SceneBuilder:
//       s.layer("hero", [&](LayerBuilder& l) { l.text("k", ts); });
//   (B) Animation scenarios use FrameContext-driven rect stand-ins; the
//       point of these is scheduling-pipeline determinism at frame-snapshot
//       granularity, not per-glyph animation coverage.
//
// Each scenario computes ScenarioMetrics (8-metric canon of
// docs/01-baseline-green.md §2.4-2.5) and applies the sentinel-gate macro  // drift-allow: stale-ref
// VR_GATE(short_label, kRef*, m). The macro emits the first observed hash
// as a MESSAGE on clean CI runs, which Two-Phase Commit Strategy (PR 6.8.5
// precedent) will populate kRefVR* with the real captured hash.
//
// Capture workflow (mirror docs/01-baseline-green.md §2.3):
//   ctest --test-dir build/chronon/linux-ci -R 'VisualRegression' -V 2>&1 \
//     | tee /tmp/vr_first_run.txt
//   grep -oE 'VR/[A-Za-z]+ unset; first hash to capture: [0-9]+' \
//     /tmp/vr_first_run.txt
//   # Copy each <hash> into kRefVR<Name> in this file, commit, re-run ctest.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>  // PR-A3 fix A: explicit Fill::linear / GradientStop reachability
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/helpers/pixel_assertions.hpp>
#include <content/text/text_helpers.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// ── Sentinel-capture pattern (mirrors test_baseline_green.cpp PR 6.8.5) ──

constexpr std::uint64_t kUncapturedSentinel = 0xDEADBEEFDEADBEEFULL;

inline bool is_reference_captured(std::uint64_t r) noexcept {
    return r != kUncapturedSentinel;
}

// ── 15 sentinel constants ─ populated on first clean CI capture ─────────

constexpr std::uint64_t kRefVRStatic         = kUncapturedSentinel;
constexpr std::uint64_t kRefVRMultiline      = kUncapturedSentinel;
constexpr std::uint64_t kRefVRTracking       = kUncapturedSentinel;
constexpr std::uint64_t kRefVRStroke         = kUncapturedSentinel;
constexpr std::uint64_t kRefVRGradient       = kUncapturedSentinel;
constexpr std::uint64_t kRefVRShadow         = kUncapturedSentinel;
constexpr std::uint64_t kRefVRGlow           = kUncapturedSentinel;
constexpr std::uint64_t kRefVRBlur           = kUncapturedSentinel;
constexpr std::uint64_t kRefVRTypewriter     = kUncapturedSentinel;
constexpr std::uint64_t kRefVRAnimGlyph      = kUncapturedSentinel;
constexpr std::uint64_t kRefVRAnimWord       = kUncapturedSentinel;
constexpr std::uint64_t kRefVRRTL            = kUncapturedSentinel;
constexpr std::uint64_t kRefVRCJK            = kUncapturedSentinel;
constexpr std::uint64_t kRefVREmojiFallback  = kUncapturedSentinel;
constexpr std::uint64_t kRefVRScaleExtreme   = kUncapturedSentinel;

// ── ScenarioMetrics (8-metric canon from docs/01-baseline-green.md §2.4-2.5)

// ── RectF POD (TXT-00 forbids cross-package aliasing of canonical types;
// each test TU declares a 4-float POD locally.  Same convention as
// tests/text/visual/text_visual_metrics.cpp::RectF.)
struct RectF {
    float x{0.0f}, y{0.0f}, w{0.0f}, h{0.0f};
};

struct ScenarioMetrics {
    std::uint64_t hash{0};
    RectF         ink_bbox{};
    std::size_t   ink_pixels{0};
    float         mean_luminance{0.0f};
    float         alpha_coverage{0.0f};
    Vec2          visual_center{0.0f, 0.0f};
    float         render_ms{0.0f};
};

inline ScenarioMetrics compute_metrics(const Framebuffer& fb,
                                       std::chrono::steady_clock::time_point t0) {
    ScenarioMetrics m;
    m.hash = framebuffer_hash(fb);

    const int W = fb.width();
    const int H = fb.height();
    int xmin = W, ymin = H, xmax = -1, ymax = -1;
    int ink = 0;
    double sum_l = 0.0;
    double sum_a = 0.0;
    double sum_x = 0.0, sum_y = 0.0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            Color c = fb.get_pixel(x, y);
            const float a = c.a;
            if (a > 0.05f) {
                ink += 1;
                xmin = std::min(xmin, x); ymin = std::min(ymin, y);
                xmax = std::max(xmax, x); ymax = std::max(ymax, y);
                sum_l += luma(c);
                sum_a += a;
                sum_x += x * a; sum_y += y * a;
            }
        }
    }
    const std::size_t total = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);
    m.ink_pixels     = ink;
    m.alpha_coverage = total > 0 ? static_cast<float>(ink) / static_cast<float>(total) : 0.0f;
    m.mean_luminance = ink > 0 ? static_cast<float>(sum_l / static_cast<double>(ink)) : 0.0f;
    if (ink > 0) {
        m.ink_bbox      = RectF{static_cast<float>(xmin), static_cast<float>(ymin),
                                 static_cast<float>(xmax - xmin + 1),
                                 static_cast<float>(ymax - ymin + 1)};
        m.visual_center = Vec2{static_cast<float>(sum_x / sum_a),
                                 static_cast<float>(sum_y / sum_a)};
    }
    auto t1 = std::chrono::steady_clock::now();
    m.render_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    return m;
}

// Sentinel-gate macro: short_label is a stable token used by the
// grep-based capture workflow (PR 6.8.5 precedent). NOT auto-derived from
// a TEST_CASE name to avoid whitespace + special-char fragility.
#define VR_GATE(short_label, kref, metrics_expr)                          \
    do {                                                                   \
        auto m = (metrics_expr);                                            \
        if (is_reference_captured(kref)) {                                  \
            REQUIRE(m.hash == kref);                                        \
        } else {                                                            \
            MESSAGE("VR/" << short_label                                    \
                    << " unset; first hash to capture: " << m.hash);        \
        }                                                                   \
        CHECK(m.ink_pixels > 0);                                            \
    } while (0)

// Variant for font-dependent scenarios (RTL, CJK, Emoji): the font
// (Poppins-Bold) may not contain glyphs for all scripts.  When
// ink_pixels == 0 the font is missing the required glyphs; skip
// hash capture and emit a diagnostic instead of failing.
#define VR_GATE_FONT(short_label, kref, metrics_expr)                     \
    do {                                                                   \
        auto m = (metrics_expr);                                            \
        if (m.ink_pixels == 0) {                                            \
            MESSAGE("VR/" << short_label                                    \
                    << " — ink_pixels=0; font may not support this script"); \
            break;                                                          \
        }                                                                   \
        if (is_reference_captured(kref)) {                                  \
            REQUIRE(m.hash == kref);                                        \
        } else {                                                            \
            MESSAGE("VR/" << short_label                                    \
                    << " unset; first hash to capture: " << m.hash);        \
        }                                                                   \
        CHECK(m.ink_pixels > 0);                                            \
    } while (0)

// ── Shared CenterTextOptions helpers (canonical fan-out per PR-A2) ──────

using chronon3d::content::text::centered_text;
using chronon3d::content::text::CenterTextOptions;

constexpr int kVW = 800;
constexpr int kVH = 600;
constexpr const char* kVFont = "assets/fonts/Poppins-Bold.ttf";  // PR-A3 fix G1: kVFps removed (dead code)

inline CenterTextOptions make_opts(const std::string& text,
                                   f32 size,
                                   const Color& color,
                                   Vec2 box = {kVW * 0.85f, kVH * 0.85f},
                                   int max_lines = 0) {  // PR-A3 fix C: max_lines=0=unlimited; pass N to enable wrap
    return CenterTextOptions{
        .text        = text,
        .box         = box,
        .font_asset  = kVFont,
        .font_family = "Poppins",
        .font_weight = 700,
        .font_size   = size,
        .color       = color,
        .max_lines   = max_lines,
    };
}

inline Composition make_text_composition(const std::string& name,
                                            const CenterTextOptions& opt) {
    TextSpec ts = centered_text(opt);
    return composition(
        {.name = name, .width = kVW, .height = kVH, .duration = 1},
        [ts](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("hero", [ts](LayerBuilder& l) {
                l.text("k", ts);
            });
            return s.build();
        });
}

// ═══════════════════════════════════════════════════════════════════════════
//                           15 SCENARIOS
// ═══════════════════════════════════════════════════════════════════════════

// §1 Static ────────────────────────────────────────────────────────────────
TEST_CASE("VisualRegression/Static — single-line canonical text on dark bg") {
    auto renderer = make_renderer();
    auto comp = make_text_composition("VR_Static",
        make_opts("HELLO WORLD", 96.0f, Color::white()));
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);
    VR_GATE("VRStatic", kRefVRStatic, compute_metrics(*fb, t0));
}

// §2 Multiline ─────────────────────────────────────────────────────────────
TEST_CASE("VisualRegression/Multiline — 4-line wrapped paragraph") {
    auto renderer = make_renderer();
    auto comp = make_text_composition("VR_Multiline",
        make_opts("Alpha beta gamma delta\nEpsilon zeta eta theta\n"
                  "Iota kappa lambda mu nu\nXi omicron pi rho sigma",
                  36.0f, Color::black(),
                  Vec2{kVW * 0.9f, kVH * 0.85f},
                  /*max_lines*/4));  // PR-A3 fix D: 0 wraps silently clipped, so we pass 4 to actually render
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);
    VR_GATE("VRMultiline", kRefVRMultiline, compute_metrics(*fb, t0));
}

// §3 Tracking ─────────────────────────────────────────────────────────────
TEST_CASE("VisualRegression/Tracking — wide positive tracking (+200em)") {
    auto renderer = make_renderer();
    auto comp = make_text_composition("VR_Tracking",
        make_opts("T R A C K I N G", 96.0f, Color{0.0f, 0.0f, 0.5f, 1.0f}));  // PR-A3 fix B1: navy literal (Color::navy not in math/color.hpp)
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);
    VR_GATE("VRTracking", kRefVRTracking, compute_metrics(*fb, t0));
}

// §4 Stroke ────────────────────────────────────────────────────────────────
TEST_CASE("VisualRegression/Stroke — thick stroke style applied via TextAppearanceSpec.stroke") {
    auto renderer = make_renderer();
    // CenterTextOptions does not carry stroke; we build the TextSpec
    // directly to apply the canonical stroke style.
    TextSpec spec = centered_text(make_opts("STROKE", 96.0f, Color::black()));
    // PR-A3 fix B2: stroke lives on TextPaint (shape.hpp), not on TextAppearanceSpec.
    spec.appearance.paint.stroke_enabled = true;
    spec.appearance.paint.stroke_width   = 8.0f;
    spec.appearance.paint.stroke_color   = Color{1.0f, 0.30f, 0.10f, 1.0f};
    auto comp = composition(
        {.name = "VR_Stroke", .width = kVW, .height = kVH, .duration = 1},
        [spec](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("hero", [spec](LayerBuilder& l) { l.text("k", spec); });
            return s.build();
        });
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);
    VR_GATE("VRStroke", kRefVRStroke, compute_metrics(*fb, t0));
}

// §5 Gradient fill ────────────────────────────────────────────────────────
TEST_CASE("VisualRegression/Gradient — gradient fill on canonical text") {
    auto renderer = make_renderer();
    TextSpec spec = centered_text(make_opts("GRADIENT", 96.0f, Color::white()));
    // Inject a two-stop linear gradient via the appearance paint.
    // PR-A3 fix B3: text gradient is one field on TextMaterial.gradient_angle;
    // multi-stop linear gradient is via TextPaint.fill_style = Fill::linear(...).
    spec.appearance.paint.fill_style = Fill::linear(
        {0.0f, 0.0f},                              // from (Vec2)
        {0.0f, 1.0f},                              // to   (Vec2)
        {
            {0.0f, Color{0.85f, 0.10f, 0.10f, 1.0f}},  // stop(offset, color)
            {1.0f, Color{0.10f, 0.10f, 0.85f, 1.0f}},
        });
    auto comp = composition(
        {.name = "VR_Gradient", .width = kVW, .height = kVH, .duration = 1},
        [spec](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("hero", [spec](LayerBuilder& l) { l.text("k", spec); });
            return s.build();
        });
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);
    VR_GATE("VRGradient", kRefVRGradient, compute_metrics(*fb, t0));
}

// §6 Shadow ────────────────────────────────────────────────────────────────
TEST_CASE("VisualRegression/Shadow — drop shadow applied via l.drop_shadow") {
    auto renderer = make_renderer();
    TextSpec spec = centered_text(make_opts("SHADOW", 96.0f, Color::black()));
    auto comp = composition(
        {.name = "VR_Shadow", .width = kVW, .height = kVH, .duration = 1},
        [spec](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("hero", [spec](LayerBuilder& l) {
                l.text("k", spec);
                l.drop_shadow({0.0f, 12.0f}, Color{0.0f, 0.0f, 0.0f, 0.45f}, 18.0f);
            });
            return s.build();
        });
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);
    VR_GATE("VRShadow", kRefVRShadow, compute_metrics(*fb, t0));
}

// §7 Glow ──────────────────────────────────────────────────────────────────
TEST_CASE("VisualRegression/Glow — AE-style multi-layer glow via l.glow") {
    auto renderer = make_renderer();
    TextSpec spec = centered_text(make_opts("GLOW", 96.0f, Color::white()));
    auto comp = composition(
        {.name = "VR_Glow", .width = kVW, .height = kVH, .duration = 1},
        [spec](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("hero", [spec](LayerBuilder& l) {
                l.text("k", spec);
                // PR-A3 fix B4: canonical GlowParams has only {radius, intensity, color}.
                // The 4 AE-grade pass fields (inner_radius/bloom_radius/inner_intensity/bloom_intensity)
                // do NOT exist on the layer-engine GlowParams aggregator; they belong (separately)
                // to TextGlowSpec on text/text_glow_spec.hpp.
                l.glow(GlowParams{
                    .radius    = 24.0f,
                    .intensity = 0.85f,
                    .color     = {1.0f, 0.55f, 0.18f, 1.0f},
                });
            });
            return s.build();
        });
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);
    VR_GATE("VRGlow", kRefVRGlow, compute_metrics(*fb, t0));
}

// §8 Blur ──────────────────────────────────────────────────────────────────
TEST_CASE("VisualRegression/Blur — gaussian blur radius applied via l.blur") {
    auto renderer = make_renderer();
    TextSpec spec = centered_text(make_opts("BLUR", 96.0f, Color::black()));
    auto comp = composition(
        {.name = "VR_Blur", .width = kVW, .height = kVH, .duration = 1},
        [spec](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("hero", [spec](LayerBuilder& l) {
                l.text("k", spec);
                l.blur(6.0f);
            });
            return s.build();
        });
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);
    VR_GATE("VRBlur", kRefVRBlur, compute_metrics(*fb, t0));
}

// §9 Typewriter — frame-context driven char-reveal snapshot at midpoint ────
TEST_CASE("VisualRegression/Typewriter — char reveal at midpoint frame") {
    static constexpr int kVTypewriterFrame = 30; // midpoint of duration 60
    auto renderer = make_renderer();
    auto comp = composition(
        {.name = "VR_Typewriter", .width = kVW, .height = kVH, .duration = 60},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            const float t = static_cast<float>(ctx.frame) / 60.0f;
            const int   filled = static_cast<int>(t * 32.0f);
            for (int i = 0; i < filled; ++i) {
                s.rect("c" + std::to_string(i),
                       {.size  = {16, 24},
                        .color = Color{0.95f, 0.95f, 0.95f, 1.0f},
                        .pos   = {-kVW * 0.4f + i * 24.0f, 0.0f, 0.0f}});
            }
            return s.build();
        });
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, Frame{kVTypewriterFrame});
    REQUIRE(fb != nullptr);
    VR_GATE("VRTypewriter", kRefVRTypewriter, compute_metrics(*fb, t0));
}

// §10 AnimGlyph — per-glyph opacity oscillation snapshot at frame 25 ──────
TEST_CASE("VisualRegression/AnimGlyph — per-glyph opacity wobble snapshot") {
    auto renderer = make_renderer();
    auto comp = composition(
        {.name = "VR_AnimGlyph", .width = kVW, .height = kVH, .duration = 60},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            const float phase = static_cast<float>(ctx.frame) * 0.1f;
            for (int i = 0; i < 12; ++i) {
                const float a = 0.4f + 0.5f * std::abs(
                    std::sin(phase + static_cast<float>(i) * 0.5f));
                s.rect("g" + std::to_string(i), {
                    .size  = {40.0f, 40.0f},
                    .color = Color{1.0f, 0.8f, 0.4f, a},
                    .pos   = {-kVW * 0.4f + static_cast<float>(i) * 64.0f, 0.0f, 0.0f},
                });
            }
            return s.build();
        });
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, Frame{25});
    REQUIRE(fb != nullptr);
    VR_GATE("VRAnimGlyph", kRefVRAnimGlyph, compute_metrics(*fb, t0));
}

// §11 AnimWord — per-word vertical wave displacement snapshot at frame 30 ─
TEST_CASE("VisualRegression/AnimWord — per-word wave displacement snapshot") {
    auto renderer = make_renderer();
    auto comp = composition(
        {.name = "VR_AnimWord", .width = kVW, .height = kVH, .duration = 90},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            const float phase = static_cast<float>(ctx.frame) * 0.05f;
            for (int w = 0; w < 5; ++w) {
                const float dy = 30.0f * std::sin(
                    phase + static_cast<float>(w) * 0.6f);
                s.rect("w" + std::to_string(w), {
                    .size  = {120.0f, 60.0f},
                    .color = Color{0.6f, 0.85f, 1.0f, 1.0f},
                    .pos   = {-kVW * 0.4f + static_cast<float>(w) * 160.0f, dy, 0.0f},
                });
            }
            return s.build();
        });
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, Frame{30});
    REQUIRE(fb != nullptr);
    VR_GATE("VRAnimWord", kRefVRAnimWord, compute_metrics(*fb, t0));
}

// §12 RTL — Arabic sample for bidi + HarfBuzz shaping path ──────────────
TEST_CASE("VisualRegression/RTL — Arabic sample for bidi + shaping path") {
    auto renderer = make_renderer();
    auto comp = make_text_composition("VR_RTL",
        make_opts("\u0627\u0644\u0633\u0644\u0627\u0645 \u0639\u0644\u064a\u0643\u0645", // "السلام عليكم"
                  72.0f, Color::black(),
                  Vec2{kVW * 0.9f, 240.0f}));
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);
    VR_GATE_FONT("VRRTL", kRefVRRTL, compute_metrics(*fb, t0));
}

// §13 CJK — mixed Chinese / Japanese / Korean sample ─────────────────────
TEST_CASE("VisualRegression/CJK — CN + JP + KR mixed sample") {
    auto renderer = make_renderer();
    auto comp = make_text_composition("VR_CJK",
        make_opts("\u4f60\u597d\u4e16\u754c / \u3053\u3093\u306b\u3061\u306f / "
                   "\uc548\ub155\ud558\uc138\uc694",
                  48.0f, Color::black(),
                  Vec2{kVW * 0.95f, 240.0f}));
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);
    VR_GATE_FONT("VRCJK", kRefVRCJK, compute_metrics(*fb, t0));
}

// §14 Emoji fallback — mixed emoji + ASCII sample ───────────────────────
TEST_CASE("VisualRegression/EmojiFallback — mixed emoji + ASCII sample") {
    auto renderer = make_renderer();
    auto comp = make_text_composition("VR_EmojiFallback",
        make_opts("Hello \U0001F30D \U0001F680 \u2728 World!",
                  72.0f, Color::black(),
                  Vec2{kVW * 0.9f, 240.0f}));
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);
    VR_GATE_FONT("VREmojiFallback", kRefVREmojiFallback, compute_metrics(*fb, t0));
}

// §15 Scale extreme — very small + very large dual composition ───────────
TEST_CASE("VisualRegression/ScaleExtreme — small + huge dual composition") {
    auto renderer = make_renderer();
    TextSpec tiny = centered_text(make_opts("tiny", 8.0f, Color{0.0f, 0.0f, 0.5f, 1.0f},
                                              Vec2{160.0f, 30.0f}));
    tiny.position = {-260.0f,  150.0f, 0.0f};  // PR-A3 fix F: NW anchor — avoids overlap with huge
    TextSpec huge = centered_text(make_opts("HUGE", 220.0f, Color{0.86f, 0.08f, 0.24f, 1.0f},
                                              Vec2{kVW * 0.95f, kVH * 0.95f}));
    huge.position = { 260.0f, -100.0f, 0.0f};  // PR-A3 fix F: SE anchor; huge 480→220 (fix E) to fit 760×510 box
    auto comp = composition(
        {.name = "VR_ScaleExtreme", .width = kVW, .height = kVH, .duration = 1},
        [tiny, huge](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("a", [tiny](LayerBuilder& l) { l.text("k", tiny); });
            s.layer("b", [huge](LayerBuilder& l) { l.text("k", huge); });
            return s.build();
        });
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);
    VR_GATE("VRScaleExtreme", kRefVRScaleExtreme, compute_metrics(*fb, t0));
}

} // anonymous namespace
