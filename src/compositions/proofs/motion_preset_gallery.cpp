// MotionPresetGallery
//   chronon3d_cli render MotionPresetGallery --graph --frames 0   -o output/proofs/motion_preset_gallery_f000.png
//   chronon3d_cli render MotionPresetGallery --graph --frames 60  -o output/proofs/motion_preset_gallery_f060.png
//   chronon3d_cli render MotionPresetGallery --graph --frames 120 -o output/proofs/motion_preset_gallery_f120.png
//   chronon3d_cli video  MotionPresetGallery --graph --start 0 --end 120 --fps 30 -o output/proofs/motion_preset_gallery.mp4

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

#include <array>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::presets::motion;

static Composition motion_preset_gallery() {
    return composition({
        .name = "MotionPresetGallery",
        .width = 1280,
        .height = 720,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        scene::utils::dark_grid_background(s, ctx, {
            .bg_color = Color{0.04f, 0.04f, 0.05f, 1.0f},
            .grid_color = Color{1.0f, 1.0f, 1.0f, 0.04f},
            .spacing = 64.0f,
            .extent = 2800.0f,
            .centered = true,
        });

        struct TileSpec {
            const char* id;
            const char* label;
            MotionPreset preset;
            Color accent;
        };

        const std::array<TileSpec, 10> tiles{{
            {"fade_in", "FadeIn", MotionPreset::FadeIn, Color{0.70f, 0.84f, 1.0f, 1.0f}},
            {"pop_in", "PopIn", MotionPreset::PopIn, Color{0.95f, 0.82f, 0.40f, 1.0f}},
            {"pop_glow", "PopGlow", MotionPreset::PopGlow, Color{1.0f, 0.55f, 0.60f, 1.0f}},
            {"slide_up", "SlideUp", MotionPreset::SlideUp, Color{0.55f, 0.95f, 0.74f, 1.0f}},
            {"slide_left", "SlideLeft", MotionPreset::SlideLeft, Color{0.92f, 0.64f, 1.0f, 1.0f}},
            {"zoom_blur", "ZoomBlur", MotionPreset::ZoomBlur, Color{0.96f, 0.76f, 0.46f, 1.0f}},
            {"push_3d", "PushIn3D", MotionPreset::PushIn3D, Color{0.85f, 0.87f, 0.95f, 1.0f}},
            {"parallax", "ParallaxDrift", MotionPreset::ParallaxDrift, Color{0.62f, 0.74f, 1.0f, 1.0f}},
            {"orbit", "Orbit2_5D", MotionPreset::Orbit2_5D, Color{1.0f, 0.63f, 0.45f, 1.0f}},
            {"shake", "ShakeImpact", MotionPreset::ShakeImpact, Color{1.0f, 0.45f, 0.45f, 1.0f}},
        }};

        const std::array<Vec2, 10> positions{{
            {-460.0f, -150.0f}, {-230.0f, -150.0f}, {0.0f, -150.0f}, {230.0f, -150.0f}, {460.0f, -150.0f},
            {-460.0f,  120.0f}, {-230.0f,  120.0f}, {0.0f,  120.0f}, {230.0f,  120.0f}, {460.0f,  120.0f},
        }};

        std::vector<MotionObject> objects;
        objects.reserve(tiles.size());

        for (usize i = 0; i < tiles.size(); ++i) {
            const auto& tile = tiles[i];
            const auto pos = positions[i];
            const float phase = static_cast<float>(i) * 0.15f;

            objects.push_back(MotionObject::group(tile.id, {
                MotionObject::rounded_rect("card")
                    .at({0.0f, 0.0f, 0.0f})
                    .size({210.0f, 180.0f})
                    .color(Color{0.10f, 0.10f, 0.12f, 1.0f}),

                MotionObject::rect("accent")
                    .at({0.0f, -35.0f, 1.0f})
                    .size({150.0f, 8.0f})
                    .color(tile.accent),

                MotionObject::text("label", tile.label)
                    .at({0.0f, 50.0f, 2.0f})
                    .font_path("assets/fonts/Inter-Bold.ttf")
                    .font_size(24.0f)
                    .tracking(0.3f)
                    .color(Color::white()),

                MotionObject::text("subtitle", "preview")
                    .at({0.0f, 82.0f, 2.0f})
                    .font_path("assets/fonts/Inter-Regular.ttf")
                    .font_size(16.0f)
                    .tracking(0.2f)
                    .color(Color{0.7f, 0.72f, 0.78f, 1.0f}),

                MotionObject::circle("icon")
                    .at({0.0f, -15.0f, 2.0f})
                    .size({52.0f, 52.0f})
                    .color(tile.accent),
            })
                .at({pos.x, pos.y, 0.0f})
                .preset(tile.preset)
                .time(0, 120)
                .enable_3d(0.0f)
                .motion_position({0.0f, std::sin(phase) * 6.0f, 0.0f})
                .motion_scale({1.0f, 1.0f, 1.0f})
            );
        }

        draw_motion_objects(s, ctx, objects);
        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("MotionPresetGallery", motion_preset_gallery)
