// ═══════════════════════════════════════════════════════════════════════════
// test_text_preset_visual.cpp — PR-A4 (Blocco A, Fase 1 followup)
//
// Sentinel-capture harness for the 16 text-style presets in
// TextPresetRegistry. Each preset gets 8 sentinels:
//   • 4 timestamps (F000 / F020 / F030 / F040) at 16:9 (1920×1080)
//   • 4 timestamps (F000 / F020 / F030 / F040) at 9:16 (1080×1920)
// Total: 16 presets × 8 sentinels = 128 sentinels.
//
// Scope lock (user-approved):
//   Reveal (10)  + Emphasis (4) + Subtitle (2)  = 16
// Excluded from A4 (deferred to A4.1 once bit-stable):
//   • caption_box, glow_pulse     (Subtitle tier 2/4)
//   • animation_compositions,
//     cinematic_text_camera,
//     cinematic_title_reveal,
//     tilt_sweep_title_v2        (Cinematic tier — depend on CameraRig)
//
// Pattern mirrors PR-A3's VR_GATE macro in
// tests/deterministic/test_visual_regression_scenarios.cpp.
//
// Capture workflow (mirror docs/01-baseline-green.md §2.3):
//   ctest --test-dir build/chronon/linux-ci -R 'VRTextPreset' -V 2>&1 \
//     | tee /tmp/vr_text.txt
//   grep -oE 'VR/Text/[A-Za-z0-9_]+ unset; first hash to capture: [0-9]+' \
//     /tmp/vr_text.txt
//   # Copy each <hash> into the matching kRefTextPres<..><Ratio><Fnnn>
//   # constant in this file. Commit. Re-run ctest to engage the gate.
//
// PNG dump path is a no-op for A4 (PNG follow-up A4.png is a sub-PR that
// will wire to tests/helpers/render_regression.hpp::verify_golden_or_create).
//
// ═══════════════════════════════════════════════════════════════════════════
#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>     // PR-A4 review-fix: explicit FrameRate brace-init (transitively reachable, but explicit for grep clarity + future-proofness)
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/helpers/pixel_assertions.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <chronon3d/registry/text_preset_registry.hpp>
#include <content/text/text_helpers.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::test;
using chronon3d::content::text::centered_text;
// TICKET-038 / TXT-00 — bring the canonical text-options + registry
// symbols into TU scope. `CenterTextOptions` lives in
// `chronon3d::content::text` (sibling of `centered_text`); the registry
// trio (`TextPreset` / `TextPresetRegistry` /
// `make_default_text_preset_registry`) lives in `chronon3d::registry`
// and is not transitively pulled by `using namespace chronon3d`,
// so it must be qualified explicitly.
using chronon3d::content::text::CenterTextOptions;
using chronon3d::registry::TextPreset;
using chronon3d::registry::TextPresetRegistry;
using chronon3d::registry::make_default_text_preset_registry;

