// ==============================================================================
// content/certification/cert_compositing.cpp
//
// TICKET-COMPOSITING-CERT — Compositing & Effects certification
// compositions (P1).
//
// 10 compositions for the canonical verify_compositing_effects_linux.sh gate.
// Each exercises one compositing/effect invariant:
//
//   CertOpacity         — two rects: one at opacity=0.3, one at 1.0
//   CertBlur            — two rects: one blurred, one sharp
//   CertGlow            — text with glow (preserve_source=true)
//   CertGlowDisabled    — glow with intensity=0 (must equal no-glow)
//   CertShadow          — rect with drop_shadow
//   CertStroke          — rect with stroke outline
//   CertMask            — masked rect (circle mask)
//   CertBlendAdd        — rect with BlendMode::Add over background
//   CertBlendMultiply   — rect with BlendMode::Multiply over background
//   CertPrecomp         — precomp_layer referencing another composition
//
// 1920×1080 canvas. Determinism guaranteed by fixed frame=0 + FrameRate{30,1}.
// ==============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/core/types/frame_context.hpp>

namespace chronon3d::content::certification {

using namespace chronon3d;

static constexpr int   kW     = 1920;
static constexpr int   kH     = 1080;
static constexpr float kCX    = static_cast<float>(kW) * 0.5f;
static constexpr float kCY    = static_cast<float>(kH) * 0.5f;
static constexpr float kRectW = 400.0f;
static constexpr float kRectH = 300.0f;

// ── Shared helpers ────────────────────────────────────────────────────────

static void add_dark_bg(SceneBuilder& s) {
    s.layer("bg", [](LayerBuilder& l) {
        l.fullscreen_rect("bg", Color{0.02f, 0.02f, 0.05f, 1.0f});
    });
}

static void add_white_rect(SceneBuilder& s, const char* id, f32 x, f32 y) {
    s.layer(id, [=](LayerBuilder& l) {
        l.position({x, y, 0.0f});
        l.rect("r", RectParams{
            .size = {kRectW, kRectH},
            .color = {1.0f, 1.0f, 1.0f, 1.0f},
            .pos = {0.0f, 0.0f, 0.0f},
            .fill = FillStyle::solid(Color{1.0f, 1.0f, 1.0f, 1.0f}),
            .stroke = {},
        });
    });
}

// ── CertOpacity ───────────────────────────────────────────────────────────

Composition cert_opacity() {
    return composition(
        {.name = "CertOpacity", .width = kW, .height = kH,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            add_dark_bg(s);
            // Left: full opacity white rect
            add_white_rect(s, "full", 200.0f, kCY - kRectH * 0.5f);
            // Right: 30% opacity white rect
            s.layer("dim", [](LayerBuilder& l) {
                l.position({kW - 200.0f - kRectW, kCY - kRectH * 0.5f, 0.0f});
                l.opacity(0.3f);
                l.rect("r", RectParams{
                    .size = {kRectW, kRectH},
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{1.0f, 1.0f, 1.0f, 1.0f}),
                    .stroke = {},
                });
            });
            return s.build();
        });
}

// ── CertBlur ──────────────────────────────────────────────────────────────

Composition cert_blur() {
    return composition(
        {.name = "CertBlur", .width = kW, .height = kH,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            add_dark_bg(s);
            // Left: sharp rect
            add_white_rect(s, "sharp", 200.0f, kCY - kRectH * 0.5f);
            // Right: blurred rect
            s.layer("blurred", [](LayerBuilder& l) {
                l.position({kW - 200.0f - kRectW, kCY - kRectH * 0.5f, 0.0f});
                l.blur(12.0f);
                l.rect("r", RectParams{
                    .size = {kRectW, kRectH},
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{1.0f, 1.0f, 1.0f, 1.0f}),
                    .stroke = {},
                });
            });
            return s.build();
        });
}

// ── CertGlow ──────────────────────────────────────────────────────────────

Composition cert_glow() {
    return composition(
        {.name = "CertGlow", .width = kW, .height = kH,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            add_dark_bg(s);
            s.layer("glow_rect", [](LayerBuilder& l) {
                l.position({kCX - kRectW * 0.5f, kCY - kRectH * 0.5f, 0.0f});
                l.glow(GlowParams{
                    .color = Color{0.3f, 0.6f, 1.0f, 1.0f},
                    .radius = 24.0f,
                    .intensity = 1.0f,
                    .preserve_source = true,
                });
                l.rect("r", RectParams{
                    .size = {kRectW, kRectH},
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{1.0f, 1.0f, 1.0f, 1.0f}),
                    .stroke = {},
                });
            });
            return s.build();
        });
}

// ── CertGlowDisabled ──────────────────────────────────────────────────────

Composition cert_glow_disabled() {
    return composition(
        {.name = "CertGlowDisabled", .width = kW, .height = kH,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            add_dark_bg(s);
            // Glow with intensity=0 — must be a real no-op
            s.layer("glow_zero", [](LayerBuilder& l) {
                l.position({kCX - kRectW * 0.5f, kCY - kRectH * 0.5f, 0.0f});
                l.glow(GlowParams{
                    .color = Color{0.3f, 0.6f, 1.0f, 1.0f},
                    .radius = 24.0f,
                    .intensity = 0.0f,
                    .preserve_source = true,
                });
                l.rect("r", RectParams{
                    .size = {kRectW, kRectH},
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{1.0f, 1.0f, 1.0f, 1.0f}),
                    .stroke = {},
                });
            });
            return s.build();
        });
}

// ── CertShadow ────────────────────────────────────────────────────────────

