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

    class FontEngine;  // forward decl — per-frame engine cascade

    // ── Phase-3.3 mechanical split note ────────────────────────────────────
    // The template member function bodies that were previously inline
    // inside this class have been moved verbatim into:
    //   * detail/scene_builder_layers.inl   (layer / screen_layer /
    //                                         adjustment_layer /
    //                                         precomp_layer /
    //                                         video_layer /
    //                                         null_layer)
    //   * detail/scene_builder_sequences.inl (sequence)
    //   * detail/scene_builder_camera.inl    (camera_rig)
    // This header now DECLARES them inline-template-style and reaches
    // for the bodies via bottom `#include "detail/...inl"`.  Behaviour
    // is preserved BIT-IDENTICALLY.  Anonymous sequence decals
    // (SequenceSpec, etc.) and other class-body-typed data still live
    // inline in this scope.

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
            // codex/agent2-font-bind-fixes — auto-forward the per-frame
            // FontEngine from the FrameContext to the builder's
            // m_font_engine slot.  Explicit override guarantee: a
            // composition lambda that calls `s.font_engine(X)` later
            // REPLACES this auto-bind with its own pointer (the setter
            // assignment is unconditional), so per-composition overrides
            // continue to work without modification.  Composition
            // lambdas that do nothing with `ctx.font_engine`
            // therefore get the engine bound transparently — fixing
            // the WP-8 PR 8.0 strict-binding "no FontEngine available"
            // failure path that `materialize_text_run_shape` reports
            // when the SceneBuilder has no engine.  If
            // `ctx.font_engine` is nullptr, this branch is a no-op and
            // the legacy path (callers set `m_font_engine`
            // themselves) is preserved.
            if (ctx.font_engine) {
                m_font_engine = ctx.font_engine;
            }
        }

        /// Bind a FontEngine to cascade into every LayerBuilder created via layer() calls.
        SceneBuilder& font_engine(FontEngine* engine) {
            m_font_engine = engine;
            return *this;
        }

        [[nodiscard]] FontEngine* font_engine() const { return m_font_engine; }

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

        // ── DECL-only templates (Phase-3.3 split) ─────────────────────────────
        // Bodies live in detail/scene_builder_*.inl (bottom-included).
        template <typename Fn>
        SceneBuilder &sequence(const std::string &name, SequenceSpec spec, Fn &&fn);

        // Standard Layers
        template <typename Fn>
        SceneBuilder &layer(std::string name, Fn &&fn);
        template <typename Fn>
        SceneBuilder &screen_layer(std::string name, Fn &&fn);

        // Adjustment layer: applies its effect stack to everything rendered before it.
        // The lambda receives a LayerBuilder but any visuals added are ignored.
        template <typename Fn>
        SceneBuilder &adjustment_layer(std::string name, Fn &&fn);

        template <typename Fn>
        SceneBuilder &precomp_layer(std::string name, std::string comp_name, Fn &&fn);

        template <typename Fn>
        SceneBuilder &video_layer(std::string name, video::VideoSource source, Fn &&fn);
        template <typename Fn>
        SceneBuilder &video_layer(std::string name, std::string path, Fn &&fn);

        template <typename Fn>
        SceneBuilder &null_layer(std::string name, Fn &&fn);

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

        // DECL-only template (body in detail/scene_builder_camera.inl)
        template <typename Fn>
        SceneBuilder &camera_rig(std::string name, Fn &&fn);

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
        FontEngine* m_font_engine{nullptr};  // cascaded to LayerBuilder via layer() calls
    };

} // namespace chronon3d

// ── Phase-3.3 bottom includes ──────────────────────────────────────────
// Templated member function bodies live in the .inl files below.
// Implicitly inline because the underlying templates are defined
// there (no ODR risk even with multiple TU inclusions).
#include "chronon3d/scene/builders/detail/scene_builder_layers.inl"
#include "chronon3d/scene/builders/detail/scene_builder_sequences.inl"
#include "chronon3d/scene/builders/detail/scene_builder_camera.inl"