namespace {

// ── Sentinel-capture helpers (mirror PR-A3 file) ─────────────────────────
constexpr std::uint64_t kUncapturedSentinel = 0xDEADBEEFDEADBEEFULL;

inline bool is_reference_captured(std::uint64_t r) noexcept {
    return r != kUncapturedSentinel;
}

// TICKET-038 / TXT-00 — POD `RectF`. The SDK has no public canonical
// RectF; the 8-metric ScenarioMetrics canon (PR-A3 /
// docs/01-baseline-green.md §2.4-2.5) uses
// `RectF{float,float,float,float}` (x0,y0,w,h). TXT-00 forbids cross-
// package aliasing and renaming canonical types, so each test TU that
// needs the metric declares a 4-float POD inside its own anonymous
// namespace. Default member initializers keep it an aggregate under
// C++17/20, so both `RectF{}` (value-init) and aggregate positional
// initialisation (`RectF{a,b,c,d}`) still compile.
struct RectF {
    float x{0.0f}, y{0.0f}, w{0.0f}, h{0.0f};
};

// ── 8-metric ScenarioMetrics canon (PR-A3 / docs/01-baseline-green.md §2.4-2.5)
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

// Sentinel-gate macro for Text Preset visual regression. Comments live
// OUTSIDE the macro body to avoid comment-inside-backslash-newline semantics
// (reviewer feedback on PR-A3 amend). 'short_label' is a stable token used
// by the grep-based capture workflow. NOT auto-derived from a TEST_CASE name
// to avoid whitespace / special-character fragility.
//
// TICKET-038 / TXT-00 — `emit_preset_gate` declares an outer
// `auto m = compute_metrics(...)` and passes `m` as `metrics_expr`
// here; the original macro body re-declared `auto m = (m)` which the
// compiler correctly rejects ("use of 'm' before deduction of 'auto'")
// because the RHS `m` referred to the freshly-introduced local, not
// the outer.  Renamed the macro-local binding to `gate_m` so the RHS
// unambiguously refers to the caller's `m` (or to a fresh expression
// when the caller passes one inline). TU-local rename; no canonical
// type touched.
#define VR_TEXT_PRESET_GATE(short_label, kref, metrics_expr)              \
    do {                                                                   \
        auto gate_m = (metrics_expr);                                       \
        if (is_reference_captured(kref)) {                                  \
            REQUIRE(gate_m.hash == kref);                                   \
        } else {                                                            \
            MESSAGE("VR/Text/" << short_label                               \
                    << " unset; first hash to capture: " << gate_m.hash);   \
        }                                                                   \
        CHECK(gate_m.ink_pixels > 0);                                       \
    } while (0)

// ── Aspect ratio helpers ─────────────────────────────────────────────────
enum class AspectRatio : int { k16x9 = 0, k9x16 = 1 };

struct AspectSpec { int width; int height; };

inline AspectSpec aspect_dims(AspectRatio r) {
    return r == AspectRatio::k16x9 ? AspectSpec{1920, 1080}
                                    : AspectSpec{1080, 1920};
}

// ── 128 sentinel constants (16 presets × 8 each) via DECLARE_TEXT_PRESET_REFS
#define DECLARE_TEXT_PRESET_REFS(prefix)                                  \
    constexpr std::uint64_t kRefTextPres##prefix##_169_F000 = kUncapturedSentinel; \
    constexpr std::uint64_t kRefTextPres##prefix##_169_F020 = kUncapturedSentinel; \
    constexpr std::uint64_t kRefTextPres##prefix##_169_F030 = kUncapturedSentinel; \
    constexpr std::uint64_t kRefTextPres##prefix##_169_F040 = kUncapturedSentinel; \
    constexpr std::uint64_t kRefTextPres##prefix##_916_F000 = kUncapturedSentinel; \
    constexpr std::uint64_t kRefTextPres##prefix##_916_F020 = kUncapturedSentinel; \
    constexpr std::uint64_t kRefTextPres##prefix##_916_F030 = kUncapturedSentinel; \
    constexpr std::uint64_t kRefTextPres##prefix##_916_F040 = kUncapturedSentinel;

// Reveal (10)
DECLARE_TEXT_PRESET_REFS(TextAnimations)
DECLARE_TEXT_PRESET_REFS(FadeIn)
DECLARE_TEXT_PRESET_REFS(BlurIn)
DECLARE_TEXT_PRESET_REFS(SlideUp)
DECLARE_TEXT_PRESET_REFS(SlideDown)
DECLARE_TEXT_PRESET_REFS(ScaleIn)
DECLARE_TEXT_PRESET_REFS(TrackingClose)
DECLARE_TEXT_PRESET_REFS(MaskedLineReveal)
DECLARE_TEXT_PRESET_REFS(WordCascade)
DECLARE_TEXT_PRESET_REFS(CharacterCascade)
// Emphasis (4)
DECLARE_TEXT_PRESET_REFS(WordPop)
DECLARE_TEXT_PRESET_REFS(ScalePunch)
DECLARE_TEXT_PRESET_REFS(ColorAccent)
DECLARE_TEXT_PRESET_REFS(GradientFill)
// Subtitle (2 — caption_box + glow_pulse deferred to A4.1)
DECLARE_TEXT_PRESET_REFS(MinimalWhite)
DECLARE_TEXT_PRESET_REFS(YellowKeyword)

#undef DECLARE_TEXT_PRESET_REFS

// ── Helper: CenterTextOptions builder for preset-text baseline ───────────
inline CenterTextOptions make_preset_base_opts(const std::string& text,
                                                AspectSpec d) {
    constexpr const char* kVFont = "assets/fonts/Poppins-Bold.ttf";
    const f32 font_size = (d.width >= d.height) ? 96.0f : 64.0f;
    return CenterTextOptions{
        .text        = text,
        .box         = Vec2{d.width * 0.85f, d.height * 0.85f},
        .font_path   = kVFont,
        .font_family = "Poppins",
        .font_weight = 700,
        .font_size   = font_size,
        .color       = Color::white(),
    };
}

// ── Single TextPresetRegistry instance shared across all 16 TEST_CASEs.
//    make_default_text_preset_registry() inside the function would otherwise
//    re-build + re-register all 22 entries 128 times (once per emit). Lifting
//    to file-scope reduces this to 1 construction. (PR-A4 code-reviewer fix.)
//
// Freeze posture: the canonical "default text preset registry" is frozen
// immediately after the 22 builtin entries seed it — mirrors the camera
// catalog posture at src/scene/camera/camera_v1/register_camera_v1.cpp:53
// and EffectCatalog at src/runtime/render_runtime.cpp:117. 16 TEST_CASEs
// only READ the registry via `kTextPresetRegistry.get(preset_id)`, so
// the freeze does NOT break the visual harness.
static const TextPresetRegistry kTextPresetRegistry = []{
    auto reg = make_default_text_preset_registry();
    reg.freeze();
    return reg;
}();

// ── Build a Composition that applies the named preset at a specific
//    (ratio, frame) point. `fps` is fixed at 30 to keep the F0/F20/F30/F40
//    sentinel axis deterministic across machines; coverage of 24/60 fps is
//    deferred to Cleanup #7 follow-up.
//
//    Wires through kTextPresetRegistry (canonical resolver per Cleanup #2)
//    and forwards the entry's builder std::function on the named layer.
//    The frame-context snapshot is taken at exactly t_frame (single-frame
//    render via renderer.render_frame(comp, Frame{t_frame})), so the old
//    "if (ctx.frame != t_frame) return s.build();" early-exit was dead code
//    and is dropped here. (PR-A4 code-reviewer fix.)
inline Composition build_preset_composition(const std::string& preset_id,
                                             AspectRatio r,
                                             int t_frame,
                                             int fps = 30) {
    AspectSpec d = aspect_dims(r);
    const TextPreset& preset = kTextPresetRegistry.get(preset_id);  // throws if id absent

    return composition(
        {.name = std::string("VR/Text/") + preset_id + "_" +
                  (r == AspectRatio::k16x9 ? "169" : "916"),
         .width = d.width, .height = d.height,
         .frame_rate = FrameRate{fps, 1},
         .duration = 60},
        [preset, preset_id, t_frame, r](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            TextSpec base = centered_text(
                make_preset_base_opts("THE QUICK BROWN FOX JUMPS",
                                       aspect_dims(r)));
            s.layer("hero", [&s, &preset, base](LayerBuilder& l) {
                l.text("k", base);
                if (preset.builder) {
                    preset.builder(s, l, base);
                }
            });
            return s.build();
        });
}

// ── Helper: render one (preset, ratio, frame) point and route through
//    the sentinel gate. One line per emit call keeps the 16 TEST_CASE
//    bodies short and grep-friendly for capture workflow.
inline void emit_preset_gate(SoftwareRenderer& renderer,
                              const std::string& preset_id,
                              AspectRatio r,
                              int t_frame,
                              std::uint64_t kref,
                              const std::string& short_label) {
    auto comp = build_preset_composition(preset_id, r, t_frame, 30);
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render_frame(comp, Frame{t_frame});
    REQUIRE(fb != nullptr);
    auto m = compute_metrics(*fb, t0);
    VR_TEXT_PRESET_GATE(short_label, kref, m);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
//  16 TEST_CASEs — one per preset. Each TEST_CASE exercises 8 sentinels
//  (4 timestamps at 16:9 + 4 timestamps at 9:16).
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("VRTextPreset/TextAnimations") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "text_animations", AspectRatio::k16x9, 0,  kRefTextPresTextAnimations_169_F000, "TextAnimations_169_F000");
    emit_preset_gate(renderer, "text_animations", AspectRatio::k16x9, 20, kRefTextPresTextAnimations_169_F020, "TextAnimations_169_F020");
    emit_preset_gate(renderer, "text_animations", AspectRatio::k16x9, 30, kRefTextPresTextAnimations_169_F030, "TextAnimations_169_F030");
    emit_preset_gate(renderer, "text_animations", AspectRatio::k16x9, 40, kRefTextPresTextAnimations_169_F040, "TextAnimations_169_F040");
    emit_preset_gate(renderer, "text_animations", AspectRatio::k9x16, 0,  kRefTextPresTextAnimations_916_F000, "TextAnimations_916_F000");
    emit_preset_gate(renderer, "text_animations", AspectRatio::k9x16, 20, kRefTextPresTextAnimations_916_F020, "TextAnimations_916_F020");
    emit_preset_gate(renderer, "text_animations", AspectRatio::k9x16, 30, kRefTextPresTextAnimations_916_F030, "TextAnimations_916_F030");
    emit_preset_gate(renderer, "text_animations", AspectRatio::k9x16, 40, kRefTextPresTextAnimations_916_F040, "TextAnimations_916_F040");
}

