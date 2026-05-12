#pragma once

#include <chronon3d/renderer/renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>

namespace chronon3d {

class SoftwareRenderer : public Renderer {
public:
    std::unique_ptr<Framebuffer> render_frame(const Composition& comp, Frame frame) override {
        auto fb = std::make_unique<Framebuffer>(comp.width(), comp.height());
        fb->clear(Color::black()); // Background color

        for (const auto& layer : comp.layers()) {
            if (!layer->is_active(frame)) continue;

            // Evaluate animated properties
            Transform transform = layer->transform.evaluate(frame);
            f32 opacity = layer->opacity.evaluate(frame);

            // Simple 2D rect rendering for now
            // We assume position is center and we draw a 100x100 rect
            // In the future, this will be more sophisticated
            draw_rect(*fb, transform, Color::white() * opacity, BlendMode::Normal);
        }

        return fb;
    }

private:
    void draw_rect(Framebuffer& fb, const Transform& transform, const Color& color, BlendMode mode) {
        // Simple 2D rect: center at transform.position.x, y
        i32 cx = static_cast<i32>(transform.position.x);
        i32 cy = static_cast<i32>(transform.position.y);
        i32 half_w = 50; // default size
        i32 half_h = 50;

        for (i32 y = cy - half_h; y < cy + half_h; ++y) {
            for (i32 x = cx - half_w; x < cx + half_w; ++x) {
                if (x < 0 || x >= fb.width() || y < 0 || y >= fb.height()) continue;
                
                Color dst = fb.get_pixel(x, y);
                Color blended = compositor::blend(color, dst, mode);
                fb.set_pixel(x, y, blended);
            }
        }
    }
};

} // namespace chronon3d
