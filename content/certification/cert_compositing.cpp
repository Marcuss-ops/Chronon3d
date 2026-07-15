// ==============================================================================
// content/certification/cert_compositing.cpp
//
// TICKET-COMPOSITING-CERT — Compositing & Effects certification.
// ==============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/core/types/frame_context.hpp>

#include "content/certification/certification_descriptor.hpp"

namespace chronon3d::content::certification {

using namespace chronon3d;

namespace {

constexpr int kCompW = 1920;
constexpr int kCompH = 1080;
constexpr float kCompCX = static_cast<float>(kCompW) * 0.5f;
constexpr float kCompCY = static_cast<float>(kCompH) * 0.5f;
constexpr float kCompRectW = 400.0f;
constexpr float kCompRectH = 300.0f;

void add_dark_bg(SceneBuilder& scene) {
    scene.layer("bg", [](LayerBuilder& layer) {
        layer.fullscreen_rect("bg", Color{0.02f, 0.02f, 0.05f, 1.0f});
    });
}

void add_rect(LayerBuilder& layer,
              Color color = Color{1.0f, 1.0f, 1.0f, 1.0f}) {
    layer.rect("r", RectParams{
        .size = {kCompRectW, kCompRectH},
        .color = color,
        .pos = {0.0f, 0.0f, 0.0f},
        .fill = FillStyle::solid(color),
        .stroke = {},
    });
}

void center_rect(LayerBuilder& layer) {
    layer.position({kCompCX - kCompRectW * 0.5f,
                    kCompCY - kCompRectH * 0.5f,
                    0.0f});
}

template <typename Build>
Composition cert_scene(const char* name, Build&& build) {
    return composition(
        {.name = name,
         .width = kCompW,
         .height = kCompH,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [build = std::forward<Build>(build)](const FrameContext& ctx) -> Scene {
            SceneBuilder scene(ctx);
            build(scene);
            return scene.build();
        });
}

} // namespace

Composition cert_opacity() {
    return cert_scene("CertOpacity", [](SceneBuilder& scene) {
        add_dark_bg(scene);
        scene.layer("full", [](LayerBuilder& layer) {
            layer.position({200.0f, kCompCY - kCompRectH * 0.5f, 0.0f});
            add_rect(layer);
        });
        scene.layer("dim", [](LayerBuilder& layer) {
            layer.position({kCompW - 200.0f - kCompRectW,
                            kCompCY - kCompRectH * 0.5f,
                            0.0f});
            layer.opacity(0.3f);
            add_rect(layer);
        });
    });
}

Composition cert_blur() {
    return cert_scene("CertBlur", [](SceneBuilder& scene) {
        add_dark_bg(scene);
        scene.layer("sharp", [](LayerBuilder& layer) {
            layer.position({200.0f, kCompCY - kCompRectH * 0.5f, 0.0f});
            add_rect(layer);
        });
        scene.layer("blurred", [](LayerBuilder& layer) {
            layer.position({kCompW - 200.0f - kCompRectW,
                            kCompCY - kCompRectH * 0.5f,
                            0.0f});
            layer.blur(12.0f);
            add_rect(layer);
        });
    });
}

Composition cert_glow() {
    return cert_scene("CertGlow", [](SceneBuilder& scene) {
        add_dark_bg(scene);
        scene.layer("glow_rect", [](LayerBuilder& layer) {
            center_rect(layer);
            layer.glow(GlowParams{
                .radius = 24.0f,
                .intensity = 1.0f,
                .color = Color{0.3f, 0.6f, 1.0f, 1.0f},
                .preserve_source = true,
            });
            add_rect(layer);
        });
    });
}

Composition cert_glow_disabled() {
    return cert_scene("CertGlowDisabled", [](SceneBuilder& scene) {
        add_dark_bg(scene);
        scene.layer("glow_zero", [](LayerBuilder& layer) {
            center_rect(layer);
            layer.glow(GlowParams{
                .radius = 24.0f,
                .intensity = 0.0f,
                .color = Color{0.3f, 0.6f, 1.0f, 1.0f},
                .preserve_source = true,
            });
            add_rect(layer);
        });
    });
}

Composition cert_shadow() {
    return cert_scene("CertShadow", [](SceneBuilder& scene) {
        add_dark_bg(scene);
        scene.layer("shadow_rect", [](LayerBuilder& layer) {
            center_rect(layer);
            layer.drop_shadow({8.0f, 8.0f},
                              Color{0.0f, 0.0f, 0.0f, 0.6f}, 16.0f);
            add_rect(layer);
        });
    });
}

Composition cert_stroke() {
    return cert_scene("CertStroke", [](SceneBuilder& scene) {
        add_dark_bg(scene);
        scene.layer("no_stroke", [](LayerBuilder& layer) {
            layer.position({200.0f, kCompCY - kCompRectH * 0.5f, 0.0f});
            add_rect(layer);
        });
        scene.layer("stroked", [](LayerBuilder& layer) {
            layer.position({kCompW - 200.0f - kCompRectW,
                            kCompCY - kCompRectH * 0.5f,
                            0.0f});
            layer.rect("r", RectParams{
                .size = {kCompRectW, kCompRectH},
                .color = {1.0f, 1.0f, 1.0f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = FillStyle::solid(Color{1.0f, 1.0f, 1.0f, 1.0f}),
                .stroke = graphics::StrokeStyle{
                    .color = {1.0f, 0.0f, 0.0f, 1.0f},
                    .width = 6.0f,
                },
            });
        });
    });
}

Composition cert_mask() {
    return cert_scene("CertMask", [](SceneBuilder& scene) {
        add_dark_bg(scene);
        scene.layer("masked", [](LayerBuilder& layer) {
            center_rect(layer);
            layer.mask_circle(CircleMaskParams{
                .radius = 120.0f,
                .pos = {kCompRectW * 0.5f, kCompRectH * 0.5f, 0.0f},
                .inverted = false,
            });
            add_rect(layer);
        });
    });
}

Composition cert_blend_add() {
    return cert_scene("CertBlendAdd", [](SceneBuilder& scene) {
        scene.layer("bg", [](LayerBuilder& layer) {
            layer.fullscreen_rect("bg", Color{0.3f, 0.3f, 0.3f, 1.0f});
        });
        scene.layer("additive", [](LayerBuilder& layer) {
            center_rect(layer);
            layer.blend(BlendMode::Add);
            add_rect(layer, Color{0.0f, 0.2f, 0.5f, 1.0f});
        });
    });
}

Composition cert_blend_multiply() {
    return cert_scene("CertBlendMultiply", [](SceneBuilder& scene) {
        scene.layer("bg", [](LayerBuilder& layer) {
            layer.fullscreen_rect("bg", Color{0.7f, 0.7f, 0.7f, 1.0f});
        });
        scene.layer("mult", [](LayerBuilder& layer) {
            center_rect(layer);
            layer.blend(BlendMode::Multiply);
            add_rect(layer, Color{0.3f, 0.5f, 0.3f, 1.0f});
        });
    });
}

Composition cert_precomp() {
    return cert_scene("CertPrecomp", [](SceneBuilder& scene) {
        add_dark_bg(scene);
        scene.precomp_layer("nested", "CertOpacity", [](LayerBuilder& layer) {
            layer.position({0.0f, 0.0f, 0.0f});
        });
    });
}

Composition cert_plain() {
    return cert_scene("CertPlain", [](SceneBuilder& scene) {
        add_dark_bg(scene);
        scene.layer("plain_rect", [](LayerBuilder& layer) {
            center_rect(layer);
            add_rect(layer);
        });
    });
}

Composition cert_clip() {
    return cert_scene("CertClip", [](SceneBuilder& scene) {
        add_dark_bg(scene);
        scene.layer("clipped", [](LayerBuilder& layer) {
            center_rect(layer);
            layer.mask_rect(RectMaskParams{
                .size = {kCompRectW * 0.5f, kCompRectH * 0.5f},
                .pos = {kCompRectW * 0.25f, kCompRectH * 0.25f, 0.0f},
                .inverted = false,
            });
            add_rect(layer);
        });
    });
}

Composition cert_blend_normal() {
    return cert_scene("CertBlendNormal", [](SceneBuilder& scene) {
        add_dark_bg(scene);
        scene.layer("normal", [](LayerBuilder& layer) {
            center_rect(layer);
            add_rect(layer, Color{1.0f, 0.0f, 0.0f, 1.0f});
        });
    });
}

Composition cert_blend_screen() {
    return cert_scene("CertBlendScreen", [](SceneBuilder& scene) {
        scene.layer("bg", [](LayerBuilder& layer) {
            layer.fullscreen_rect("bg", Color{0.3f, 0.3f, 0.3f, 1.0f});
        });
        scene.layer("screen", [](LayerBuilder& layer) {
            center_rect(layer);
            layer.blend(BlendMode::Screen);
            add_rect(layer);
        });
    });
}

Composition cert_nested() {
    return cert_scene("CertNested", [](SceneBuilder& scene) {
        add_dark_bg(scene);
        scene.layer("nested_rect", [](LayerBuilder& layer) {
            center_rect(layer);
            layer.glow(GlowParams{
                .radius = 16.0f,
                .intensity = 0.8f,
                .color = Color{0.3f, 0.6f, 1.0f, 1.0f},
                .preserve_source = true,
            });
            layer.blur(4.0f);
            add_rect(layer);
        });
    });
}

void register_cert_compositing_compositions(CompositionRegistry& registry) {
    const auto add = [&](const char* id, auto factory) {
        registry.add(certification_descriptor(
            id, kCompW, kCompH, Frame{1}, std::move(factory)));
    };

    add("CertOpacity", [](const CompositionProps&) { return cert_opacity(); });
    add("CertBlur", [](const CompositionProps&) { return cert_blur(); });
    add("CertGlow", [](const CompositionProps&) { return cert_glow(); });
    add("CertGlowDisabled", [](const CompositionProps&) { return cert_glow_disabled(); });
    add("CertShadow", [](const CompositionProps&) { return cert_shadow(); });
    add("CertStroke", [](const CompositionProps&) { return cert_stroke(); });
    add("CertMask", [](const CompositionProps&) { return cert_mask(); });
    add("CertBlendAdd", [](const CompositionProps&) { return cert_blend_add(); });
    add("CertBlendMultiply", [](const CompositionProps&) { return cert_blend_multiply(); });
    add("CertPrecomp", [](const CompositionProps&) { return cert_precomp(); });
    add("CertPlain", [](const CompositionProps&) { return cert_plain(); });
    add("CertClip", [](const CompositionProps&) { return cert_clip(); });
    add("CertBlendNormal", [](const CompositionProps&) { return cert_blend_normal(); });
    add("CertBlendScreen", [](const CompositionProps&) { return cert_blend_screen(); });
    add("CertNested", [](const CompositionProps&) { return cert_nested(); });
}

} // namespace chronon3d::content::certification
