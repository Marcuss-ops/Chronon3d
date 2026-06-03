#include <chronon3d/scene/camera/camera_debug_overlay.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

namespace chronon3d {

void add_camera_debug_overlay(
    SceneBuilder& s,
    const CameraShotReport& report,
    CameraDebugOverlayOptions options
) {
    s.layer("camera_debug_hud", [&](LayerBuilder& l) {
        if (options.show_safe_area) {
            l.rect("safe_area_hud", RectParams{
                .size = {1920.0f * 0.90f, 1080.0f * 0.90f},
                .pos = {1920.0f * 0.05f, 1080.0f * 0.05f, 0.0f},
                .fill = Fill{ .enabled = false },
                .stroke = { .enabled = true, .color = Color{1.0f, 0.8f, 0.0f, 0.35f}, .width = 1.5f }
            });
        }

        if (options.show_target) {
            l.rect("center_target_guide", RectParams{
                .size = {30.0f, 30.0f},
                .pos = {960.0f - 15.0f, 540.0f - 15.0f, 0.0f},
                .fill = Fill{ .enabled = false },
                .stroke = { .enabled = true, .color = Color{1.0f, 0.0f, 1.0f, 0.7f}, .width = 2.0f }
            });
        }

        if (options.show_projected_bounds) {
            int idx = 0;
            for (const auto& lr : report.layers) {
                Color border_color = lr.passed ? Color{0.0f, 1.0f, 0.0f, 0.6f} : Color{1.0f, 0.0f, 0.0f, 0.8f};
                
                Vec2 b_size = lr.bounds.max - lr.bounds.min;
                if (b_size.x > 0.0f && b_size.y > 0.0f) {
                    std::string node_name = "bounds_hud_" + lr.name + "_" + std::to_string(idx);
                    l.rect(node_name, RectParams{
                        .size = b_size,
                        .pos = {lr.bounds.min.x, lr.bounds.min.y, 0.0f},
                        .fill = Fill{ .enabled = false },
                        .stroke = { .enabled = true, .color = border_color, .width = 1.5f }
                    });

                    if (options.show_layer_names) {
                        std::string label_name = "label_hud_" + lr.name + "_" + std::to_string(idx);
                        l.text(label_name, TextParams{
                            .text = lr.name + (lr.passed ? " (PASS)" : " (FAIL)"),
                            .pos = {lr.bounds.min.x + 5.0f, lr.bounds.min.y + 15.0f, 0.0f},
                            .font_size = 12.0f,
                            .color = border_color
                        });
                    }
                }
                idx++;
            }
        }
    });
}

} // namespace chronon3d
