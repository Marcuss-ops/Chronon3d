// ═══════════════════════════════════════════════════════════════════════════
// tests/helpers/overlay_cert_fixture.hpp — OVERLAY-CERT-1 canonical fixture
//
// Single source of truth for the overlay certification scene.  Every lane of
// the overlay certification suite (software/CPU, Vulkan, FULL-vs-SPARSE
// differential correctness, video export) consumes THIS composition, so the
// fixture is authored once and rendered by every backend.
//
// ── OVERLAY-CERT-1 (canonical spec) ────────────────────────────────────────
//   canvas    : 1280 × 720
//   frame rate: 30 fps
//   duration  : 8 s = 240 frames
//
//   bg        : dark solid (deterministic; a video background is layered by
//               consumers that need one, e.g. the E2E export lane)
//   headline  : "CHRONON HEADLINE"   TopCenter     font 64  white
//               + drop shadow (offset 8,8, blur 12) + fade-in 10 frames
//   subtitle  : "Chronon overlay certification"  BottomCenter  font 42
//               + drop shadow (offset 8,8, blur 12) + fade-in 5 frames
//   watermark : assets/images/checker.png (logo stand-in, RGBA)
//               TopRight, 180 px wide, opacity 0.85, fade-in 8 frames
//
// The composition is backend-agnostic: text shaping needs a FontEngine, which
// is passed in by the caller (software renderers, Vulkan renderers and the
// video-export pipeline all own one).  Layer / node names are stable so later
// certification tests (placement, ownership, parity, residency) can rely on
// them.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/model/core/transition.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d::test {

// ── Canvas / time ─────────────────────────────────────────────────────────
inline constexpr int kOverlayCertWidth = 1280;
inline constexpr int kOverlayCertHeight = 720;
inline constexpr int kOverlayCertFps = 30;
inline constexpr Frame kOverlayCertFrames = Frame{240};   // 8 s @ 30 fps

// ── Background ────────────────────────────────────────────────────────────
inline constexpr Color kOverlayCertBackground{0.05f, 0.05f, 0.08f, 1.0f};

// ── Headline (TopCenter, font 64, shadow, fade-in 10 frames) ─────────────
inline constexpr char kOverlayCertHeadlineText[] = "CHRONON HEADLINE";
inline constexpr float kOverlayCertHeadlineFontSize = 64.0f;
inline constexpr Frame kOverlayCertHeadlineFadeFrames = Frame{10};

// ── Subtitle (BottomCenter, font 42, shadow, fade-in 5 frames) ───────────
inline constexpr char kOverlayCertSubtitleText[] = "Chronon overlay certification";
inline constexpr float kOverlayCertSubtitleFontSize = 42.0f;
inline constexpr Frame kOverlayCertSubtitleFadeFrames = Frame{5};

// ── Watermark (TopRight, 180 px, opacity 0.85, fade-in 8 frames) ─────────
// RGBA checker pattern — deterministic logo stand-in with alpha.
inline constexpr char kOverlayCertWatermarkAsset[] = "assets/images/checker.png";
inline constexpr float kOverlayCertWatermarkWidth = 180.0f;
inline constexpr float kOverlayCertWatermarkMargin = 20.0f;
inline constexpr float kOverlayCertWatermarkOpacity = 0.85f;
inline constexpr Frame kOverlayCertWatermarkFadeFrames = Frame{8};

// ── Shared shadow style (matches OVL-06: black shadow, offset 8,8, blur 12)
inline constexpr float kOverlayCertShadowOffset = 8.0f;
inline constexpr float kOverlayCertShadowRadius = 12.0f;

/// Crossfade-in spec for N frames (linear easing) — seconds = frames / fps.
inline LayerTransitionSpec overlay_cert_fade(Frame frames) {
    LayerTransitionSpec spec;
    spec.transition_id = "crossfade";
    spec.duration = frames.integral() / static_cast<double>(kOverlayCertFps);
    spec.delay = 0.0;
    spec.easing = Easing::Linear;
    return spec;
}

