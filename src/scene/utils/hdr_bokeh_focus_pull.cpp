// ============================================================================
// hdr_bokeh_focus_pull.cpp — native C++ HDR bokeh composition.
//
// Scene contract:
//   • 1920×1080 @ 30 fps, 180 frames.
//   • Deterministic HDR emitter field: linear RGB values far above 1.0 so the
//     working framebuffer is genuinely scene-referred, not an SDR glow.
//   • CameraRing with 64 golden-angle taps; its optical center racks left and
//     right while the maximum CoC breathes, which is the visible focus pull.
//   • Camera focus_distance racks near→far→near in sync (real camera state,
//     consumed by the physical DoF model when a DoF path is active).
//   • The title/subtitle stay on their own sharp layers; the HDR field carries
//     the single CameraRing effect (the native fast path requires a one-effect
//     stack).
//   • Compression to SDR is NOT done in the scene: the output boundary applies
//     ACES (--tone-map aces) before the linear→sRGB transfer.
// ============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/composition/register_builtin_compositions.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/presets/font_asset_paths.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace chronon3d {
namespace {

constexpr i32 kWidth = 1920;
constexpr i32 kHeight = 1080;
constexpr Frame kDuration{180};
constexpr FrameRate kFrameRate{30, 1};
constexpr float kGoldenAngleRadians = 2.39996323f;
constexpr float kTwoPi = 6.28318530718f;

struct BokehLight {
    float x{0.0f};
    float y{0.0f};
    float radius{8.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

// Scene-linear emitter palette. Values above 1.0 are intentional: they are
// what makes the bokeh genuinely HDR before the output tone map.
constexpr std::array<Color, 6> kHdrPalette{
    Color{9.5f, 5.8f, 2.4f, 1.0f},
    Color{2.2f, 5.4f, 11.0f, 1.0f},
    Color{8.4f, 2.6f, 6.6f, 1.0f},
    Color{12.0f, 10.5f, 8.0f, 1.0f},
    Color{1.8f, 8.8f, 5.2f, 1.0f},
    Color{4.6f, 3.2f, 10.0f, 1.0f},
};

// Deterministic golden-angle ring of emitters outside the title box. No RNG,
// so every run produces the same field and the scene hash is stable.
const std::array<BokehLight, 30>& bokeh_lights() {
    static const std::array<BokehLight, 30> lights = [] {
        std::array<BokehLight, 30> result{};
        for (std::size_t i = 0; i < result.size(); ++i) {
            const float angle =
                static_cast<float>(i) * kGoldenAngleRadians + 0.4f;
            const float ring =
                0.56f + 0.39f *
                    static_cast<float>((static_cast<int>(i) * 7) % 13) / 12.0f;
            result[i] = BokehLight{
                .x = std::cos(angle) * ring * (static_cast<float>(kWidth) * 0.5f),
                .y = std::sin(angle) * ring * (static_cast<float>(kHeight) * 0.5f),
                .radius = 5.0f + static_cast<float>((i * 5) % 4) * 5.5f,
                .color = kHdrPalette[i % kHdrPalette.size()],
            };
        }
        return result;
    }();
    return lights;
}

Composition hdr_bokeh_focus_pull_composition() {
    CompositionSpec spec{
        .name = "HdrBokehFocusPull",
        .width = kWidth,
        .height = kHeight,
        .frame_rate = kFrameRate,
        .duration = kDuration,
    };

    return Composition(spec, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float progress = ctx.duration() > Frame{0}
            ? static_cast<float>(std::clamp(
                  static_cast<double>(ctx.frame().as_i64()) /
                      static_cast<double>(ctx.duration().as_i64()),
                  0.0, 1.0))
            : 0.0f;
        // 0 → 1 → 0: a full rack near→far→near over the shot.
        const float pull = 0.5f - 0.5f * std::cos(progress * kTwoPi);
        const float seconds =
            static_cast<float>(ctx.frame().as_i64()) /
            static_cast<float>(kFrameRate.numerator);

        s.layer("background", [](LayerBuilder& l) {
            l.fill(Color{0.006f, 0.008f, 0.013f, 1.0f});
        });

        // HDR emitter field + the one CameraRing effect. The optical center
        // sweeps sideways (focus pull), while the CoC breathes with `pull`.
        s.layer("hdr_bokeh_field", [progress, pull](LayerBuilder& l) {
            // The emitter field is a screen-space optical element: keep it out
            // of the 2.5D camera projection so the native Vulkan ellipse path
            // can rasterize the HDR discs. The camera still drives the DoF and
            // the CameraRing center/focus rack below.
            l.screen_space();
            // Transparent full-canvas extent. The native CameraRing fast path
            // requires the layer clip to cover the blurred result, and the
            // emitter ring alone would clip the CoC at its outer bounds.
            l.fullscreen_rect("field_extent", Color{0.0f, 0.0f, 0.0f, 0.0f});
            const auto& lights = bokeh_lights();
            char name[32];
            for (std::size_t i = 0; i < lights.size(); ++i) {
                std::snprintf(name, sizeof(name), "bokeh_%02zu", i);
                l.circle(name, CircleParams{
                    .radius = lights[i].radius,
                    .color = lights[i].color,
                    .pos = {lights[i].x, lights[i].y, 0.0f},
                });
            }
            l.effect(CameraRingParams{
                .center = {
                    0.5f + 0.18f * std::sin(progress * kTwoPi),
                    0.5f + 0.06f * std::sin(progress * kTwoPi * 2.0f),
                },
                .axis_scale = {0.74f, 0.32f},
                .inner_radius = 0.16f + 0.11f * pull,
                .outer_radius = 0.78f,
                .max_blur_radius = 0.034f - 0.019f * pull,
                .chroma_dispersion = 0.008f,
                .anamorphic_stretch = 1.0f,
                .samples = 64,
            });
        });

        // Camera state is authored per frame in C++: a slow drift plus a real
        // focus-distance rack that mirrors the CameraRing focus pull.
        DepthOfFieldSettings dof;
        dof.enabled = true;
        dof.focus_distance = 560.0f + 520.0f * pull;
        dof.max_blur = 18.0f;
        dof.highlight_gain = 1.0f;

        s.set_evaluated_camera(
            camera()
                .enabled(true)
                .position({
                    34.0f * std::sin(seconds * 0.42f),
                    12.0f * std::sin(seconds * 0.31f),
                    -1010.0f + 14.0f * std::sin(seconds * 0.23f),
                })
                .rotation({0.0f, 2.2f * std::sin(seconds * 0.35f), 0.0f})
                .zoom(1000.0f)
                .dof(dof)
                .build());

        // The title participates in the same HDR range (values > 1) but is on
        // a sharp layer, so the tone map is what brings it back to SDR.
        s.layer("title", [](LayerBuilder& l) {
            l.text("title", TextDefinition{
                .content = {.value = "BOKEH"},
                .style = {
                    .font = {
                        .font_path = presets::inter_black_font_path(),
                        .font_family = "Inter",
                        .font_weight = 900,
                        .font_size = 164.0f,
                    },
                    .color = Color{2.4f, 2.5f, 2.8f, 1.0f},
                },
                .frame = {
                    .size = {1200.0f, 240.0f},
                    .placement = TextPlacement{
                        TextPlacementKind::Center, {0.0f, -28.0f}},
                    .anchor = TextAnchor::Center,
                    .align = TextAlign::Center,
                    .vertical_align = VerticalAlign::Middle,
                    .centering_mode = TextCenteringMode::PixelInk,
                },
            });
        });

        s.layer("subtitle", [](LayerBuilder& l) {
            l.text("subtitle", TextDefinition{
                .content = {.value = "HDR TONEMAP · FOCUS PULL · 64 TAP"},
                .style = {
                    .font = {
                        .font_path = presets::inter_light_font_path(),
                        .font_family = "Inter",
                        .font_weight = 300,
                        .font_size = 34.0f,
                    },
                    .color = Color{0.72f, 0.84f, 1.0f, 1.0f},
                },
                .frame = {
                    .size = {1300.0f, 80.0f},
                    .placement = TextPlacement{
                        TextPlacementKind::Center, {0.0f, 118.0f}},
                    .anchor = TextAnchor::Center,
                    .align = TextAlign::Center,
                    .vertical_align = VerticalAlign::Middle,
                    .centering_mode = TextCenteringMode::PixelInk,
                },
            });
        });

        return s.build();
    });
}

} // namespace

void register_hdr_bokeh_focus_pull(CompositionRegistry& registry) {
    registry.add(make_composition_descriptor(
        "HdrBokehFocusPull", [](const CompositionProps&) {
            return hdr_bokeh_focus_pull_composition();
        }));
}

} // namespace chronon3d