TEST_CASE("VRTextPreset/FadeIn") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "fade_in", AspectRatio::k16x9, 0,  kRefTextPresFadeIn_169_F000, "FadeIn_169_F000");
    emit_preset_gate(renderer, "fade_in", AspectRatio::k16x9, 20, kRefTextPresFadeIn_169_F020, "FadeIn_169_F020");
    emit_preset_gate(renderer, "fade_in", AspectRatio::k16x9, 30, kRefTextPresFadeIn_169_F030, "FadeIn_169_F030");
    emit_preset_gate(renderer, "fade_in", AspectRatio::k16x9, 40, kRefTextPresFadeIn_169_F040, "FadeIn_169_F040");
    emit_preset_gate(renderer, "fade_in", AspectRatio::k9x16, 0,  kRefTextPresFadeIn_916_F000, "FadeIn_916_F000");
    emit_preset_gate(renderer, "fade_in", AspectRatio::k9x16, 20, kRefTextPresFadeIn_916_F020, "FadeIn_916_F020");
    emit_preset_gate(renderer, "fade_in", AspectRatio::k9x16, 30, kRefTextPresFadeIn_916_F030, "FadeIn_916_F030");
    emit_preset_gate(renderer, "fade_in", AspectRatio::k9x16, 40, kRefTextPresFadeIn_916_F040, "FadeIn_916_F040");
}