Composition cert_shadow() {
    return composition(
        {.name = "CertShadow", .width = kW, .height = kH,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            add_dark_bg(s);
            s.layer("shadow_rect", [](LayerBuilder& l) {
                l.position({kCX - kRectW * 0.5f, kCY - kRectH * 0.5f, 0.0f});
                l.drop_shadow({8.0f, 8.0f}, Color{0.0f, 0.0f, 0.0f, 0.6f}, 16.0f);
                l.rect("r", RectParams{
                    .size = {kRectW, kRectH},
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{1.0f, 1.0f, 1.0f, 1.0f}),
                    .stroke = {},
                });
            });
            return s.build();
        });
}

// ── CertStroke ────────────────────────────────────────────────────────────

Composition cert_stroke() {
    return composition(
        {.name = "CertStroke", .width = kW, .height = kH,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            add_dark_bg(s);
            // Left: no stroke
            add_white_rect(s, "no_stroke", 200.0f, kCY - kRectH * 0.5f);
            // Right: red stroke
            s.layer("stroked", [](LayerBuilder& l) {
                l.position({kW - 200.0f - kRectW, kCY - kRectH * 0.5f, 0.0f});
                l.rect("r", RectParams{
                    .size = {kRectW, kRectH},
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{1.0f, 1.0f, 1.0f, 1.0f}),
                    .stroke = graphics::StrokeStyle{
                        .color = {1.0f, 0.0f, 0.0f, 1.0f},
                        .width = 6.0f,
                    },
                });
            });
            return s.build();
        });
}

// ── CertMask ──────────────────────────────────────────────────────────────

Composition cert_mask() {
    return composition(
        {.name = "CertMask", .width = kW, .height = kH,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            add_dark_bg(s);
            s.layer("masked", [](LayerBuilder& l) {
                l.position({kCX - kRectW * 0.5f, kCY - kRectH * 0.5f, 0.0f});
                l.mask_circle(CircleMaskParams{
                    .radius = 120.0f,
                    .center = {kRectW * 0.5f, kRectH * 0.5f},
                    .inverted = false,
                });
                l.rect("r", RectParams{
                    .size = {kRectW, kRectH},
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{1.0f, 1.0f, 1.0f, 1.0f}),
                    .stroke = {},
                });
            });
            return s.build();
        });
}

// ── CertBlendAdd ──────────────────────────────────────────────────────────

Composition cert_blend_add() {
    return composition(
        {.name = "CertBlendAdd", .width = kW, .height = kH,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            // Mid-gray background
            s.layer("bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.3f, 0.3f, 0.3f, 1.0f});
            });
            // Blue additive rect — should brighten the background
            s.layer("additive", [](LayerBuilder& l) {
                l.position({kCX - kRectW * 0.5f, kCY - kRectH * 0.5f, 0.0f});
                l.blend(BlendMode::Add);
                l.rect("r", RectParams{
                    .size = {kRectW, kRectH},
                    .color = {0.0f, 0.2f, 0.5f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.0f, 0.2f, 0.5f, 1.0f}),
                    .stroke = {},
                });
            });
            return s.build();
        });
}

// ── CertBlendMultiply ─────────────────────────────────────────────────────

Composition cert_blend_multiply() {
    return composition(
        {.name = "CertBlendMultiply", .width = kW, .height = kH,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            // Light gray background
            s.layer("bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.7f, 0.7f, 0.7f, 1.0f});
            });
            // Dark multiply rect — should darken the background
            s.layer("mult", [](LayerBuilder& l) {
                l.position({kCX - kRectW * 0.5f, kCY - kRectH * 0.5f, 0.0f});
                l.blend(BlendMode::Multiply);
                l.rect("r", RectParams{
                    .size = {kRectW, kRectH},
                    .color = {0.3f, 0.5f, 0.3f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.3f, 0.5f, 0.3f, 1.0f}),
                    .stroke = {},
                });
            });
            return s.build();
        });
}

// ── CertPrecomp ───────────────────────────────────────────────────────────

Composition cert_precomp() {
    return composition(
        {.name = "CertPrecomp", .width = kW, .height = kH,
         .frame_rate = FrameRate{30, 1}, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            add_dark_bg(s);
            s.precomp_layer("nested", "CertOpacity", [](LayerBuilder& l) {
                l.position({0.0f, 0.0f, 0.0f});
            });
            return s.build();
        });
}

// ── Registration ──────────────────────────────────────────────────────────

void register_cert_compositing_compositions(CompositionRegistry& registry) {
    registry.add("CertOpacity", [](const CompositionProps&) {
        return cert_opacity();
    });
    registry.add("CertBlur", [](const CompositionProps&) {
        return cert_blur();
    });
    registry.add("CertGlow", [](const CompositionProps&) {
        return cert_glow();
    });
    registry.add("CertGlowDisabled", [](const CompositionProps&) {
        return cert_glow_disabled();
    });
    registry.add("CertShadow", [](const CompositionProps&) {
        return cert_shadow();
    });
    registry.add("CertStroke", [](const CompositionProps&) {
        return cert_stroke();
    });
    registry.add("CertMask", [](const CompositionProps&) {
        return cert_mask();
    });
    registry.add("CertBlendAdd", [](const CompositionProps&) {
        return cert_blend_add();
    });
    registry.add("CertBlendMultiply", [](const CompositionProps&) {
        return cert_blend_multiply();
    });
    registry.add("CertPrecomp", [](const CompositionProps&) {
        return cert_precomp();
    });
}

} // namespace chronon3d::content::certification
