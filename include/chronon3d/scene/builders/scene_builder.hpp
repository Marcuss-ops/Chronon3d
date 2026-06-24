#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/model/shape/transform_3d.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/camera_api.hpp>
#include <chronon3d/scene/camera/camera_rig_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/null_builder.hpp>
#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
#include <chronon3d/scene/model/core/hierarchy_resolver.hpp>  // ResolvedSceneTransforms
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/rendering/lighting_rig.hpp>
#include <chronon3d/animation/effects/stagger.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <type_traits>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>


namespace chronon3d {

    class FontEngine;

    class SceneBuilder {
      public:
        explicit SceneBuilder(std::pmr::memory_resource *res = std::pmr::get_default_resource(),
                              registry::ShapeRegistry* shape_registry = nullptr)
            : scene_(res), current_time_(SampleTime::from_frame_int(0, FrameRate{30, 1})) {
            m_ctx.resource = res;
            m_ctx.width = m_width;
            m_ctx.height = m_height;
            if (shape_registry) {
                m_shape_registry = shape_registry;
            } else {
                m_own_shape_registry.emplace(registry::make_default_shape_registry());
                m_shape_registry = &*m_own_shape_registry;
            }
        }

        explicit SceneBuilder(i32 width, i32 height, std::pmr::memory_resource *res = std::pmr::get_default_resource(),
                              registry::ShapeRegistry* shape_registry = nullptr)
            : scene_(res), current_time_(SampleTime::from_frame_int(0, FrameRate{30, 1})), m_width(width), m_height(height) {
            m_ctx.resource = res;
            m_ctx.width = width;
            m_ctx.height = height;
            if (shape_registry) {
                m_shape_registry = shape_registry;
            } else {
                m_own_shape_registry.emplace(registry::make_default_shape_registry());
                m_shape_registry = &*m_own_shape_registry;
            }
        }

        // Convenience constructor for compositions — preserves sub-frame time.
        explicit SceneBuilder(const FrameContext &ctx,
                              registry::ShapeRegistry* shape_registry = nullptr)
            : scene_(ctx.resource),
              current_time_(SampleTime::from_frame(
                  static_cast<double>(ctx.frame) + static_cast<double>(ctx.frame_time),
                  ctx.frame_rate)),
              m_ctx(ctx), m_width(ctx.width), m_height(ctx.height) {
            if (shape_registry) {
                m_shape_registry = shape_registry;
            } else {
                m_own_shape_registry.emplace(registry::make_default_shape_registry());
                m_shape_registry = &*m_own_shape_registry;
            }
        }

        [[nodiscard]] CameraApi camera() { return CameraApi(*this); }

        /// Set camera from an AnimatedCamera2_5D, evaluated at the current time.
        SceneBuilder& animated_camera(const AnimatedCamera2_5D& cam) {
            set_camera(cam.evaluate(current_time_));
            return *this;
        }

        SceneBuilder &ambient_light(Color color = Color{1, 1, 1, 1}, f32 intensity = 0.2f);

        SceneBuilder &directional_light(Vec3 direction, Color color = Color{1, 1, 1, 1},
                                        f32 intensity = 1.0f);

        /// Apply a DepthGrade configuration to the scene.
        SceneBuilder& apply_depth_grade(const rendering::DepthGrade& grade) {
            scene_.set_depth_grade(grade);
            return *this;
        }

        /// Apply a LightingRig preset, configuring the scene's light context,
        /// rim light, and shadow settings in one call.
        SceneBuilder &apply_lighting_rig(const rendering::LightingRig& rig);

        SceneBuilder &rect(std::string name, RectParams p);
        SceneBuilder &rounded_rect(std::string name, RoundedRectParams p);
        SceneBuilder &circle(std::string name, CircleParams p);
        SceneBuilder &line(std::string name, LineParams p);
        SceneBuilder &path(std::string name, PathParams p);
        SceneBuilder &image(std::string name, ImageParams p);
        SceneBuilder &grid_background(std::string name, GridBackgroundParams p);
        SceneBuilder &shape(std::string_view id, std::string name, registry::ShapeParams params);