TEST_CASE("VRTextPreset/BlurIn") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "blur_in", AspectRatio::k16x9, 0,  kRefTextPresBlurIn_169_F000, "BlurIn_169_F000");
    emit_preset_gate(renderer, "blur_in", AspectRatio::k16x9, 20, kRefTextPresBlurIn_169_F020, "BlurIn_169_F020");
    emit_preset_gate(renderer, "blur_in", AspectRatio::k16x9, 30, kRefTextPresBlurIn_169_F030, "BlurIn_169_F030");
    emit_preset_gate(renderer, "blur_in", AspectRatio::k16x9, 40, kRefTextPresBlurIn_169_F040, "BlurIn_169_F040");
    emit_preset_gate(renderer, "blur_in", AspectRatio::k9x16, 0,  kRefTextPresBlurIn_916_F000, "BlurIn_916_F000");
    emit_preset_gate(renderer, "blur_in", AspectRatio::k9x16, 20, kRefTextPresBlurIn_916_F020, "BlurIn_916_F020");
    emit_preset_gate(renderer, "blur_in", AspectRatio::k9x16, 30, kRefTextPresBlurIn_916_F030, "BlurIn_916_F030");
    emit_preset_gate(renderer, "blur_in", AspectRatio::k9x16, 40, kRefTextPresBlurIn_916_F040, "BlurIn_916_F040");
}

