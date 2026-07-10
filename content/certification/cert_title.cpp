// ==============================================================================
// content/certification/cert_title.cpp
//
// FASE 3.1-3.2 — Composizioni di certificazione testo
//
// CertTitle:          1920×1080 (16:9), "EPIC TITLE" Inter Bold 120pt centrato
// CertTitleVertical:  1080×1920 (9:16), stesso testo per TikTok/Shorts
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

// ── Helper: crea una composizione con testo centrato su sfondo ──────────
static Composition make_cert_title_comp(const char* name,
                                         int width, int height,
                                         const char* text,
                                         float font_size) {
    return composition(
        {.name = name,
         .width = width,
         .height = height,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [text, font_size, width, height](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            // Dark grid background
            backgrounds::add_common_background(
                s, backgrounds::BackgroundStyles::Minimalist());
            // Centered text layer — pin_to(Center) already translates the
            // layer origin to canvas centre, so pos must be {0,0,0} to
            // avoid a double-offset (previously {w/2, h/2} pushed text
            // to the bottom-right corner).
            s.layer("title", [&](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.text("title_text", text::centered_text({
                    .text      = text,
                    .box        = {static_cast<float>(width),
                                   static_cast<float>(height)},
                    .pos        = {0.0f, 0.0f, 0.0f},
                     .font_asset = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 700,
                    .font_size  = font_size,
                }));
            });
            return s.build();
        });
}

// ── CertTitle — 1920×1080, 16:9 ───────────────────────────────────────
Composition cert_title() {
    return make_cert_title_comp("CertTitle", 1920, 1080,
                                "EPIC TITLE", 120.0f);
}

// ── CertTitleVertical — 1080×1920, 9:16 ───────────────────────────────
Composition cert_title_vertical() {
    return make_cert_title_comp("CertTitleVertical", 1080, 1920,
                                "EPIC TITLE", 96.0f);
}

// ── Registration ──────────────────────────────────────────────────────
void register_cert_title_compositions(CompositionRegistry& registry) {
    registry.add("CertTitle", [](const CompositionProps&) {
        return cert_title();
    });
    registry.add("CertTitleVertical", [](const CompositionProps&) {
        return cert_title_vertical();
    });
}

} // namespace chronon3d::content::certification