        struct SequenceSpec {
            Frame from{0};
            Frame duration{0};
        };

        template <typename Fn>
        SceneBuilder &sequence(const std::string & /*name*/, SequenceSpec spec, Fn &&fn) {
            const Frame cf = current_integer_frame();
            bool active = cf >= spec.from && cf < spec.from + spec.duration;
            if (!active) {
                return *this;
            }

            FrameContext local_ctx = m_ctx;
            local_ctx.frame = Frame{cf - spec.from};
            local_ctx.local_frame = local_ctx.frame;
            local_ctx.duration = spec.duration;
            // Preserve sub-frame fraction from the parent time.
            local_ctx.frame_time = m_ctx.frame_time;

            SceneBuilder sub_builder(local_ctx, m_shape_registry);
            if (m_font_engine) sub_builder.font_engine(m_font_engine);
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
            LayerBuilder builder(std::move(name), current_time_, scene_.resource(), m_shape_registry);
            builder.screen_dimensions(static_cast<f32>(m_width), static_cast<f32>(m_height));
            if (m_font_engine) builder.font_engine(m_font_engine);
            std::forward<Fn>(fn)(builder);

            Layer l = builder.build();
            if (l.active_at(current_integer_frame())) {
                scene_.add_layer(std::move(l));
            }
            return *this;
        }

        template <typename Fn>
        SceneBuilder &screen_layer(std::string name, Fn &&fn) {
            LayerBuilder builder(std::move(name), current_time_, scene_.resource(), m_shape_registry);
            builder.screen_dimensions(static_cast<f32>(m_width), static_cast<f32>(m_height));
            if (m_font_engine) builder.font_engine(m_font_engine);
            std::forward<Fn>(fn)(builder);

            Layer l = builder.build();
            if (l.active_at(current_integer_frame())) {
                scene_.add_layer(std::move(l));
            }
            return *this;
        }

        // Adjustment layer: applies its effect stack to everything rendered before it.
        // The lambda receives a LayerBuilder but any visuals added are ignored.
        template <typename Fn>
        SceneBuilder &adjustment_layer(std::string name, Fn &&fn) {
            LayerBuilder builder(std::move(name), current_time_, scene_.resource(), m_shape_registry);
            builder.screen_dimensions(static_cast<f32>(m_width), static_cast<f32>(m_height));
            if (m_font_engine) builder.font_engine(m_font_engine);
            std::forward<Fn>(fn)(builder);

            Layer l = builder.build();
            l.kind = LayerKind::Adjustment;
            if (l.active_at(current_integer_frame())) {
                scene_.add_layer(std::move(l));
            }
            return *this;
        }

        template <typename Fn>
        SceneBuilder &precomp_layer(std::string name, std::string comp_name, Fn &&fn) {
            LayerBuilder builder(std::move(name), current_time_, scene_.resource(), m_shape_registry);
            if (m_font_engine) builder.font_engine(m_font_engine);
            std::forward<Fn>(fn)(builder);

            Layer l = builder.build();
            l.kind = LayerKind::Precomp;
            l.precomp_composition_name = std::pmr::string{comp_name, scene_.resource()};
            if (l.active_at(current_integer_frame())) {
                scene_.add_layer(std::move(l));
            }
            return *this;
        }