TEST_CASE("VRTextPreset/SlideUp") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "slide_up", AspectRatio::k16x9, 0,  kRefTextPresSlideUp_169_F000, "SlideUp_169_F000");
    emit_preset_gate(renderer, "slide_up", AspectRatio::k16x9, 20, kRefTextPresSlideUp_169_F020, "SlideUp_169_F020");
    emit_preset_gate(renderer, "slide_up", AspectRatio::k16x9, 30, kRefTextPresSlideUp_169_F030, "SlideUp_169_F030");
    emit_preset_gate(renderer, "slide_up", AspectRatio::k16x9, 40, kRefTextPresSlideUp_169_F040, "SlideUp_169_F040");
    emit_preset_gate(renderer, "slide_up", AspectRatio::k9x16, 0,  kRefTextPresSlideUp_916_F000, "SlideUp_916_F000");
    emit_preset_gate(renderer, "slide_up", AspectRatio::k9x16, 20, kRefTextPresSlideUp_916_F020, "SlideUp_916_F020");
    emit_preset_gate(renderer, "slide_up", AspectRatio::k9x16, 30, kRefTextPresSlideUp_916_F030, "SlideUp_916_F030");
    emit_preset_gate(renderer, "slide_up", AspectRatio::k9x16, 40, kRefTextPresSlideUp_916_F040, "SlideUp_916_F040");
}

TEST_CASE("VRTextPreset/SlideDown") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "slide_down", AspectRatio::k16x9, 0,  kRefTextPresSlideDown_169_F000, "SlideDown_169_F000");
    emit_preset_gate(renderer, "slide_down", AspectRatio::k16x9, 20, kRefTextPresSlideDown_169_F020, "SlideDown_169_F020");
    emit_preset_gate(renderer, "slide_down", AspectRatio::k16x9, 30, kRefTextPresSlideDown_169_F030, "SlideDown_169_F030");
    emit_preset_gate(renderer, "slide_down", AspectRatio::k16x9, 40, kRefTextPresSlideDown_169_F040, "SlideDown_169_F040");
    emit_preset_gate(renderer, "slide_down", AspectRatio::k9x16, 0,  kRefTextPresSlideDown_916_F000, "SlideDown_916_F000");
    emit_preset_gate(renderer, "slide_down", AspectRatio::k9x16, 20, kRefTextPresSlideDown_916_F020, "SlideDown_916_F020");
    emit_preset_gate(renderer, "slide_down", AspectRatio::k9x16, 30, kRefTextPresSlideDown_916_F030, "SlideDown_916_F030");
    emit_preset_gate(renderer, "slide_down", AspectRatio::k9x16, 40, kRefTextPresSlideDown_916_F040, "SlideDown_916_F040");
}

