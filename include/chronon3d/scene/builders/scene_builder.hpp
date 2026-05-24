#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/camera_api.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>


namespace chronon3d {

    class SceneBuilder {
      public:
        explicit SceneBuilder(std::pmr::memory_resource *res = std::pmr::get_default_resource())
            : scene_(res), current_frame_(0) {
            m_ctx.resource = res;
            m_ctx.width = m_width;
            m_ctx.height = m_height;
        }

        explicit SceneBuilder(i32 width, i32 height, std::pmr::memory_resource *res = std::pmr::get_default_resource())
            : scene_(res), current_frame_(0), m_width(width), m_height(height) {
            m_ctx.resource = res;
            m_ctx.width = width;
            m_ctx.height = height;
        }

        // Convenience constructor for compositions
        explicit SceneBuilder(const FrameContext &ctx)
            : scene_(ctx.resource), current_frame_(ctx.frame), m_ctx(ctx), m_width(ctx.width), m_height(ctx.height) {}

        [[nodiscard]] CameraApi camera() { return CameraApi(*this); }

        SceneBuilder &ambient_light(Color color = Color{1, 1, 1, 1}, f32 intensity = 0.2f);

        SceneBuilder &directional_light(Vec3 direction, Color color = Color{1, 1, 1, 1},
                                        f32 intensity = 1.0f);

        SceneBuilder &rect(std::string name, RectParams p);
        SceneBuilder &rounded_rect(std::string name, RoundedRectParams p);
        SceneBuilder &circle(std::string name, CircleParams p);
        SceneBuilder &line(std::string name, LineParams p);
        SceneBuilder &path(std::string name, PathParams p);
        SceneBuilder &image(std::string name, ImageParams p);
        SceneBuilder &shape(std::string_view id, std::string name, registry::ShapeParams params);

        // text() removed — text engine deprecated

        struct SequenceSpec {
            Frame from{0};
            Frame duration{0};
        };

        template <typename Fn>
        SceneBuilder &sequence(const std::string & /*name*/, SequenceSpec spec, Fn &&fn) {
            bool active = current_frame_ >= spec.from && current_frame_ < spec.from + spec.duration;
            if (!active) {
                return *this;
            }

            FrameContext local_ctx = m_ctx;
            local_ctx.frame = Frame{current_frame_ - spec.from};
            local_ctx.local_frame = local_ctx.frame;
            local_ctx.duration = spec.duration;

            SceneBuilder sub_builder(local_ctx);
            std::forward<Fn>(fn)(sub_builder);

            Scene sub_scene = sub_builder.build();

            for (auto &layer : sub_scene.layers()) {
                if (layer.duration >= 0) {
                    layer.from += spec.from;
                } else {
                    layer.from = spec.from;
                    layer.duration = spec.duration;
                }
                scene_.add_layer(std::move(layer));
            }

            for (auto &node : sub_scene.nodes()) {
                scene_.add_node(std::move(node));
            }

            return *this;
        }

        // Standard Layers
        template <typename Fn>
        SceneBuilder &layer(std::string name, Fn &&fn) {
            LayerBuilder builder(std::move(name), current_frame_, scene_.resource());
            std::forward<Fn>(fn)(builder);

            Layer l = builder.build();
            if (l.active_at(current_frame_)) {
                scene_.add_layer(std::move(l));
            }
            return *this;
        }

        template <typename Fn>
        SceneBuilder &screen_layer(std::string name, Fn &&fn) {
            LayerBuilder builder(std::move(name), current_frame_, scene_.resource());
            builder.screen_dimensions(static_cast<f32>(m_width), static_cast<f32>(m_height));
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
        SceneBuilder &adjustment_layer(std::string name, Fn &&fn) {
            LayerBuilder builder(std::move(name), current_frame_, scene_.resource());
            std::forward<Fn>(fn)(builder);

            Layer l = builder.build();
            l.kind = LayerKind::Adjustment;
            if (l.active_at(current_frame_)) {
                scene_.add_layer(std::move(l));
            }
            return *this;
        }

        template <typename Fn>
        SceneBuilder &precomp_layer(std::string name, std::string comp_name, Fn &&fn) {
            LayerBuilder builder(std::move(name), current_frame_, scene_.resource());
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
        SceneBuilder &video_layer(std::string name, video::VideoSource source, Fn &&fn) {
            LayerBuilder builder(std::move(name), current_frame_, scene_.resource());
            std::forward<Fn>(fn)(builder);

            Layer l = builder.build();
            l.kind = LayerKind::Video;
            l.video_source = std::make_unique<video::VideoSource>(std::move(source));
            if (l.active_at(current_frame_)) {
                scene_.add_layer(std::move(l));
            }
            return *this;
        }

        template <typename Fn>
        SceneBuilder &video_layer(std::string name, std::string path, Fn &&fn) {
            video::VideoSource source;
            source.path = std::move(path);
            return video_layer(std::move(name), std::move(source), std::forward<Fn>(fn));
        }

        template <typename Fn>
        SceneBuilder &null_layer(std::string name, Fn &&fn) {
            LayerBuilder builder(std::move(name), current_frame_, scene_.resource());
            std::forward<Fn>(fn)(builder);

            Layer l = builder.build();
            l.kind = LayerKind::Null;
            if (l.active_at(current_frame_)) {
                scene_.add_layer(std::move(l));
            }
            return *this;
        }

        // Fluent API for transformations (root level)
        SceneBuilder &at(Vec3 pos);
        SceneBuilder &rotate(Vec3 euler_deg);
        SceneBuilder &scale(Vec3 s);
        SceneBuilder &anchor(Vec3 a);
        SceneBuilder &opacity(f32 a);
        SceneBuilder &with_shadow(DropShadow shadow);
        SceneBuilder &with_glow(Glow glow);

        [[nodiscard]] Scene build();
        [[nodiscard]] const Camera2_5D &camera_2_5d() const;

        [[nodiscard]] std::pmr::memory_resource *resource() const {
            return scene_.resource();
        }
        [[nodiscard]] Frame frame() const {
            return current_frame_;
        }

        friend class CameraApi;

      private:
        void set_camera(Camera2_5D camera) {
            scene_.set_camera_2_5d(std::move(camera));
        }

        template <typename Fn>
        void edit_camera(Fn &&fn) {
            auto cam = scene_.camera_2_5d();
            std::forward<Fn>(fn)(cam);
            scene_.set_camera_2_5d(cam);
        }

        Scene scene_;
        Frame current_frame_;
        FrameContext m_ctx{};
        i32 m_width{1920};
        i32 m_height{1080};
    };

} // namespace chronon3d