        template <typename Fn>
        SceneBuilder &video_layer(std::string name, video::VideoSource source, Fn &&fn) {
            LayerBuilder builder(std::move(name), current_time_, scene_.resource(), m_shape_registry);
            if (m_font_engine) builder.font_engine(m_font_engine);
            std::forward<Fn>(fn)(builder);

            Layer l = builder.build();
            l.kind = LayerKind::Video;
            l.video_source = std::make_unique<video::VideoSource>(std::move(source));
            if (l.active_at(current_integer_frame())) {
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
            if constexpr (std::is_invocable_v<Fn, NullBuilder&>) {
                NullParams params;
                params.name = std::move(name);
                NullBuilder builder(params);
                std::forward<Fn>(fn)(builder);

                Layer l(scene_.resource());
                l.name = std::pmr::string(params.name, scene_.resource());
                l.kind = LayerKind::Null;
                l.transform.position = params.transform.position;
                l.transform.rotation = glm::quat(glm::radians(params.transform.rotation));
                l.transform.scale = params.transform.scale;
                l.transform.anchor = params.transform.anchor;
                l.parent_name = std::pmr::string(params.transform.parent_name, scene_.resource());
                l.visible = params.visible_in_diagnostics;

                if (l.active_at(current_integer_frame())) {
                    scene_.add_layer(std::move(l));
                }
                return *this;
            } else {
                LayerBuilder builder(std::move(name), current_time_, scene_.resource(), m_shape_registry);
                if (m_font_engine) builder.font_engine(m_font_engine);
                std::forward<Fn>(fn)(builder);

                Layer l = builder.build();
                l.kind = LayerKind::Null;
                if (l.active_at(current_integer_frame())) {
                    scene_.add_layer(std::move(l));
                }
                return *this;
            }
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

        template <typename Fn>
        SceneBuilder &camera_rig(std::string name, Fn &&fn) {
            CameraRig rig;
            rig.name = std::move(name);
            CameraRigBuilder builder(rig);
            std::forward<Fn>(fn)(builder);

            // Resolve parent/target nulls through the canonical HierarchyResolver
            // pipeline.  ResolvedSceneTransforms is the value type consumed by
            // CameraRig::evaluate(); no legacy bridge required.
            std::vector<SceneTransformInput> inputs;
            inputs.reserve(scene_.layers().size());
            for (const auto& layer : scene_.layers()) {
                Transform3D t3d;
                t3d.position = layer.transform.position;
                t3d.rotation = glm::degrees(glm::eulerAngles(layer.transform.rotation));
                t3d.scale = layer.transform.scale;
                t3d.anchor = layer.transform.anchor;
                t3d.parent_name = std::string(layer.parent_name);
                t3d.inherits_position = true;
                t3d.inherits_rotation = true;
                t3d.inherits_scale = true;
                inputs.push_back(SceneTransformInput{
                    std::string(layer.name),
                    t3d,
                    layer.kind != LayerKind::Null,
                    false
                });
            }
            ResolvedSceneTransforms resolved = resolve_scene_transforms(inputs);

            Camera2_5D camera_baked = rig.evaluate(current_time_, &resolved);
            scene_.set_camera_2_5d(camera_baked);
            return *this;
        }

        /// Stagger all layers in the scene by their spatial order.
        SceneBuilder& stagger(const StaggerConfig& config, StaggerOrder order = StaggerOrder::LeftToRight);

        /// Stagger only the named layers.
        SceneBuilder& stagger(const std::vector<std::string>& names, const StaggerConfig& config, StaggerOrder order = StaggerOrder::LeftToRight);

        [[nodiscard]] std::pmr::memory_resource *resource() const {
            return scene_.resource();
        }
        [[nodiscard]] Frame frame() const {
            return current_integer_frame();
        }

        /// Access the full sub-frame time for animation evaluation.
        [[nodiscard]] SampleTime sample_time() const {
            return current_time_;
        }

        /// WP-8 PR 8.0 — per-SceneBuilder FontEngine default. Forwarded to
        /// every LayerBuilder created by layer() / screen_layer() / etc.
        SceneBuilder& font_engine(FontEngine* engine) { m_font_engine = engine; return *this; }
        [[nodiscard]] FontEngine* font_engine() const { return m_font_engine; }

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

        [[nodiscard]] Frame current_integer_frame() const {
            return Frame{static_cast<i64>(std::floor(current_time_.frame))};
        }

        Scene scene_;
        SampleTime current_time_{SampleTime::from_frame_int(0, FrameRate{30, 1})};
        FrameContext m_ctx{};
        i32 m_width{1920};
        i32 m_height{1080};
        registry::ShapeRegistry* m_shape_registry{nullptr};
        std::optional<registry::ShapeRegistry> m_own_shape_registry;
        FontEngine* m_font_engine{nullptr};
    };

} // namespace chronon3d