TEST_CASE("VRTextPreset/ScaleIn") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "scale_in", AspectRatio::k16x9, 0,  kRefTextPresScaleIn_169_F000, "ScaleIn_169_F000");
    emit_preset_gate(renderer, "scale_in", AspectRatio::k16x9, 20, kRefTextPresScaleIn_169_F020, "ScaleIn_169_F020");
    emit_preset_gate(renderer, "scale_in", AspectRatio::k16x9, 30, kRefTextPresScaleIn_169_F030, "ScaleIn_169_F030");
    emit_preset_gate(renderer, "scale_in", AspectRatio::k16x9, 40, kRefTextPresScaleIn_169_F040, "ScaleIn_169_F040");
    emit_preset_gate(renderer, "scale_in", AspectRatio::k9x16, 0,  kRefTextPresScaleIn_916_F000, "ScaleIn_916_F000");
    emit_preset_gate(renderer, "scale_in", AspectRatio::k9x16, 20, kRefTextPresScaleIn_916_F020, "ScaleIn_916_F020");
    emit_preset_gate(renderer, "scale_in", AspectRatio::k9x16, 30, kRefTextPresScaleIn_916_F030, "ScaleIn_916_F030");
    emit_preset_gate(renderer, "scale_in", AspectRatio::k9x16, 40, kRefTextPresScaleIn_916_F040, "ScaleIn_916_F040");
}

TEST_CASE("VRTextPreset/TrackingClose") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k16x9, 0,  kRefTextPresTrackingClose_169_F000, "TrackingClose_169_F000");
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k16x9, 20, kRefTextPresTrackingClose_169_F020, "TrackingClose_169_F020");
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k16x9, 30, kRefTextPresTrackingClose_169_F030, "TrackingClose_169_F030");
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k16x9, 40, kRefTextPresTrackingClose_169_F040, "TrackingClose_169_F040");
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k9x16, 0,  kRefTextPresTrackingClose_916_F000, "TrackingClose_916_F000");
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k9x16, 20, kRefTextPresTrackingClose_916_F020, "TrackingClose_916_F020");
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k9x16, 30, kRefTextPresTrackingClose_916_F030, "TrackingClose_916_F030");
    emit_preset_gate(renderer, "tracking_close", AspectRatio::k9x16, 40, kRefTextPresTrackingClose_916_F040, "TrackingClose_916_F040");
}

TEST_CASE("VRTextPreset/MaskedLineReveal") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k16x9, 0,  kRefTextPresMaskedLineReveal_169_F000, "MaskedLineReveal_169_F000");
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k16x9, 20, kRefTextPresMaskedLineReveal_169_F020, "MaskedLineReveal_169_F020");
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k16x9, 30, kRefTextPresMaskedLineReveal_169_F030, "MaskedLineReveal_169_F030");
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k16x9, 40, kRefTextPresMaskedLineReveal_169_F040, "MaskedLineReveal_169_F040");
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k9x16, 0,  kRefTextPresMaskedLineReveal_916_F000, "MaskedLineReveal_916_F000");
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k9x16, 20, kRefTextPresMaskedLineReveal_916_F020, "MaskedLineReveal_916_F020");
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k9x16, 30, kRefTextPresMaskedLineReveal_916_F030, "MaskedLineReveal_916_F030");
    emit_preset_gate(renderer, "masked_line_reveal", AspectRatio::k9x16, 40, kRefTextPresMaskedLineReveal_916_F040, "MaskedLineReveal_916_F040");
}

TEST_CASE("VRTextPreset/WordCascade") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k16x9, 0,  kRefTextPresWordCascade_169_F000, "WordCascade_169_F000");
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k16x9, 20, kRefTextPresWordCascade_169_F020, "WordCascade_169_F020");
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k16x9, 30, kRefTextPresWordCascade_169_F030, "WordCascade_169_F030");
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k16x9, 40, kRefTextPresWordCascade_169_F040, "WordCascade_169_F040");
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k9x16, 0,  kRefTextPresWordCascade_916_F000, "WordCascade_916_F000");
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k9x16, 20, kRefTextPresWordCascade_916_F020, "WordCascade_916_F020");
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k9x16, 30, kRefTextPresWordCascade_916_F030, "WordCascade_916_F030");
    emit_preset_gate(renderer, "word_cascade", AspectRatio::k9x16, 40, kRefTextPresWordCascade_916_F040, "WordCascade_916_F040");
}

