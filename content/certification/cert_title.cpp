// ==============================================================================
// content/certification/cert_title.cpp
//
// FASE 3.1-3.2 — Composizioni di certificazione testo
//
// CertTitle:          1920×1080 (16:9), "EPIC TITLE" Inter Bold 120pt centrato
// CertTitleVertical:  1080×1920 (9:16), stesso testo per TikTok/Shorts
//
// M1.8 §2D / TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS (2026-07-10):
//   - 1 `text::centered_text({...})` call site (in make_cert_title_comp
//     helper, shared by CertTitle + CertTitleVertical) migrated to
//     canonical `TextDefinition{}` API (F2.C adapter).
//   - `text_helpers.hpp` include removed (no longer used).
// ==============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/core/types/frame_context.hpp>

#include "content/common/background_helpers.hpp"

namespace chronon3d::content::certification {

using namespace chronon3d;

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
            // Centered text layer.
            // NOTE: pin_to(Center) does NOT translate text nodes —
            // text pos must be absolute canvas coordinates.
            // (TICKET: pin_to + text-node interaction needs engine fix.)
            s.layer("title", [&](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.text("title_text", TextDefinition{
    .content = {.value = text},
    .style = {
        .font = {.font_path   = "assets/fonts/Inter-Bold.ttf",
                                   .font_family = "Inter",
                                   .font_weight = 700,
                                   .font_size   = font_size},
        .color = Color::white()
    },
    .frame = {.size = {static_cast<float>(width),
                                                      static_cast<float>(height)},
.placement = TextPlacement{TextPlacementKind::Absolute, {static_cast<float>(width) * 0.5f,
                                   static_cast<float>(height) * 0.5f}},
.anchor = TextAnchor::Center,
.align = TextAlign::Center,
.vertical_align = VerticalAlign::Middle,
.wrap = TextWrap::Word,
.overflow = TextOverflow::Clip,
.centering_mode = TextCenteringMode::PixelInk,
.line_height = 0.95f,
.max_lines = 1
    }
});
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
