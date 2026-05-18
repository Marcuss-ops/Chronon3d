// MotionDepthProof
//   chronon3d_cli render MotionDepthProof --graph --frames 0  -o output/proofs/motion_depth_f000.png
//   chronon3d_cli render MotionDepthProof --graph --frames 45 -o output/proofs/motion_depth_f045.png
//   chronon3d_cli render MotionDepthProof --graph --frames 89 -o output/proofs/motion_depth_f089.png
//   chronon3d_cli video  MotionDepthProof --graph --start 0 --end 90 --fps 30 -o output/proofs/motion_depth_proof.mp4

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

#include <array>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::presets::motion;

static Composition motion_depth_proof() {
    return composition({
        .name = "MotionDepthProof",
        .width = 1280,
        .height = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;
        s.camera().set(camera_motion::parallax_sweep(t, 12.0f, -1000.0f, 1000.0f));

        scene::utils::dark_grid_background(s, ctx, {
            .bg_color = Color{0.045f, 0.045f, 0.055f, 1.0f},
            .grid_color = Color{1.0f, 1.0f, 1.0f, 0.045f},
            .spacing = 80.0f,
            .extent = 2800.0f,
            .centered = true,
        });

        s.layer("header", [](LayerBuilder& l) {
            l.position({0.0f, -280.0f, 0.0f});
            l.text("depth_title", {
                .content = "2.5D DEPTH PROOF",
                .style = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size = 58.0f,
                    .color = Color{0.96f, 0.96f, 0.98f, 1.0f},
                    .align = TextAlign::Center,
                    .tracking = 0.8f,
                },
            });
        });

        struct CardSpec {
            const char* id;
            const char* label;
            Vec2 pos;
            f32 z;
            Color color;
        };

        const std::array<CardSpec, 5> cards{{
            {"back", "z = -500", {-360.0f, 0.0f}, -500.0f, Color{0.14f, 0.20f, 0.88f, 1.0f}},
            {"mid_back", "z = -300", {-170.0f, 60.0f}, -300.0f, Color{0.12f, 0.68f, 0.88f, 1.0f}},
            {"center", "z = -100", {0.0f, 0.0f}, -100.0f, Color{0.18f, 0.82f, 0.28f, 1.0f}},
            {"front", "z = 100", {180.0f, 72.0f}, 100.0f, Color{1.0f, 0.60f, 0.18f, 1.0f}},
            {"text", "z = 180", {380.0f, -20.0f}, 180.0f, Color{1.0f, 0.18f, 0.18f, 1.0f}},
        }};

        std::vector<MotionObject> objects;
        objects.reserve(cards.size());

        for (const auto& card : cards) {
            objects.push_back(MotionObject::group(card.id, {
                MotionObject::rounded_rect("panel")
                    .at({0.0f, 0.0f, 0.0f})
                    .size({210.0f, 210.0f})
                    .color(Color{0.09f, 0.09f, 0.11f, 1.0f}),

                MotionObject::rect("stripe")
                    .at({0.0f, -72.0f, 1.0f})
                    .size({170.0f, 8.0f})
                    .color(card.color),

                MotionObject::text("label", card.label)
                    .at({0.0f, 86.0f, 2.0f})
                    .font_path("assets/fonts/Inter-Bold.ttf")
                    .font_size(24.0f)
                    .tracking(0.3f)
                    .color(Color::white()),
            })
                .at({card.pos.x, card.pos.y, card.z})
                .preset(MotionPreset::ParallaxDrift)
                .time(0, 90)
                .enable_3d()
                .parallax(1.2f)
            );
        }

        draw_motion_objects(s, ctx, objects);
        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("MotionDepthProof", motion_depth_proof)
