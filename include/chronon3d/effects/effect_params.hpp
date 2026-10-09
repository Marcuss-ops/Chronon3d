#pragma once

// ── Effect parameter structs and variant type ──────────────────────────────
// Separated from effect_stack.hpp to break the circular dependency between
// EffectInstance (needs variant) and the param struct definitions.

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/graphics/field2d.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/backends/software/sampling/edge_mode.hpp>
#include <chronon3d/effects/luminance_response.hpp>
#include <variant>
#include <vector>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace chronon3d {

struct BlurParams          { f32   radius{0.0f}; };
struct TintParams          { Color color{0,0,0,0}; f32 amount{1.0f}; };
struct BrightnessParams    { f32   value{0.0f}; };
struct ContrastParams      { f32   value{1.0f}; };
struct DropShadowParams    { Vec2 offset{0,8}; Color color{0,0,0,0.35f}; f32 radius{12}; };

// One glow is one Gaussian halo. Radius, intensity and tint are the only
// author-facing controls; preview/final execution share the same rendering.
struct GlowParams {
    f32 radius{24.0f};
    f32 intensity{0.25f};
    Color color{1, 1, 1, 1};
};

struct BloomParams         { f32 threshold{0.80f}; f32 radius{24.0f}; f32 intensity{0.60f}; };

// ── Exposure (stops-based, HDR-safe) ───────────────────────────────
struct ExposureParams { f32 stops{0.0f}; };

// ── Levels (per-channel + master) ────────────────────────────────────────
struct LevelsChannel {
    f32 input_black{0.0f};
    f32 input_white{1.0f};
    f32 gamma{1.0f};
    f32 output_black{0.0f};
    f32 output_white{1.0f};
};

struct LevelsParams {
    LevelsChannel master{};
    LevelsChannel red{};
    LevelsChannel green{};
    LevelsChannel blue{};
};

// Relative linear-RGB white balance controls. These normalized artist controls
// are deliberately not exposed as Kelvin because the source illuminant and
// camera white point are not part of the current project input contract.
struct TemperatureTintParams {
    f32 temperature{0.0f}; // [-1,1], cool to warm
    f32 tint{0.0f};        // [-1,1], green to magenta
};

// Global lift/gamma/gain grade in Chronon's linear RGB working space.
struct LiftGammaGainParams {
    f32 lift{0.0f};  // additive offset [-1,1]
    f32 gamma{1.0f}; // signed power shaping [0.1,4]
    f32 gain{1.0f};  // linear multiplier [0,4]
};

// ── Adjustment-layer color correction params (AE-5) ─────────────────────
struct SaturationParams    { f32 value{1.0f}; };  // 1.0 = unchanged, 0 = greyscale
struct HueRotateParams     { f32 degrees{0.0f}; }; // 0 = unchanged, 180 = invert hue
struct InvertParams        { f32 amount{1.0f}; };  // 1.0 = full invert, 0 = no-op
struct VignetteParams      {
    f32 radius{0.5f};      // 0..1, fraction of frame diagonal
    f32 softness{0.5f};    // 0..1, edge softness
    f32 amount{1.0f};      // 0..1, darkness amount
    Color color{0,0,0,1};  // vignette color (usually black)
};

enum class WaveAxis {
    Horizontal,
    Vertical,
};

struct Fake3DWaveParams {
    f32 amplitude_px{18.0f};
    f32 frequency{2.2f};
    f32 speed{3.5f};
    f32 depth_px{35.0f};
    f32 phase{0.0f};
    i32 slices{24};
    WaveAxis axis{WaveAxis::Horizontal};
    f32 perspective{0.06f};
    f32 highlight{0.20f};
    f32 side_darkening{0.18f};
    bool shadow_enabled{true};
    Color shadow_color{1.0f, 0.05f, 0.05f, 0.75f};
    Vec2 shadow_offset{10.0f, 8.0f};
    f32 shadow_blur{0.0f};
    bool expand_bounds{true};
};

// ── Curves (A3) — per-channel + master curve via control points ──────────
struct CurvePoint {
    float x{0.0f};
    float y{0.0f};
};

struct CurvesParams {
    std::vector<CurvePoint> master;  // combined RGB curve
    std::vector<CurvePoint> red;     // per-channel curves (overrides master if non-empty)
    std::vector<CurvePoint> green;
    std::vector<CurvePoint> blue;
};

// ── Fill (Replace / Mix) ───────────────────────────────────────────────
enum class FillMode {
    Replace,
    Mix
};

