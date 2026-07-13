// ==============================================================================
// content/certification/cert_render_runtime.cpp
//
// TICKET-RENDER-RUNTIME-CERT — Render runtime & framebuffer certification
// compositions (P0).
//
// 4 minimal-surface compositions for the canonical verify_render_runtime_linux.sh
// gate: CertRectangle / CertImage / CertText / CertComposite.
// Each is a deterministic single-frame composition that exercises one
// framebuffer isolation invariant:
//   - CertRectangle: pixel presence/absence + alpha correctness + bbox
//   - CertImage:     pixel presence (from asset) + alpha correctness + bbox
//   - CertText:      pixel presence (from glyph ink) + alpha + bbox + no black frame
//   - CertComposite: 4 layers → layer order + no exploded geometry
//
// 1920×1080 canvas. Determinism guaranteed by fixed frame=0 + FrameRate{30,1}
// + duration=1 (single-frame evaluation).
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

// ── Shared constants ──────────────────────────────────────────────────────
static constexpr int   kWidth       = 1920;
static constexpr int   kHeight      = 1080;
static constexpr float kCenterX     = static_cast<float>(kWidth) * 0.5f;
static constexpr float kCenterY     = static_cast<float>(kHeight) * 0.5f;
static constexpr float kShapeW      = 400.0f;
static constexpr float kShapeH      = 300.0f;
static constexpr float kShapeX      = kCenterX - kShapeW * 0.5f;  // 760
static constexpr float kShapeY      = kCenterY - kShapeH * 0.5f;  // 390
// Test image asset path (canonical; the gate generates this PNG on-the-fly
// before invoking `chronon3d_cli still CertImage`).
static constexpr const char* kCertImageAsset = "content/certification/assets/cert_image_test.png";

// ── CertRectangle: single red rectangle centered on canvas ────────────────
Composition cert_rectangle() {
    return composition(
        {.name = "CertRectangle",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            // Solid dark background so the red rect is visible
            s.layer("bg", [](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.rect("bg", RectParams{
                    .size = {static_cast<float>(kWidth), static_cast<float>(kHeight)},
                    .color = {0.05f, 0.05f, 0.08f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.05f, 0.05f, 0.08f, 1.0f}),
                    .stroke = {},
                });
            });
            // The cert rectangle: solid red, 400×300, centered
            s.layer("rect", [](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.rect("cert_rect", RectParams{
                    .size = {kShapeW, kShapeH},
                    .color = {1.0f, 0.0f, 0.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{1.0f, 0.0f, 0.0f, 1.0f}),
                    .stroke = {},
                });
            });
            return s.build();
        });
}

// ── CertImage: single image layer (test PNG) centered on canvas ────────────
Composition cert_image() {
    return composition(
        {.name = "CertImage",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.rect("bg", RectParams{
                    .size = {static_cast<float>(kWidth), static_cast<float>(kHeight)},
                    .color = {0.05f, 0.05f, 0.08f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.05f, 0.05f, 0.08f, 1.0f}),
                    .stroke = {},
                });
            });
            s.layer("img", [](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.image("cert_img", ImageParams{
                    .asset_path = kCertImageAsset,
                    .size = {kShapeW, kShapeH},
                    .opacity = 1.0f,
                });
            });
            return s.build();
        });
}

// ── CertText: single text layer centered on canvas ────────────────────────
Composition cert_text() {
    return composition(
        {.name = "CertText",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            backgrounds::add_common_background(
                s, backgrounds::BackgroundStyles::Minimalist());
            s.layer("text_layer", [](LayerBuilder& l) {
                l.text("cert_text", TextDefinition{
    .content = {.value = "RUNTIME CERT"},
    .style = {
        .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 96.0f},
        .color = Color::white()
    },
    .frame = {
        .placement = TextPlacement{TextPlacementKind::Absolute, {kCenterX, kCenterY}},
        .size = {kShapeW, kShapeH},
        .anchor = TextAnchor::Center,
        .centering_mode = TextCenteringMode::PixelInk,
        .align = TextAlign::Center,
        .vertical_align = VerticalAlign::Middle,
        .wrap = TextWrap::Word,
        .overflow = TextOverflow::Clip,
        .line_height = 0.95f,
        .max_lines = 1
    }
});
            });
            return s.build();
        });
}

// ── CertComposite: 4 layers (bg → image → rect → text) for layer order ────
Composition cert_composite() {
    return composition(
        {.name = "CertComposite",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            // Layer 1 (bottom): background
            s.layer("bg", [](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.rect("bg", RectParams{
                    .size = {static_cast<float>(kWidth), static_cast<float>(kHeight)},
                    .color = {0.05f, 0.05f, 0.08f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.05f, 0.05f, 0.08f, 1.0f}),
                    .stroke = {},
                });
            });
            // Layer 2: image (offset top-left)
            s.layer("img", [](LayerBuilder& l) {
                l.pin_to(Anchor::TopLeft, 50.0f);
                l.image("cert_img", ImageParams{
                    .asset_path = kCertImageAsset,
                    .size = {200.0f, 150.0f},
                    .opacity = 1.0f,
                });
            });
            // Layer 3: rectangle (offset bottom-right)
            s.layer("rect", [](LayerBuilder& l) {
                l.pin_to(Anchor::BottomRight, 50.0f);
                l.rect("cert_rect", RectParams{
                    .size = {200.0f, 150.0f},
                    .color = {0.0f, 1.0f, 0.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.0f, 1.0f, 0.0f, 1.0f}),
                    .stroke = {},
                });
            });
            // Layer 4 (top): text overlay
            s.layer("text_overlay", [](LayerBuilder& l) {
                l.text("cert_text", TextDefinition{
    .content = {.value = "COMPOSITE"},
    .style = {
        .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 72.0f},
        .color = Color{1.0f, 1.0f, 0.0f, 1.0f}
    },
    .frame = {
        .placement = TextPlacement{TextPlacementKind::Absolute, {kCenterX, kCenterY}},
        .size = {kShapeW, kShapeH},
        .anchor = TextAnchor::Center,
        .centering_mode = TextCenteringMode::PixelInk,
        .align = TextAlign::Center,
        .vertical_align = VerticalAlign::Middle,
        .wrap = TextWrap::Word,
        .overflow = TextOverflow::Clip,
        .line_height = 0.95f,
        .max_lines = 1
    }
});
            });
            return s.build();
        });
}

// ── Registration ──────────────────────────────────────────────────────────
void register_cert_render_runtime_compositions(CompositionRegistry& registry) {
    registry.add("CertRectangle", [](const CompositionProps&) {
        return cert_rectangle();
    });
    registry.add("CertImage", [](const CompositionProps&) {
        return cert_image();
    });
    registry.add("CertText", [](const CompositionProps&) {
        return cert_text();
    });
    registry.add("CertComposite", [](const CompositionProps&) {
        return cert_composite();
    });
}

} // namespace chronon3d::content::certification
