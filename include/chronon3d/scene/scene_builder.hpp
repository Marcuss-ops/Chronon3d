#pragma once

#include <chronon3d/scene/scene.hpp>
#include <chronon3d/scene/camera_2_5d.hpp>
#include <chronon3d/scene/builder_params.hpp>
#include <chronon3d/scene/layer_builder.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/frame.hpp>
#include <chronon3d/core/frame_context.hpp>
#include <chronon3d/video/video_source.hpp>
#include <string>
#include <functional>
#include <unordered_map>

namespace chronon3d {

class SceneBuilder {
public:
    explicit SceneBuilder(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : scene_(res), current_frame_(0) {}

    // Convenience constructor for compositions
    explicit SceneBuilder(const FrameContext& ctx)
        : scene_(ctx.resource), current_frame_(ctx.frame) {}

    SceneBuilder& rect(std::string name, RectParams p);
    SceneBuilder& rounded_rect(std::string name, RoundedRectParams p);
    SceneBuilder& circle(std::string name, CircleParams p);
    SceneBuilder& line(std::string name, LineParams p);
    SceneBuilder& text(std::string name, TextParams p);
    SceneBuilder& image(std::string name, ImageParams p);

    // Camera 2.5D — full struct override
    SceneBuilder& camera_2_5d(Camera2_5D camera) {
        scene_.set_camera_2_5d(camera);
        return *this;
    }

    // Fluent camera helpers — each mutates only the specified field.
    SceneBuilder& enable_camera_2_5d(bool enabled = true) {
        auto cam = scene_.camera_2_5d();
        cam.enabled = enabled;
        scene_.set_camera_2_5d(cam);
        return *this;
    }
    SceneBuilder& camera_position(Vec3 p) {
        auto cam = scene_.camera_2_5d();
        cam.position = p;
        scene_.set_camera_2_5d(cam);
        return *this;
    }
    SceneBuilder& camera_zoom(f32 zoom) {
        auto cam = scene_.camera_2_5d();
        cam.zoom = zoom;
        scene_.set_camera_2_5d(cam);
        return *this;
    }
    SceneBuilder& camera_fov(f32 fov_deg) {
        auto cam = scene_.camera_2_5d();
        cam.fov_deg = fov_deg;
        cam.projection_mode = Camera2_5DProjectionMode::Fov;
        scene_.set_camera_2_5d(cam);
        return *this;
    }
    SceneBuilder& camera_dof(DepthOfFieldSettings dof) {
        auto cam = scene_.camera_2_5d();
        cam.dof = dof;
        scene_.set_camera_2_5d(cam);
        return *this;
    }

    // Hierarchy
    template <typename Fn>
    SceneBuilder& layer(std::string name, Fn&& fn) {
        LayerBuilder builder(std::move(name), scene_.resource());
        std::forward<Fn>(fn)(builder);

        Layer l = builder.build();
        if (l.active_at(current_frame_)) {
            scene_.add_layer(std::move(l));
        }
        return *this;
    }

    // Adjustment layer: applies its effect stack to everything rendered before it.
    // The lambda receives a LayerBuilder but any visuals added are ignored.
    template <typename Fn>
    SceneBuilder& adjustment_layer(std::string name, Fn&& fn) {
        LayerBuilder builder(std::move(name), scene_.resource());
        std::forward<Fn>(fn)(builder);

        Layer l = builder.build();
        l.kind = LayerKind::Adjustment;
        if (l.active_at(current_frame_)) {
            scene_.add_layer(std::move(l));
        }
        return *this;
    }

    template <typename Fn>
    SceneBuilder& precomp_layer(std::string name, std::string comp_name, Fn&& fn) {
        LayerBuilder builder(std::move(name), scene_.resource());
        std::forward<Fn>(fn)(builder);

        Layer l = builder.build();
        l.kind = LayerKind::Precomp;
        l.precomp_composition_name = std::pmr::string{comp_name, scene_.resource()};
        if (l.active_at(current_frame_)) {
            scene_.add_layer(std::move(l));
        }
        return *this;
    }

    template <typename Fn>
    SceneBuilder& video_layer(std::string name, video::VideoSource source, Fn&& fn) {
        LayerBuilder builder(std::move(name), scene_.resource());
        std::forward<Fn>(fn)(builder);

        Layer l = builder.build();
        l.kind = LayerKind::Video;
        l.video_source = std::move(source);
        if (l.active_at(current_frame_)) {
            scene_.add_layer(std::move(l));
        }
        return *this;
    }

    template <typename Fn>
    SceneBuilder& video_layer(std::string name, std::string path, Fn&& fn) {
        video::VideoSource source;
        source.path = std::move(path);
        return video_layer(std::move(name), std::move(source), std::forward<Fn>(fn));
    }

    template <typename Fn>
    SceneBuilder& null(std::string name, Fn&& fn) {
        LayerBuilder builder(std::move(name), scene_.resource());
        std::forward<Fn>(fn)(builder);

        Layer l = builder.build();
        l.kind = LayerKind::Null;
        if (l.active_at(current_frame_)) {
            scene_.add_layer(std::move(l));
        }
        return *this;
    }

    // Fluent API for transformations (root level)
    SceneBuilder& at(Vec3 pos) {
        scene_.last_node().world_transform.position = pos;
        return *this;
    }

    SceneBuilder& rotate(Vec3 euler_deg) {
        scene_.last_node().world_transform.rotation = math::from_euler(euler_deg);
        return *this;
    }

    SceneBuilder& scale(Vec3 s) {
        scene_.last_node().world_transform.scale = s;
        return *this;
    }

    SceneBuilder& anchor(Vec3 a) {
        scene_.last_node().world_transform.anchor = a;
        return *this;
    }

    SceneBuilder& opacity(f32 a) {
        scene_.last_node().world_transform.opacity = a;
        return *this;
    }

    SceneBuilder& with_shadow(DropShadow shadow) {
        scene_.last_node().shadow = shadow;
        return *this;
    }

    SceneBuilder& with_glow(Glow glow) {
        scene_.last_node().glow = glow;
        return *this;
    }

    // Legacy Overloads (Deprecated)
    [[deprecated("Use rect(id, RectParams)")]]
    SceneBuilder& rect(std::string name, Vec3 position, Color color, Vec2 size = {100, 100}) {
        return rect(std::move(name), {.size = size, .color = color, .pos = position});
    }

    [[deprecated("Use rounded_rect(id, RoundedRectParams)")]]
    SceneBuilder& rounded_rect(std::string name, Vec3 position, Vec2 size, f32 radius, Color color) {
        return rounded_rect(std::move(name), {.size = size, .radius = radius, .color = color, .pos = position});
    }

    [[deprecated("Use circle(id, CircleParams)")]]
    SceneBuilder& circle(std::string name, Vec3 position, f32 radius, Color color) {
        return circle(std::move(name), {.radius = radius, .color = color, .pos = position});
    }

    [[deprecated("Use text(id, TextParams)")]]
    SceneBuilder& text(std::string name, std::string content, Vec3 position, const TextStyle& style) {
        return text(std::move(name), {.content = std::move(content), .style = style, .pos = position});
    }

    [[deprecated("Use line(id, LineParams)")]]
    SceneBuilder& line(std::string name, Vec3 start, Vec3 end, Color color, f32 thickness = 1.0f) {
        return line(std::move(name), {.from = start, .to = end, .thickness = thickness, .color = color});
    }

    [[nodiscard]] Scene build() {
        resolve_hierarchy();
        return std::move(scene_);
    }

    [[nodiscard]] std::pmr::memory_resource* resource() const { return scene_.resource(); }
    [[nodiscard]] Frame frame() const { return current_frame_; }

private:
    void resolve_hierarchy();

    Scene scene_;
    Frame current_frame_;
};

} // namespace chronon3d
