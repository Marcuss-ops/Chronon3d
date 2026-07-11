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
#include <chronon3d/scene/builders/node_handle.hpp>
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

class FontEngine;      // forward decl — per-frame engine cascade
class SequenceBuilder;  // forward decl — Sequence V2 facade (sequence_builder.hpp)

// ═══════════════════════════════════════════════════════════════════════════
// SceneBuilder — public declarations only
// ═══════════════════════════════════════════════════════════════════════════
//
// This header is intentionally THIN. All definitions for non-template
// member functions live in:
//
//     detail/scene_builder_inline.inl        (Phase-3.3 + Refactor 6a)
//
// All definitions for templated member functions live in:
//
//     detail/scene_builder_layers.inl     (layer / screen_layer /
//                                          adjustment_layer /
//                                          precomp_layer /
//                                          video_layer / null_layer)
//     detail/scene_builder_sequences.inl  (sequence)
//     detail/scene_builder_camera.inl     (camera_rig + edit_camera)
//
// These are bottom-included at the bottom of this header (mirrors the
// existing Phase-3.3 split). Bodies in .inl files marked `inline` for
// non-template functions so ODR is satisfied across TUs. Templates are
// implicitly inline by definition.
//
// No new public classes / symbols are introduced by this split — public
// API surface is byte-identical to pre-Refactor-6.

class SceneBuilder {
  public:
    explicit SceneBuilder(std::pmr::memory_resource* res = std::pmr::get_default_resource(),
                          registry::ShapeRegistry* shape_registry = nullptr);

    explicit SceneBuilder(i32 width, i32 height,
                          std::pmr::memory_resource* res = std::pmr::get_default_resource(),
                          registry::ShapeRegistry* shape_registry = nullptr);

    // Convenience constructor for compositions — preserves sub-frame time.
    explicit SceneBuilder(const FrameContext& ctx,
                          registry::ShapeRegistry* shape_registry = nullptr);

    /// Bind a FontEngine to cascade into every LayerBuilder created via layer() calls.
    SceneBuilder& font_engine(FontEngine* engine);
    [[nodiscard]] FontEngine* font_engine() const;

    [[nodiscard]] CameraApi camera();

    /// Set camera from an AnimatedCamera2_5D, evaluated at the current time.
    SceneBuilder& animated_camera(const AnimatedCamera2_5D& cam);

    SceneBuilder& ambient_light(Color color = Color{1, 1, 1, 1}, f32 intensity = 0.2f);
    SceneBuilder& directional_light(Vec3 direction, Color color = Color{1, 1, 1, 1},
                                    f32 intensity = 1.0f);

    /// Apply a DepthGrade configuration to the scene.
    SceneBuilder& apply_depth_grade(const rendering::DepthGrade& grade);

    /// Apply a LightingRig preset, configuring the scene's light context,
    /// rim light, and shadow settings in one call.
    SceneBuilder& apply_lighting_rig(const rendering::LightingRig& rig);
    SceneBuilder& rect(std::string name, RectParams p);
    SceneBuilder& rounded_rect(std::string name, RoundedRectParams p);
    SceneBuilder& circle(std::string name, CircleParams p);
    SceneBuilder& line(std::string name, LineParams p);
    SceneBuilder& path(std::string name, PathParams p);
    SceneBuilder& image(std::string name, ImageParams p);
    SceneBuilder& grid_background(std::string name, GridBackgroundParams p);
    SceneBuilder& shape(std::string_view id, std::string name, registry::ShapeParams params);

    struct SequenceSpec {
        Frame from{0};
        Frame duration{0};
        Frame trim_before{0};  // Sequence V2: local frame offset
    };

    // ── DECL-only templates (Phase-3.3 split; bodies in .inl) ────────
    template <typename Fn>
    SceneBuilder& sequence(const std::string& name, SequenceSpec spec, Fn&& fn);

    // Standard Layers
    template <typename Fn>
    SceneBuilder& layer(std::string name, Fn&& fn);
    template <typename Fn>
    SceneBuilder& screen_layer(std::string name, Fn&& fn);

    // Adjustment layer: applies its effect stack to everything rendered before it.
    // The lambda receives a LayerBuilder but any visuals added are ignored.
    template <typename Fn>
    SceneBuilder& adjustment_layer(std::string name, Fn&& fn);

    template <typename Fn>
    SceneBuilder& precomp_layer(std::string name, std::string comp_name, Fn&& fn);

    template <typename Fn>
    SceneBuilder& video_layer(std::string name, video::VideoSource source, Fn&& fn);
    template <typename Fn>
    SceneBuilder& video_layer(std::string name, std::string path, Fn&& fn);