TEST_CASE("VRTextPreset/CharacterCascade") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k16x9, 0,  kRefTextPresCharacterCascade_169_F000, "CharacterCascade_169_F000");
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k16x9, 20, kRefTextPresCharacterCascade_169_F020, "CharacterCascade_169_F020");
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k16x9, 30, kRefTextPresCharacterCascade_169_F030, "CharacterCascade_169_F030");
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k16x9, 40, kRefTextPresCharacterCascade_169_F040, "CharacterCascade_169_F040");
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k9x16, 0,  kRefTextPresCharacterCascade_916_F000, "CharacterCascade_916_F000");
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k9x16, 20, kRefTextPresCharacterCascade_916_F020, "CharacterCascade_916_F020");
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k9x16, 30, kRefTextPresCharacterCascade_916_F030, "CharacterCascade_916_F030");
    emit_preset_gate(renderer, "character_cascade", AspectRatio::k9x16, 40, kRefTextPresCharacterCascade_916_F040, "CharacterCascade_916_F040");
}

// ── Emphasis (4) ────────────────────────────────────────────────────────

TEST_CASE("VRTextPreset/WordPop") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "word_pop", AspectRatio::k16x9, 0,  kRefTextPresWordPop_169_F000, "WordPop_169_F000");
    emit_preset_gate(renderer, "word_pop", AspectRatio::k16x9, 20, kRefTextPresWordPop_169_F020, "WordPop_169_F020");
    emit_preset_gate(renderer, "word_pop", AspectRatio::k16x9, 30, kRefTextPresWordPop_169_F030, "WordPop_169_F030");
    emit_preset_gate(renderer, "word_pop", AspectRatio::k16x9, 40, kRefTextPresWordPop_169_F040, "WordPop_169_F040");
    emit_preset_gate(renderer, "word_pop", AspectRatio::k9x16, 0,  kRefTextPresWordPop_916_F000, "WordPop_916_F000");
    emit_preset_gate(renderer, "word_pop", AspectRatio::k9x16, 20, kRefTextPresWordPop_916_F020, "WordPop_916_F020");
    emit_preset_gate(renderer, "word_pop", AspectRatio::k9x16, 30, kRefTextPresWordPop_916_F030, "WordPop_916_F030");
    emit_preset_gate(renderer, "word_pop", AspectRatio::k9x16, 40, kRefTextPresWordPop_916_F040, "WordPop_916_F040");
}

TEST_CASE("VRTextPreset/ScalePunch") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k16x9, 0,  kRefTextPresScalePunch_169_F000, "ScalePunch_169_F000");
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k16x9, 20, kRefTextPresScalePunch_169_F020, "ScalePunch_169_F020");
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k16x9, 30, kRefTextPresScalePunch_169_F030, "ScalePunch_169_F030");
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k16x9, 40, kRefTextPresScalePunch_169_F040, "ScalePunch_169_F040");
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k9x16, 0,  kRefTextPresScalePunch_916_F000, "ScalePunch_916_F000");
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k9x16, 20, kRefTextPresScalePunch_916_F020, "ScalePunch_916_F020");
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k9x16, 30, kRefTextPresScalePunch_916_F030, "ScalePunch_916_F030");
    emit_preset_gate(renderer, "scale_punch", AspectRatio::k9x16, 40, kRefTextPresScalePunch_916_F040, "ScalePunch_916_F040");
}

