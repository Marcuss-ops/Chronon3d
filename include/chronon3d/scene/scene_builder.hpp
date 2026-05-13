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
    SceneBuilder& camera_parent(std::string name) {
        auto cam = scene_.camera_2_5d();
        cam.parent_name = std::pmr::string{name, scene_.resource()};
        scene_.set_camera_2_5d(cam);
        return *this;
    }
    SceneBuilder& camera_rotation(Vec3 euler_deg) {
        auto cam = scene_.camera_2_5d();
        cam.rotation = euler_deg;
        scene_.set_camera_2_5d(cam);
        return *this;
    }
    SceneBuilder& camera_point_of_interest(Vec3 poi) {
        auto cam = scene_.camera_2_5d();
        cam.point_of_interest = poi;
        scene_.set_camera_2_5d(cam);
        return *this;
    }
    SceneBuilder& camera_look_at(Vec3 poi) {
        return camera_point_of_interest(poi);
    }
    SceneBuilder& camera_target(std::string name) {
        auto cam = scene_.camera_2_5d();
        cam.target_name = std::pmr::string{name, scene_.resource()};
        scene_.set_camera_2_5d(cam);
        return *this;
    }

    // Fake-3D compound helpers — create a single enable_3d() layer automatically.
    // The pos field in params sets both layer world position (for depth sorting)
    // and the shape's world_pos (for proper face projection).
    SceneBuilder& fake_box3d_layer(std::string name, FakeBox3DParams p) {
        return layer(std::move(name), [p](LayerBuilder& l) {
            l.enable_3d().position(p.pos).fake_box3d("box", p);
        });
    }

    SceneBuilder& grid_plane_layer(std::string name, GridPlaneParams p) {
        return layer(std::move(name), [p](LayerBuilder& l) {
            l.enable_3d().position(p.pos).grid_plane("grid", p);
        });
    }

    // Contact shadow: double-layered blurred rounded rect for realism.
    // Includes a soft "footprint" and a tighter "occlusion" core.
    SceneBuilder& contact_shadow_layer(std::string name, ContactShadowParams p) {
        // Core "occlusion" shadow (sharper, darker, smaller)
        layer(name + "_core", [p](LayerBuilder& l) {
            l.enable_3d()
             .position(p.pos + Vec3{0, 1.0f, 0}) // slightly above to avoid Z-fight
             .rounded_rect("core", {
                 .size   = p.size * 0.75f,
                 .radius = std::min(p.size.x, p.size.y) * 0.25f,
                 .color  = p.color.with_alpha(p.opacity * 0.9f),
             })
             .blur(p.blur * 0.3f);
        });

        // Soft ambient shadow (larger, softer)
        return layer(std::move(name), [p](LayerBuilder& l) {
            l.enable_3d()
             .position(p.pos)
             .rounded_rect("soft", {
                 .size   = p.size,
                 .radius = std::min(p.size.x, p.size.y) * 0.35f,
                 .color  = p.color.with_alpha(p.opacity * 0.6f),
             })
             .blur(p.blur);
        });
    }

    // Fake extruded text: stacks N offset copies (side color) then a front layer.
    SceneBuilder& fake_extruded_text_layer(std::string base_name, FakeExtrudedTextParams p) {
        // Side layers: deepest first (renders behind front)
        for (int i = p.depth; i >= 1; --i) {
            const f32 fi = static_cast<f32>(i);
            const Vec3 off{p.extrude_dir.x * fi, p.extrude_dir.y * fi, 0};
            // Side shading: gets slightly darker with depth
            const f32 shade = 1.0f - 0.20f * (fi / static_cast<f32>(p.depth));
            const Color sc = p.side_color.with_alpha(p.side_color.a * shade);
            
            layer(base_name + "_s" + std::to_string(i), [p, off, sc](LayerBuilder& l) {
                l.position(p.pos + off)
                 .text("t", {
                     .content = p.text,
                     .style   = {.font_path = p.font_path, .size = p.font_size,
                                 .color = sc, .align = p.align}
                 });
            });
        }
        // Front face
        return layer(base_name, [p](LayerBuilder& l) {
            l.position(p.pos)
             .text("t", {
                 .content = p.text,
                 .style   = {.font_path = p.font_path, .size = p.font_size,
                             .color = p.front_color, .align = p.align}
             });
        });
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
    SceneBuilder& null_layer(std::string name, Fn&& fn) {
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
        return std::move(scene_);
    }

    [[nodiscard]] std::pmr::memory_resource* resource() const { return scene_.resource(); }
    [[nodiscard]] Frame frame() const { return current_frame_; }

private:
    Scene scene_;
    Frame current_frame_;
};

} // namespace chronon3d