    template <typename Fn>
    SceneBuilder& null_layer(std::string name, Fn&& fn);

    // ── A4: [[deprecated]] — use last_node_handle() for explicit node access ──
    [[deprecated("Use last_node_handle().position(pos) for explicit node access")]]
    SceneBuilder& at(Vec3 pos);
    [[deprecated("Use last_node_handle().rotate(euler_deg) for explicit node access")]]
    SceneBuilder& rotate(Vec3 euler_deg);
    [[deprecated("Use last_node_handle().scale(s) for explicit node access")]]
    SceneBuilder& scale(Vec3 s);
    [[deprecated("Use last_node_handle().anchor(a) for explicit node access")]]
    SceneBuilder& anchor(Vec3 a);
    [[deprecated("Use last_node_handle().opacity(v) for explicit node access")]]
    SceneBuilder& opacity(f32 a);
    [[deprecated("Use last_node_handle().with_shadow(s) for explicit node access")]]
    SceneBuilder& with_shadow(DropShadow shadow);
    [[deprecated("Use last_node_handle().with_glow(g) for explicit node access")]]
    SceneBuilder& with_glow(Glow glow);

    // ── A4 — Explicit node handle for the last pushed node ────────────
    [[nodiscard]] NodeHandle last_node_handle();

    [[nodiscard]] Scene build();
    [[nodiscard]] const Camera2_5D& camera_2_5d() const;

    // DECL-only template (body in detail/scene_builder_camera.inl)
    template <typename Fn>
    SceneBuilder& camera_rig(std::string name, Fn&& fn);

    /// Stagger all layers in the scene by their spatial order.
    SceneBuilder& stagger(const StaggerConfig& config, StaggerOrder order = StaggerOrder::LeftToRight);

    /// Stagger only the named layers.
    SceneBuilder& stagger(const std::vector<std::string>& names, const StaggerConfig& config, StaggerOrder order = StaggerOrder::LeftToRight);

    [[nodiscard]] std::pmr::memory_resource* resource() const;
    [[nodiscard]] Frame frame() const;

    /// Access the full sub-frame time for animation evaluation.
    [[nodiscard]] SampleTime sample_time() const;

    /// C2 — canonical sequence compilation entry point.
    /// Used by both SceneBuilder::sequence() and SequenceBuilder::sequence().
    /// Extracted to eliminate code duplication and fix nested-manifest
    /// divergence between the two call sites.
    template <typename Fn>
    void compile_sequence(
        Frame cf,
        const FrameContext& parent_ctx,
        SequenceSpec spec,
        Fn&& fn,
        Scene& target_scene,
        registry::ShapeRegistry* shape_reg);

    friend class CameraApi;
    friend class SequenceBuilder;  // Sequence V2: access scene_ for nested merge

  private:
    void set_camera(Camera2_5D camera);

    template <typename Fn>
    void edit_camera(Fn&& fn);

    [[nodiscard]] Frame current_integer_frame() const;

    /// Always merge the layer's asset manifest into the scene, then
    /// conditionally add the layer to the scene's renderable list.
    /// This ensures the manifest is complete even for inactive layers
    /// (needed by AssetPreflightResolver FullComposition mode).
    void commit_layer(Layer layer) {
        scene_.asset_manifest().merge(layer.asset_manifest);
        if (layer.active_at(current_integer_frame())) {
            scene_.add_layer(std::move(layer));
        }
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

// ── Phase-3.3 + Refactor 6a bottom includes ────────────────────────────────
// Member function bodies are split across these .inl files (no new public
// types, no API surface change). All bodies are reachable from any TU that
// includes scene_builder.hpp:
//
//   * scene_builder_inline.inl       — non-template member bodies
//                                       (Refactor 6a: was previously
//                                       in-class inline; moved for header
//                                       thinness)
//   * scene_builder_layers.inl       — templated layer_*() bodies
//                                       (Phase-3.3)
//   * scene_builder_sequences.inl    — templated sequence() body
//                                       (Phase-3.3)
//   * scene_builder_camera.inl       — templated camera_rig() body + the
//                                       edit_camera() friend-of-CameraApi
//                                       helper                          (Phase-3.3)
//
// Non-template bodies are marked `inline` in their .inl so ODR is satisfied
// when this header is included in multiple TUs. Templates are implicitly
// inline by definition.
#include "chronon3d/scene/builders/detail/scene_builder_inline.inl"
#include "chronon3d/scene/builders/detail/scene_builder_layers.inl"
#include <chronon3d/scene/builders/sequence_builder.hpp>
#include "chronon3d/scene/builders/detail/scene_builder_camera.inl"