struct FillParams {
    Color color{1, 1, 1, 1};
    float amount{1.0f};
    FillMode mode{FillMode::Replace};
};

// ── Noise (deterministic, thread-safe) ─────────────────────────────────
enum class NoiseColorMode {
    Monochrome,
    RGB
};

struct NoiseParams {
    float amount{0.0f};
    uint32_t seed{0};
    bool animated{false};
    NoiseColorMode color_mode{NoiseColorMode::Monochrome};
    float size{1.0f}; // grain cell size in pixels; 1 = independent pixel noise
    // Relative grain amplitude sampled across luminance, from shadows to
    // highlights. All ones preserves the historical uniform-noise result.
    float shadow_response{1.0f};
    float midtone_response{1.0f};
    float highlight_response{1.0f};
};

// Deterministic fractal value noise generated over the full frame. Evolution
// is a continuous sampling offset in lattice-space per second.
struct FractalNoiseParams {
    uint32_t seed{0};
    uint32_t octaves{4};
    float frequency{0.015f};
    float amplitude{1.0f};
    float evolution{0.0f};
    float contrast{1.0f};
    float brightness{0.0f};
    NoiseColorMode color_mode{NoiseColorMode::Monochrome};
    std::optional<graphics::Field2D> field;
};

struct CheckerboardParams {
    float size{64.0f};
    Color color_a{0.0f, 0.0f, 0.0f, 1.0f};
    Color color_b{1.0f, 1.0f, 1.0f, 1.0f};
    float feather{0.0f};
};

struct FourColorGradientParams {
    std::array<Color, 4> colors{
        Color{1.0f, 0.0f, 0.0f, 1.0f}, Color{0.0f, 1.0f, 0.0f, 1.0f},
        Color{0.0f, 0.0f, 1.0f, 1.0f}, Color{1.0f, 1.0f, 0.0f, 1.0f}};
    std::array<Vec2, 4> points{
        Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f}, Vec2{0.0f, 1.0f}, Vec2{1.0f, 1.0f}};
    float blend_smoothness{0.15f};
};

// A parsed 3D color lookup table. Entries use .cube's red-fastest ordering.
struct Lut3DParams {
    uint32_t dimension{0};
    std::shared_ptr<const std::vector<std::array<float, 3>>> values;
    std::array<float, 3> domain_min{0.0f, 0.0f, 0.0f};
    std::array<float, 3> domain_max{1.0f, 1.0f, 1.0f};
    float amount{1.0f};
};

struct EchoParams {
    uint32_t echoes{3};
    float decay{0.5f};
    uint32_t time_offset_frames{1};
};

enum class KeyingMode : std::uint8_t { Luma, Chroma };

struct KeyingParams {
    KeyingMode mode{KeyingMode::Chroma};
    Color key_color{0.0f, 1.0f, 0.0f, 1.0f};
    float threshold{0.20f};
    float softness{0.08f};
    float spill{0.75f};
    // Screen-matte refinement (Keylight-style clip + choke):
    // the matte value is remapped through [clip_black, clip_white] and then
    // eroded by `choke` before it reaches alpha.
    float clip_black{0.0f};  // matte at or below this is fully removed
    float clip_white{1.0f};  // matte at or above this is fully kept
    float choke{0.0f};       // edge erosion of the matte, [0, 1)
    // Garbage matte: keep-only rectangle in normalized layer coordinates
    // (x, y, width, height). Disabled when width or height is 0.
    float garbage_x{0.0f};
    float garbage_y{0.0f};
    float garbage_w{0.0f};
    float garbage_h{0.0f};
    float garbage_feather{0.0f}; // soft edge width in normalized units [0, 0.5]
};

struct ThresholdParams {
    float threshold{0.5f};
    float softness{0.0f};
};

struct MosaicParams {
    std::uint32_t block_size{8};
};

enum class ChromaticOffsetMode : std::uint8_t {
    Constant,
    Radial,
    Velocity
};

/// Inverse-sampled affine transform for one RGB component. Translation and
/// rotation are in screen pixels/degrees; scale is centered on the frame.
struct RgbChannelTransform {
    Vec2 translation{0.0f, 0.0f};
    Vec2 scale{1.0f, 1.0f};
    f32 rotation_degrees{0.0f};
    f32 opacity{1.0f};
};

struct ChromaticAberrationParams {
    float red_offset{2.0f};
    float blue_offset{-2.0f};
    ChromaticOffsetMode offset_mode{ChromaticOffsetMode::Constant};
    Vec2 velocity{0.0f, 0.0f};
    Vec2 center{0.5f, 0.5f};
    RgbChannelTransform red{};
    RgbChannelTransform green{};
    RgbChannelTransform blue{};
};

