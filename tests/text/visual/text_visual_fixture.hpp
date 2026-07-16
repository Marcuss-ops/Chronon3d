// ═══════════════════════════════════════════════════════════════════════════
// tests/text/visual/text_visual_fixture.hpp — Phase-2.1 P0 split
//
// Phase-2.1 mechanical split of tests/text/test_text_preset_visual.cpp  // drift-allow: stale-ref
// (898 LOC, 128 sentinels).  Fixture header owns ONLY the composition-
// building + frame-render + gate-emission orchestration; the metric POD,
// hash/ink computation, and VisualExpectation enum live in `text_visual_
// metrics.cpp` and `text_visual_expectations.hpp` (sibling files).
//
// Per user spec:
//   * text_visual_fixture.hpp          — creazione composizione,
//                                        render frame, calcolo metriche,
//                                        confronto aspettative.
//   * text_visual_metrics.cpp          — RectF / ScenarioMetrics PODs +
//                                        `inline` compute_metrics(…).
//                                        (shared via #include from each
//                                        test TU).
//   * text_visual_expectations.hpp     — VisualExpectation enum,
//                                        expectation_name(),
//                                        kVisibleMinPixels, the
//                                        VR_TEXT_PRESET_GATE macro.
//   * text_visual_sentinels.hpp        — 128 sentinel constants (data
//                                        only — no test logic).
//
// Companion test TUs (4 split files + 1 placeholder):
//   tests/text/test_text_preset_reveal.cpp       (10 Reveal presets)
//   tests/text/test_text_preset_emphasis.cpp     (4  Emphasis presets)
//   tests/text/test_text_preset_subtitle.cpp     (2 Subtitle presets +
//                                                  2 e2e pipelines)
//   tests/text/test_text_preset_cinematic.cpp    (placeholder for
//                                                  the cinematic tier
//                                                  (caption_box,
//                                                  glow_pulse,
//                                                  animation_compositions,
//                                                  cinematic_text_camera,
//                                                  cinematic_title_reveal,
//                                                  tilt_sweep_title_v2),
//                                                  all deferred to A4.1
//                                                  once bit-stable).
// ═══════════════════════════════════════════════════════════════════════════
#pragma once

#include <string>

#include <doctest/doctest.h>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_error.hpp>
#include <chronon3d/sdk/render_request.hpp>
#include <chronon3d/sdk/render_settings.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/registry/text_preset_registry.hpp>
#include <content/text/text_helpers.hpp>
#include <chronon3d/text/text_definition.hpp>  // F2.C — from_text_definition()

#include <chrono>
#include <utility>

using namespace chronon3d;

// ── Composition + render pipeline ─────────────────────────────────────────
// Aspect ratio bin + dim helpers (verbatim from original file).
enum class AspectRatio : int { k16x9 = 0, k9x16 = 1 };
struct AspectSpec { int width; int height; };

inline AspectSpec aspect_dims(AspectRatio r) {
    return r == AspectRatio::k16x9 ? AspectSpec{1920, 1080}
                                    : AspectSpec{1080, 1920};
}

// CenterTextOptions builder for the preset-text baseline.  Source-of-truth
// for the `make_preset_base_opts` helper — see PR-A4 review-fix that
// lifted the font/path choices to a single constant.
inline chronon3d::content::text::CenterTextOptions
make_preset_base_opts(const std::string& text,
                      AspectSpec d) {
    constexpr const char* kVFont = "assets/fonts/Poppins-Bold.ttf";
    const f32 font_size = (d.width >= d.height) ? 96.0f : 64.0f;
    return chronon3d::content::text::CenterTextOptions{
        .text        = text,
        .box         = Vec2{d.width * 0.85f, d.height * 0.85f},
        .font_asset   = kVFont,
        .font_family = "Poppins",
        .font_weight = 700,
        .font_size   = font_size,
        .color       = Color::white(),
    };
}

// Single shared TextPresetRegistry instance — frozen at static-init time so
// the 16 TEST_CASEs don't each rebuild + re-register the 22 builtin entries.
// Lifted-to-file-scope was a PR-A4 code-reviewer fix (would otherwise
// rebuild 128 times — once per emit_preset_gate call).
inline const chronon3d::registry::TextPresetRegistry& shared_text_preset_registry() {
    static const chronon3d::registry::TextPresetRegistry kTextPresetRegistry = []() {
        auto reg = chronon3d::registry::make_default_text_preset_registry();
        reg.freeze();
        return reg;
    }();
    return kTextPresetRegistry;
}

// Build a Composition that applies the named preset at a specific
// (ratio, frame) point.  fps fixed at 30 to keep the F0/F20/F30/F40
// sentinel axis deterministic across machines; coverage of 24/60 fps
// is deferred to Cleanup #7.
inline Composition build_preset_composition(const std::string& preset_id,
                                             AspectRatio r,
                                             int t_frame,
                                             chronon3d::FontEngine* font_engine = nullptr,
                                             int fps = 30) {
    AspectSpec d = aspect_dims(r);
    const auto& preset = shared_text_preset_registry().get(preset_id);  // throws if id absent

    return composition(
        {.name = std::string("VR/Text/") + preset_id + "_" +
                  (r == AspectRatio::k16x9 ? "169" : "916"),
         .width = d.width, .height = d.height,
         .frame_rate = chronon3d::FrameRate{fps, 1},
         .duration = 60},
        [preset, preset_id, t_frame, r, font_engine](const chronon3d::FrameContext& ctx) -> chronon3d::Scene {
            chronon3d::SceneBuilder s(ctx);
            if (font_engine) s.font_engine(font_engine);
            auto base = chronon3d::content::text::centered_text(
                make_preset_base_opts("THE QUICK BROWN FOX JUMPS",
                                       aspect_dims(r)));
            s.layer("hero", [&s, &preset, base](chronon3d::LayerBuilder& l) {
                // The preset builder (wire_through_resolver) already creates the
                // canonical text-run entry.  A second l.text(…) call would produce
                // a duplicate RenderNode at the same position, routing through
                // MultiSourceNode instead of TextRunNode — the duplicate's animators
                // (fade_in / scale_drop) can blank the static text at early frames.
                if (preset.builder) {
                    preset.builder(s, l, from_text_definition(base));
                }
            });
            return s.build();
        });
}

// Render-one-(preset, ratio, frame)-point + route through sentinel gate.
// The metric POD and the gate macro are #included below AFTER the prototype
// is visible at file-scope so the test TU's TU-local `inline metric::`
// definitions resolve before emit_preset_gate()'s body uses them.
#include "tests/text/visual/text_visual_metrics.cpp"     // RectF / ScenarioMetrics / compute_metrics
#include "tests/text/visual/text_visual_expectations.hpp" // VisualExpectation enum / gate macro

inline void emit_preset_gate(chronon3d::SoftwareRenderer& renderer,
                              const std::string& preset_id,
                              AspectRatio r,
                              int t_frame,
                              std::uint64_t kref,
                              const std::string& short_label,
                              VisualExpectation expectation) {
    auto comp = build_preset_composition(preset_id, r, t_frame, &renderer.font_engine(), 30);
    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(comp, chronon3d::Frame{t_frame});
    REQUIRE(fb != nullptr);
    auto m = compute_metrics(*fb, t0);
    VR_TEXT_PRESET_GATE(short_label, kref, m, expectation);
}