/// Canonical OVERLAY-CERT-1 composition (see spec block at the top).
inline Composition make_overlay_cert_1(FontEngine* font_engine) {
    return composition(
        {.name = "OVERLAY-CERT-1",
         .width = kOverlayCertWidth,
         .height = kOverlayCertHeight,
         .frame_rate = FrameRate{kOverlayCertFps, 1},
         .duration = kOverlayCertFrames},
        [font_engine](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(font_engine);

            // Background: dark solid.  Consumers needing a video background
            // render this fixture over their own video layer instead of
            // mutating the fixture.
            s.layer("bg", [](LayerBuilder& l) {
                l.fill(kOverlayCertBackground);
            });

            // Headline: TopCenter, font 64, shadow, fade-in 10 frames.
            s.layer("headline", [font_engine](LayerBuilder& l) {
                l.font_engine(font_engine);
                l.drop_shadow(
                    Vec2{kOverlayCertShadowOffset, kOverlayCertShadowOffset},
                    Color{0.0f, 0.0f, 0.0f, 0.80f},
                    kOverlayCertShadowRadius);
                l.transition_in(overlay_cert_fade(kOverlayCertHeadlineFadeFrames));
                l.text("headline", TextDefinition{
                    .content = {.value = kOverlayCertHeadlineText},
                    .style = {.font = {
                        .font_path = "assets/fonts/Inter-Bold.ttf",
                        .font_family = "Inter",
                        .font_weight = 700,
                        .font_size = kOverlayCertHeadlineFontSize,
                    }, .color = Color::white()},
                    .frame = {
                        .size = {1000.0f, 120.0f},
                        .placement = TextPlacement{
                            TextPlacementKind::TopCenter, {0.0f, 100.0f}},
                        .anchor = TextAnchor::Center,
                        .align = TextAlign::Center,
                        .vertical_align = VerticalAlign::Middle,
                        .centering_mode = TextCenteringMode::PixelInk,
                    }
                });
            });

            // Subtitle: BottomCenter, font 42, shadow, fade-in 5 frames.
            s.screen_layer("subtitle", [font_engine](LayerBuilder& l) {
                l.font_engine(font_engine);
                l.drop_shadow(
                    Vec2{kOverlayCertShadowOffset, kOverlayCertShadowOffset},
                    Color{0.0f, 0.0f, 0.0f, 0.80f},
                    kOverlayCertShadowRadius);
                l.transition_in(overlay_cert_fade(kOverlayCertSubtitleFadeFrames));
                l.text("subtitle", TextDefinition{
                    .content = {.value = kOverlayCertSubtitleText},
                    .style = {.font = {
                        .font_path = "assets/fonts/Inter-Regular.ttf",
                        .font_family = "Inter",
                        .font_weight = 400,
                        .font_size = kOverlayCertSubtitleFontSize,
                    }, .color = Color{0.92f, 0.92f, 0.96f, 1.0f}},
                    .frame = {
                        .size = {900.0f, 90.0f},
                        .placement = TextPlacement{
                            TextPlacementKind::BottomCenter, {0.0f, -80.0f}},
                        .anchor = TextAnchor::Center,
                        .align = TextAlign::Center,
                        .vertical_align = VerticalAlign::Middle,
                        .centering_mode = TextCenteringMode::PixelInk,
                    }
                });
            });

            // Watermark: TopRight (20 px margin), 180 px, opacity 0.85,
            // fade-in 8 frames.
            s.screen_layer("watermark", [](LayerBuilder& l) {
                l.pin_to(Anchor::TopRight, kOverlayCertWatermarkMargin);
                l.opacity(kOverlayCertWatermarkOpacity);
                l.transition_in(overlay_cert_fade(kOverlayCertWatermarkFadeFrames));
                l.image("watermark", ImageParams{
                    .asset_path = kOverlayCertWatermarkAsset,
                    .size = {kOverlayCertWatermarkWidth, kOverlayCertWatermarkWidth},
                    .fit = FitMode::Contain,
                });
            });

            return s.build();
        });
}

} // namespace chronon3d::test
