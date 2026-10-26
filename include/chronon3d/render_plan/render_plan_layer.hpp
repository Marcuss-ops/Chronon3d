#pragma once

#include <chronon3d/render_plan/render_plan_effects.hpp>
#include <chronon3d/render_plan/render_plan_layer_types.hpp>

namespace chronon3d::render_plan {
struct LayerPlan {
    struct StaggerPlan {
        std::string group_id;
        std::uint32_t per_item_frames{0};
        std::string order{"forward"};
        std::uint32_t seed{0};
        std::string track_property{"opacity"};
        std::string easing;
    };
    struct FollowPathDriverPlan {
        std::string path_id;
        std::vector<ShapePathCommandPlan> path_commands;
        AnimationTrackPlan progress;
        bool auto_orient{false};
        float offset_deg{0.0f};
        bool clamp_end{true};
    };
    std::optional<FollowPathDriverPlan> follow_path;
    struct FlowLayoutPlan { std::string group_id; std::string axis{"row"}; std::string align{"start"}; std::string justify{"start"}; float gap{0}; bool wrap{false}; std::array<float,2> cell_size{0,0}; std::optional<StaggerPlan> stagger; bool intrinsic_size{false}; };
    struct GridLayoutPlan { std::string group_id; std::string fit{"auto"}; int columns{1}; int rows{1}; std::array<float,2> gap{0,0}; std::array<float,2> cell_size{0,0}; std::optional<StaggerPlan> stagger; bool intrinsic_size{false}; };
    std::optional<FlowLayoutPlan> flow_layout;
    std::optional<GridLayoutPlan> grid_layout;
    std::optional<StaggerPlan> stagger_group;
    std::string id;
    LayerType type{LayerType::Color};
    std::optional<std::string> parent;
    std::optional<TrackMattePlan> track_matte;
    std::optional<TimeRemapPlan> time_remap;
    std::string asset;
    // Transfer encoding for byte-backed image layers; tagged EXR and video
    // sources use their own metadata and remain handled by their decoders.
    std::string input_color_space{"srgb"};
    // Optional named OCIO source space. Requires the plan-level OCIO contract.
    std::string ocio_input_space;
    std::string source;
    std::string text;
    std::vector<TextSpanOverride> text_spans;
    std::vector<TextGlyphPaintRange> svg_glyph_paint_ranges;
    std::optional<TextCounterPlan> text_counter;

    // Normalized concrete font asset path copied from style.font by decoder.
    // Kept as an execution field so asset preflight remains simple.
    std::string font;
    std::optional<LayerStylePlan> style;
    std::optional<MotionBlurPlan> motion_blur;
    std::optional<ShapeContentPlan> shape;
    std::optional<InlineMeshPlan> inline_mesh;
    std::optional<ParticleEmitterParams> particle_emitter;
    std::optional<LightLayerPlan> light;
    struct PathMaskPlan {
        std::string type{"path"};
        std::string mode{"add"};
        // Text mask source layer. The layer must be a prepared text layer;
        // graph compilation resolves its same-frame rendered alpha.
        std::string text_source;
        std::vector<ShapePathCommandPlan> path;
        std::vector<ShapePathCommandPlan> morph_target;
        std::optional<Field2DPlan> field;
        std::optional<AnimationTrackPlan> morph_animation;
        std::array<float, 2> position{0.0f, 0.0f};
        std::array<float, 2> size{0.0f, 0.0f};
        float radius{0.0f};
        float feather{0.0f};
        float expansion{0.0f};
        float opacity{1.0f};
        std::optional<AnimationTrackPlan> feather_track;
        std::optional<AnimationTrackPlan> expansion_track;
        std::optional<AnimationTrackPlan> opacity_track;
        bool inverted{false};
    };
    std::optional<PathMaskPlan> path_mask;
    std::vector<PathMaskPlan> masks;
    using EffectKindPlan = ::chronon3d::render_plan::EffectKindPlan;
    using KeyingModePlan = ::chronon3d::render_plan::KeyingModePlan;
    using EffectPlan = ::chronon3d::render_plan::EffectPlan;
    using EffectParamTrackPlan = ::chronon3d::render_plan::EffectParamTrackPlan;
    std::vector<EffectParamTrackPlan> effect_param_tracks;
    std::vector<EffectPlan> effects;
    std::optional<BackdropFilterPlan> backdrop_filter;

    std::array<float, 2> size{0.0f, 0.0f};
    std::size_t size_dimensions{0};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::size_t position_dimensions{0};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    std::size_t scale_dimensions{0};
    // Euler angles in degrees, [x, y, z], for both static and tracked values.
    std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};
    std::size_t rotation_dimensions{0};
    float skew_angle_deg{0.0f};
    float skew_axis_deg{0.0f};
    // Spatial contract: ordinary layers stay in the flat canvas plane. Set
    // this flag to route a planar image/shape through the scene camera, making
    // its Z position and X/Y rotations meaningful (2.5D). Mesh layers are
    // camera-backed by their kind and do not need this flag. This switch does
    // not mean “arbitrary 3D geometry”; that is represented by Mesh.
    bool enable_3d{false};
    // Mesh shadow participation is opt-in in the wire contract. Absence keeps
    // the historical compiler behavior and therefore old fingerprints stable.
    std::optional<bool> casts_shadows;
    std::optional<bool> accepts_shadows;
    // Keep a layer pinned to output-canvas coordinates while a scene camera
    // is active. This is independent of enable_3d and is for screen overlays
    // such as backgrounds or HUD elements.
    bool screen_space{false};
    // Pixel insets [top, right, bottom, left] for title/action-safe layout.
    std::optional<std::array<float, 4>> safe_insets;
    std::optional<std::string> depth_role;
    float depth_offset{0.0f};
    // Rounded image corner radius, evaluated by the image compositor's
    // analytic/SDF-style mask. Zero keeps the regular image fast path.
    std::optional<float> radius;

    std::optional<Frame> start_frame;
    std::optional<Frame> duration_frames;
    std::optional<TransitionPlan> transition_in;
    std::optional<TransitionPlan> transition_out;
    std::optional<FitMode> fit;
    std::optional<ImageCropPlan> crop;
    std::optional<AnimationPlan> animation;
    std::optional<GradientAnimationPlan> fill_gradient_animation;
    std::vector<TextAnimatorPlan> text_animators;

    std::optional<chronon3d::BlendMode> blend_mode;
    std::optional<float> opacity;
    bool loop{false};
    bool auto_orient{false};
};

// Immutable producer-side preparation metadata. The JSON sidecar is decoded
// and validated at the CLI boundary, then carried into the canonical compiler
// so equal text identities can share one PreparedTextRun. It contains no
// backend handles and is intentionally not a second render plan.

}  // namespace chronon3d::render_plan