struct HalftoneParams {
    std::uint32_t size{8};
    float angle_degrees{0.0f};
    float amount{1.0f};
};

struct LightRaysParams {
    Vec2 origin{0.5f, 0.5f};
    float length{320.0f};
    float density{0.75f};
    Color color{1.0f, 0.86f, 0.55f, 1.0f};
    float decay{0.92f};
};

// ── Offset (pixel shift with edge modes) ────────────────────────────────
struct OffsetParams {
    Vec2 offset{0.0f, 0.0f};
    sampling::EdgeMode edge_mode{sampling::EdgeMode::Transparent};
    sampling::SampleFilter filter{sampling::SampleFilter::Bilinear};
};

// ── Directional Blur (A5) ───────────────────────────────────────────────
struct DirectionalBlurParams {
    float angle{0.0f};    // degrees, 0 = horizontal right
    float length{0.0f};    // full extent of blur in pixels
    int samples{0};        // 0 = auto (derived from length, ceil(length/2))
};

// ── Radial Blur (A6) ───────────────────────────────────────────────────
struct RadialBlurParams {
    Vec2 center{0.5f, 0.5f};  // normalized center [0, 1]
    float amount{0.0f};       // blur strength, 0 = identity
    int render_samples{8};    // tap count for final render
    int preview_samples{4};   // tap count for preview
};

// ── Stroke (A7) ───────────────────────────────────────────────────────
enum class StrokeMode {
    Outside,    // stroke only outside the source shape
    Inside,     // stroke only inside the source shape
    Center      // stroke centered on the source edge
};

struct StrokeParams {
    Color color{1.0f, 1.0f, 1.0f, 1.0f};  // stroke colour (straight RGBA)
    float width{0.0f};                      // stroke width in pixels
    float softness{0.0f};                   // edge feather in pixels
    StrokeMode mode{StrokeMode::Outside};
};

// ── Warp family (AE distortion parity) ─────────────────────────────────
// All four resample in place: every destination pixel samples the source
// through an inverse map. Centers, quads and grid points are in normalized
// layer space [0, 1]; radial radii are fractions of min(width, height).

// Corner Pin — maps the four corners of the source onto an arbitrary quad
// (screen replacement). The inverse bilinear solve is fail-closed: a
// destination pixel with no source solution renders transparent.
struct CornerPinParams {
    Vec2 top_left{0.0f, 0.0f};
    Vec2 top_right{1.0f, 0.0f};
    Vec2 bottom_right{1.0f, 1.0f};
    Vec2 bottom_left{0.0f, 1.0f};
    sampling::EdgeMode edge_mode{sampling::EdgeMode::Transparent};
};

// Twirl — rotation around `center`, linearly faded to zero at `radius`
// raised to `falloff`. The inverse map is the exact negative rotation at
// the same radius, so the warp is bijective where the falloff is monotonic.
struct TwirlParams {
    Vec2 center{0.5f, 0.5f};
    float angle_degrees{90.0f};
    float radius{0.5f};
    float falloff{1.0f};
    sampling::EdgeMode edge_mode{sampling::EdgeMode::Transparent};
};

// Bulge / Pinch — radial magnification around `center`. amount > 0 bulges,
// amount < 0 pinches. The inverse samples at radius / s(d).
struct BulgeParams {
    Vec2 center{0.5f, 0.5f};
    float amount{0.5f};   // [-1, 1]
    float radius{0.5f};
    sampling::EdgeMode edge_mode{sampling::EdgeMode::Transparent};
};

// Mesh Warp — control grid (cols x rows) of displacement vectors, bilinearly
// interpolated between grid points (smoothstep-weighted when smoothness > 0).
struct MeshWarpParams {
    int cols{2};               // [2, 32]
    int rows{2};               // [2, 32]
    float smoothness{0.0f};    // [0, 1]
    std::vector<Vec2> offsets; // cols * rows displacement vectors (normalized)
    sampling::EdgeMode edge_mode{sampling::EdgeMode::Transparent};
};