TEST_CASE("VRTextPreset/ColorAccent") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "color_accent", AspectRatio::k16x9, 0,  kRefTextPresColorAccent_169_F000, "ColorAccent_169_F000");
    emit_preset_gate(renderer, "color_accent", AspectRatio::k16x9, 20, kRefTextPresColorAccent_169_F020, "ColorAccent_169_F020");
    emit_preset_gate(renderer, "color_accent", AspectRatio::k16x9, 30, kRefTextPresColorAccent_169_F030, "ColorAccent_169_F030");
    emit_preset_gate(renderer, "color_accent", AspectRatio::k16x9, 40, kRefTextPresColorAccent_169_F040, "ColorAccent_169_F040");
    emit_preset_gate(renderer, "color_accent", AspectRatio::k9x16, 0,  kRefTextPresColorAccent_916_F000, "ColorAccent_916_F000");
    emit_preset_gate(renderer, "color_accent", AspectRatio::k9x16, 20, kRefTextPresColorAccent_916_F020, "ColorAccent_916_F020");
    emit_preset_gate(renderer, "color_accent", AspectRatio::k9x16, 30, kRefTextPresColorAccent_916_F030, "ColorAccent_916_F030");
    emit_preset_gate(renderer, "color_accent", AspectRatio::k9x16, 40, kRefTextPresColorAccent_916_F040, "ColorAccent_916_F040");
}

TEST_CASE("VRTextPreset/GradientFill") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k16x9, 0,  kRefTextPresGradientFill_169_F000, "GradientFill_169_F000");
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k16x9, 20, kRefTextPresGradientFill_169_F020, "GradientFill_169_F020");
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k16x9, 30, kRefTextPresGradientFill_169_F030, "GradientFill_169_F030");
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k16x9, 40, kRefTextPresGradientFill_169_F040, "GradientFill_169_F040");
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k9x16, 0,  kRefTextPresGradientFill_916_F000, "GradientFill_916_F000");
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k9x16, 20, kRefTextPresGradientFill_916_F020, "GradientFill_916_F020");
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k9x16, 30, kRefTextPresGradientFill_916_F030, "GradientFill_916_F030");
    emit_preset_gate(renderer, "gradient_fill", AspectRatio::k9x16, 40, kRefTextPresGradientFill_916_F040, "GradientFill_916_F040");
}

// ── Subtitle (2 — caption_box + glow_pulse deferred to A4.1) ─────────────

TEST_CASE("VRTextPreset/MinimalWhite") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k16x9, 0,  kRefTextPresMinimalWhite_169_F000, "MinimalWhite_169_F000");
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k16x9, 20, kRefTextPresMinimalWhite_169_F020, "MinimalWhite_169_F020");
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k16x9, 30, kRefTextPresMinimalWhite_169_F030, "MinimalWhite_169_F030");
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k16x9, 40, kRefTextPresMinimalWhite_169_F040, "MinimalWhite_169_F040");
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k9x16, 0,  kRefTextPresMinimalWhite_916_F000, "MinimalWhite_916_F000");
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k9x16, 20, kRefTextPresMinimalWhite_916_F020, "MinimalWhite_916_F020");
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k9x16, 30, kRefTextPresMinimalWhite_916_F030, "MinimalWhite_916_F030");
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k9x16, 40, kRefTextPresMinimalWhite_916_F040, "MinimalWhite_916_F040");
}

TEST_CASE("VRTextPreset/YellowKeyword") {
    auto renderer = make_renderer();
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k16x9, 0,  kRefTextPresYellowKeyword_169_F000, "YellowKeyword_169_F000");
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k16x9, 20, kRefTextPresYellowKeyword_169_F020, "YellowKeyword_169_F020");
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k16x9, 30, kRefTextPresYellowKeyword_169_F030, "YellowKeyword_169_F030");
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k16x9, 40, kRefTextPresYellowKeyword_169_F040, "YellowKeyword_169_F040");
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k9x16, 0,  kRefTextPresYellowKeyword_916_F000, "YellowKeyword_916_F000");
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k9x16, 20, kRefTextPresYellowKeyword_916_F020, "YellowKeyword_916_F020");
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k9x16, 30, kRefTextPresYellowKeyword_916_F030, "YellowKeyword_916_F030");
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k9x16, 40, kRefTextPresYellowKeyword_916_F040, "YellowKeyword_916_F040");
}
