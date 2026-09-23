#pragma once
// ============================================================================
// apps/chronon3d_cli/canary/canary_common.hpp
//
// The canary authoring kit every canary subfile shares: the constants, the
// timing/easing helpers, the text shape, the orbit camera and the two
// composition specs.
//
// These are `inline` definitions in `chronon3d::canary_detail`, so each
// registration subfile compiles the same kit without adding a link-time
// surface.  Before the pack was split by family they lived in the anonymous
// namespace of the single register_canary_compositions.cpp; the numbers and
// the semantics are unchanged.
// ============================================================================

#include <algorithm>
#include <array>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/effects/presets/glow_presets.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <cmath>
#include <string>

namespace chronon3d::canary_detail {

    constexpr i32 kCanaryWidth = 1920;
    constexpr i32 kCanaryHeight = 1080;
    constexpr i32 kCanaryFps = 30;
    constexpr Frame kCanaryDuration{90};
    constexpr Frame kBlackGlowDuration{150};
    constexpr Frame kTypewriterRevealEnd{54}; // 1.8 s at 30 fps; then hold.

    // The fonts and the picture are logical asset paths resolved against
    // `--assets-root` at render time (the engine never touches the filesystem).
    constexpr const char *kCanaryFontBold = "tests/fixtures/Inter-Bold.ttf";
    // The fixture checkout contains the canonical Inter Bold face; Chronon3D
    // still owns font loading/shaping/rasterization for both authored roles.
    constexpr const char *kCanaryFontRegular = "tests/fixtures/Inter-Bold.ttf";
    constexpr const char *kCanaryImageCard = "assets/images/checker.png";

    struct AppleV3Font {
        const char *suffix;
        const char *path;
    };
    inline constexpr std::array<AppleV3Font, 5> kAppleV3Fonts{{
        {"inter", "assets/fonts/Inter-Bold.ttf"},
        {"poppins", "assets/fonts/Poppins-Bold.ttf"},
        {"dmsans", "assets/fonts/DMSans-Bold.ttf"},
        {"georgia", "assets/fonts/Georgia_Bold.ttf"},
        {"freeserif", "assets/fonts/FreeSerif.ttf"},
    }};

    constexpr Color kCanaryVoid{0.035f, 0.043f, 0.075f, 1.0f};

    /// Sub-frame progress in [0,1] over the composition's own duration.  Keyed off
    /// the frame the context carries, so the value is a property of the frame and
    /// not of the order the frames were asked for.
    [[nodiscard]] inline f32 canary_progress(const FrameContext &ctx) {

        const i32 duration = ctx.duration().integral();
        const i32 frame = ctx.frame().integral();
        if (duration <= 1)
            return 0.0f;
        return std::clamp(static_cast<f32>(frame) / static_cast<f32>(duration - 1), 0.0f, 1.0f);
    }

    [[nodiscard]] inline f32 canary_lerp(f32 a, f32 b, f32 t) {
        return a + (b - a) * t;
    }

    [[nodiscard]] inline f32 canary_smoothstep(f32 t) {

        const f32 u = std::clamp(t, 0.0f, 1.0f);
        return u * u * (3.0f - 2.0f * u);
    }

    /// A centred text run in the composition's own font: the one place a text
    /// layer's shape is decided, so a metric change lands in every clip at once.
    [[nodiscard]] inline TextDefinition canary_text(std::string value, const char *font_path,
                                                    f32 font_size, i32 weight, Color color, Vec2 box,
                                                    i32 max_lines = 1) {

        TextDefinition definition;
        definition.content.value = std::move(value);
        definition.style.font.font_path = font_path;
        definition.style.font.font_weight = weight;
        definition.style.font.font_size = font_size;
        definition.style.color = color;
        definition.frame.size = box;
        definition.frame.placement =
            TextPlacement{TextPlacementKind::CanvasCenter, {0.0f, 0.0f}};
        definition.frame.anchor = TextAnchor::Center;
        definition.frame.align = TextAlign::Center;
        definition.frame.vertical_align = VerticalAlign::Middle;
        definition.frame.centering_mode = TextCenteringMode::PixelInk;
        definition.frame.max_lines = max_lines;
        return definition;
    }

    /// The camera of the hero shots: an orbit around the world origin expressed as
    /// yaw/pitch/distance, so the three numbers in the canary spec are the three
    /// arguments here and nothing is hidden in a preset's easing.
    ///
    /// The pose is written from its spherical definition rather than accumulated
    /// per frame, which is why frame 89 asked for first is bit-identical to frame
    /// 89 asked for last.
    [[nodiscard]] inline Camera2_5D canary_orbit_camera(f32 yaw_deg, f32 pitch_deg, f32 distance) {

        const f32 yaw = glm::radians(yaw_deg);
        const f32 pitch = glm::radians(pitch_deg);

        Camera2_5D camera;
        camera.enabled = true;
        camera.optics_mode = CameraOpticsMode::Zoom;
        camera.position =
            Vec3{distance * std::sin(yaw) * std::cos(pitch), distance * std::sin(pitch),
                 -distance * std::cos(yaw) * std::cos(pitch)};
        camera.zoom = 1000.0f;
        camera.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
        camera.point_of_interest_enabled = true;
        camera.is_animated = true;
        return camera;
    }

    [[nodiscard]] inline CompositionSpec canary_spec(const char *name) {

        return CompositionSpec{.name = name,
                               .width = kCanaryWidth,
                               .height = kCanaryHeight,
                               .frame_rate = FrameRate{kCanaryFps, 1},
                               .duration = kCanaryDuration};
    }

    [[nodiscard]] inline CompositionSpec black_glow_spec(const char *name) {

        return CompositionSpec{.name = name,
                               .width = kCanaryWidth,
                               .height = kCanaryHeight,
                               .frame_rate = FrameRate{kCanaryFps, 1},
                               .duration = kBlackGlowDuration};
    }

} // namespace chronon3d::canary_detail