struct SharpenParams { float amount{0.0f}; float radius{1.0f}; float threshold{0.0f}; };
struct RoughenEdgesParams {
    float amount{0.0f};       // maximum boundary displacement in pixels [0, 64]
    float scale{8.0f};         // noise cell size in pixels (0, 1024]
    std::uint32_t complexity{1}; // fBm octave count [1, 4]
    float evolution{0.0f};     // deterministic noise phase
    float evolution_speed{0.0f}; // phase units per second
    std::uint32_t seed{0};
};
struct TurbulentDisplaceParams {
    float amount{0.1f}, size{0.1f}, evolution{0.0f}, evolution_speed{0.0f};
    std::uint32_t complexity{2}; sampling::EdgeMode edge_mode{sampling::EdgeMode::Transparent};
};
struct WaveWarpParams {
    enum class WaveType : std::uint8_t { Sine, Triangle, Square, Saw };
    WaveType type{WaveType::Sine}; Vec2 direction{1.0f, 0.0f};
    float amplitude{0.05f}, wavelength{0.2f}, speed{1.0f}, phase{0.0f};
    sampling::EdgeMode edge_mode{sampling::EdgeMode::Transparent};
};
struct RippleParams {
    Vec2 center{0.5f, 0.5f}; float radius{0.25f}, wave_speed{1.5f}, width{0.1f}, wave_height{0.05f}, damping{0.5f};
    sampling::EdgeMode edge_mode{sampling::EdgeMode::Transparent};
};
struct DisplacementMapParams {
    enum class Channel : std::uint8_t { Luma, Red, Green, Blue, Alpha };
    std::string map_layer;
    float intensity_x{0.05f}, intensity_y{0.05f};
    Channel channel{Channel::Luma};
    std::optional<graphics::Field2D> field;
    sampling::EdgeMode edge_mode{sampling::EdgeMode::Transparent};
};
enum class FoldAxis : std::uint8_t {
    Vertical,
    Horizontal
};

struct LensDistortionParams {
    float curvature{0.15f};     // > 0 barrel, < 0 pincushion [-1, 1]
    Vec2 center{0.5f, 0.5f};     // normalized optical center [0, 1]
    float scale{1.0f};          // zoom scale compensation [0.1, 4]
    sampling::EdgeMode edge_mode{sampling::EdgeMode::Transparent};
};

struct FoldWarpParams {
    FoldAxis axis{FoldAxis::Vertical};
    float position{0.5f};       // crease position [0, 1]
    float amount{0.25f};        // fold displacement [-1, 1]
    float radius{0.20f};        // influence radius [0.01, 1]
    float falloff{1.0f};        // falloff exponent [0.1, 4]
    float perspective{0.05f};   // 3D perspective foreshortening [0, 0.5]
    sampling::EdgeMode edge_mode{sampling::EdgeMode::Transparent};
};

struct CreaseShadingParams {
    FoldAxis axis{FoldAxis::Vertical};
    float position{0.5f};       // crease position [0, 1]
    float width{24.0f};         // shading extent in pixels [1, 256]
    float dark_side{0.35f};     // shadow intensity [0, 1]
    float light_side{0.20f};    // highlight intensity [0, 1]
    float softness{0.50f};      // feather softness [0, 1]
};

// Vignette is the only WIP-local parameter type kept here; the Chromatic
// Aberration and Halftone structs above are the canonical (upstream) ones.
struct VignetteEffectParams { float amount{0.5f}; float radius{0.5f}; float softness{0.5f}; Color color{0,0,0,1}; };

// Type-erased variant: stores exactly one effect parameter struct, indexed by
// EffectType.  Extraction via std::get_if<T>() is O(1) with no type_info
// comparison — strictly faster than std::any_cast's runtime type check.
using EffectParams = std::variant<
    std::monostate,
    BlurParams,
    TintParams,
    BrightnessParams,
    ContrastParams,
    DropShadowParams,
    GlowParams,
    BloomParams,
    Fake3DWaveParams,
    SaturationParams,
    HueRotateParams,
    InvertParams,
    VignetteParams,
    ExposureParams,
    LevelsParams,
    TemperatureTintParams,
    FillParams,
    NoiseParams,
    FractalNoiseParams,
    CheckerboardParams,
    FourColorGradientParams,
    Lut3DParams,
    EchoParams,
    KeyingParams,
    ThresholdParams,
    MosaicParams,
    LightRaysParams,
    OffsetParams,
    DirectionalBlurParams,
    RadialBlurParams,
    StrokeParams,
    CurvesParams,
    CornerPinParams,
    TwirlParams,
    BulgeParams,
    MeshWarpParams,
    SharpenParams, RoughenEdgesParams, TurbulentDisplaceParams, WaveWarpParams, RippleParams, DisplacementMapParams,
    ChromaticAberrationParams, VignetteEffectParams, HalftoneParams,
    LensDistortionParams, LuminanceResponseParams, FoldWarpParams, CreaseShadingParams,
    LiftGammaGainParams
>;

} // namespace chronon3d
