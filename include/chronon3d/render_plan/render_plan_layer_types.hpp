#pragma once

#include <chronon3d/render_plan/render_plan_types.hpp>
#include <chronon3d/render_plan/render_plan_text.hpp>

namespace chronon3d::render_plan {
enum class FitMode : std::uint8_t { Cover, Contain, Stretch, None };
struct ImageCropPlan {
    bool enabled{false};
    std::array<float, 2> origin{0.0f, 0.0f};
    std::array<float, 2> size{1.0f, 1.0f};
    bool flip_x{false};
};
enum class OutputFormat : std::uint8_t { Png, Mp4, Mkv, WebM, PngSequence, Gif };
enum class VideoCodec : std::uint8_t { Auto, H264, H265, VP9, AV1 };
enum class RateControlMode : std::uint8_t { Crf, ConstantQp, Bitrate };

struct CanvasSpec {
    int width{0};
    int height{0};
    FrameRate fps{30, 1};
    Frame duration{0};
};

struct AnimationPlan {
    std::vector<AnimationTrackPlan> tracks;
};

/// Shutter duration is expressed as a fraction of one frame: seconds are
/// (shutter_angle / 360) * (fps_den / fps_num). `samples` is the number of
/// temporal renderer samples distributed over that interval. The camera
/// provider is queried at each resulting absolute presentation time.
struct MotionBlurPlan {
    float shutter_angle{180.0f};
    std::uint32_t samples{12};
};

/// Light kinds. `Radial` is the historical screen-space glow (default, output
/// unchanged). `Point`, `Spot` and `Directional` are real light sources; their
/// shading and cast shadows are implemented by the lighting slices of
/// TICKET-LIGHTS-SHADOWS-PARTICLES. The compiler rejects unsupported light
/// behavior explicitly rather than drawing the glow in its place.
enum class LightKindPlan : std::uint8_t { Radial, Point, Spot, Directional };

struct LightLayerPlan {
    float radius{256.0f};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    float intensity{1.0f};
    LightKindPlan kind{LightKindPlan::Radial};
    /// Direction the light travels (Spot, Directional). Need not be unit length;
    /// the decoder rejects the zero vector.
    std::array<float, 3> direction{0.0f, 0.0f, -1.0f};
    /// Distance at which Point/Spot contribution reaches zero. 0 = unbounded.
    float range{0.0f};
    /// Falloff exponent: contribution ~ 1 / (1 + (d / radius)^decay).
    float decay{1.0f};
    /// Spot half-angle in radians, (0, pi].
    float angle{0.78539816339744830962f};
    /// Spot edge softness in [0, 1].
    float penumbra{0.0f};
    bool cast_shadow{false};
    /// Shadow blur radius in pixels, >= 0.
    float shadow_radius{1.0f};
    /// Depth bias that prevents self-shadowing, >= 0.
    float shadow_bias{0.0f};
};

enum class TrackMatteTypePlan : std::uint8_t {
    Alpha, AlphaInverted, Luma, LumaInverted
};

struct TrackMattePlan {
    std::string source;
    TrackMatteTypePlan type{TrackMatteTypePlan::Alpha};
};

struct TimeRemapPlan {
    std::optional<float> speed;
    bool reverse{false};
    std::optional<Frame> freeze_frame;
    std::optional<AnimationTrackPlan> animation;
};

// Camera values are expressed in world units and degrees. rotation_deg uses
// the canonical [tilt_x, pan_y, roll_z] order consumed by Camera2_5D.
//
// Depth-of-field contract: dof_focus_distance is the camera-space distance
// along the optical axis from the camera position to the focus plane (S1 in
// the thin-lens model, scene units, strictly positive). It is NOT a world-Z
// coordinate: the legacy world-Z focus reference (DepthOfFieldSettings::
// focus_z) is derived from it with the camera pose at compile/eval time.
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
struct CameraPlan {
    std::string type{"perspective"};
    std::array<float, 3> position{0.0f, 0.0f, -1000.0f};
    std::array<float, 3> rotation_deg{0.0f, 0.0f, 0.0f};
    float fov_deg{50.0f};
    float near{1.0f};
    float far{10000.0f};
    float zoom{1.0f};
    // Physical lens authoring (0 = not authored). When focal_length_mm > 0
    // and an f-stop is authored, the camera compiles to the physical thin-
    // lens model; otherwise the legacy blur-per-unit model is used.
    float focal_length_mm{0.0f};
    float sensor_width_mm{36.0f};
    // Normalized viewport shift: +X right, +Y down, each unit is one viewport.
    std::array<float, 2> lens_shift{0.0f, 0.0f};
    float f_stop{0.0f};
    int iris_blades{0};
    bool dof_enabled{false};
    float dof_focus_distance{1000.0f};
    float dof_aperture{0.015f};
    float dof_max_blur{24.0f};
    // Physical DoF overrides (0 = not authored, inherit the camera-level
    // lens value). Shaped-bokeh requests fail closed at render time (see
    // reject_unsupported_dof_shape): no kernel implements them yet.
    float dof_f_stop{0.0f};
    int dof_iris_blades{0};
    float dof_highlight_gain{0.0f};
};

// A complete, already-resolved renderer camera at an absolute presentation
// frame. Seconds are derived from this frame and the enclosing CanvasPlan's
// fps_num/fps_den. For sub-frame shutter samples, use MotionBlurPlan timing;
// offline plans interpolate adjacent complete samples while a runtime CM3D
// provider can evaluate the exact requested time. Offline plans store these
// values so C3D never reconstructs camera intent from independently animated
// properties.
struct CameraSamplePlan {
    double frame{0.0};
    CameraPlan camera;
};

struct TransitionPlan {
    std::string id;
    std::string kind{"none"};
    Frame duration_frames{1};
    Frame delay_frames{0};
    std::string easing{"linear"};
    std::string direction;
    float distance{1.0f};
    float zoom_amount{0.2f};
    float feather{0.1f};
    std::array<float, 2> center{0.5f, 0.5f};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    float seed{1.2f};
    std::array<float, 4> inner_color{1.0f, 0.45f, 0.0f, 1.0f};
    std::array<float, 4> mid_color{1.0f, 0.45f, 0.0f, 1.0f};
    std::array<float, 4> outer_color{1.0f, 1.0f, 1.0f, 1.0f};
    float speed{1.35f};
    float remotion_direction{3.0f};
    float angle{0.0f};
    float dip_px{0.0f};
};

}  // namespace chronon3d::render_plan
