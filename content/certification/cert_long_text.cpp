// ==============================================================================
// content/certification/cert_long_text.cpp
//
// FASE 3.4 — CertLongText composition
//
// Testo lungo con wrapping automatico, line spacing, no parole tagliate.
// 1920×1080 canvas, text box centrato con margini safe.
// ==============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>

#include "content/common/background_helpers.hpp"
#include "content/text/text_helpers.hpp"

namespace chronon3d::content::certification {

Composition cert_long_text() {
    constexpr int   kWidth  = 1920;
    constexpr int   kHeight = 1080;
    constexpr float kMargin = 120.0f;   // safe horizontal margins

    return composition(
        {.name = "CertLongText",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);

            // Background
            backgrounds::add_common_background(
                s, backgrounds::BackgroundStyles::Minimalist());

            // ── Long wrapped text layer ─────────────────────────────
            // Box: 80% of canvas width, centered, top-aligned with margin
            const float kBoxW = static_cast<float>(kWidth) - kMargin * 2.0f;
            const float kBoxH = static_cast<float>(kHeight) - kMargin * 2.0f;

            s.layer("long_text", [kBoxW, kBoxH](LayerBuilder& l) {
                l.text("body", text::centered_text({
                    .text = "This is a very long sentence that must wrap "
                            "correctly across multiple lines, demonstrating "
                            "proper word wrapping and line spacing in the "
                            "Chronon3D text engine for production-ready "
                            "subtitle and body text rendering. Every word "
                            "should remain intact without being cut or "
                            "hyphenated mid-character.",
                    .box        = {kBoxW, kBoxH},
                    .pos        = {static_cast<float>(kWidth) * 0.5f,
                                   kMargin + kBoxH * 0.5f,
                                   0.0f},
                     .font_asset = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 400,
                    .font_size  = 36.0f,
                    .tracking   = 0.5f,
                    .color      = Color{0.92f, 0.92f, 0.95f, 1.0f},
                    .max_lines  = 0,        // unlimited lines
                    .auto_fit   = false,     // fixed size, let it wrap
                    .line_height = 1.5f,
                }));
            });

            return s.build();
        });
}

// ── Registration ──────────────────────────────────────────────────────
void register_cert_long_text_compositions(CompositionRegistry& registry) {
    registry.add("CertLongText", [](const CompositionProps&) {
        return cert_long_text();
    });
}

} // namespace chronon3d::content::certification
