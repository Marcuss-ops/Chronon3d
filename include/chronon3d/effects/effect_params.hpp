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
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

namespace chronon3d {

struct BlurParams          { f32   radius{0.0f}; };
struct TintParams          { Color color{0,0,0,0}; f32 amount{1.0f}; };
// Bodymovin/After Effects two-color Tint controls, stored in sRGB coordinates.
struct LottieTintParams {
    f32 black_r{0.0f}, black_g{0.0f}, black_b{0.0f};
    f32 white_r{1.0f}, white_g{1.0f}, white_b{1.0f};
    f32 amount{1.0f};
};
struct LottieTritoneParams {
    f32 dark_r{0.0f}, dark_g{0.0f}, dark_b{0.0f};
    f32 mid_r{0.5f}, mid_g{0.5f}, mid_b{0.5f};
    f32 bright_r{1.0f}, bright_g{1.0f}, bright_b{1.0f};
    f32 amount{1.0f};
};
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

// General 4x5 affine color transform. Values are row-major; each output
// channel computes dot(row[0..3], RGBA) + row[4].
struct ColorMatrixParams {
    std::array<float, 20> values{
        1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,1,0};
};

// Posterize each straight-RGB channel to a fixed number of evenly spaced
// values over [0, 1]. Alpha is preserved and HDR values are clamped by design.
struct PosterizeParams {
    std::uint32_t levels{6};
    void validate() const {
        if (levels < 2 || levels > 256)
            throw std::invalid_argument("Posterize levels must be in [2, 256]");
    }
};

// Map straight-RGB luminance through five evenly spaced colour stops
// (0, 0.25, 0.5, 0.75, 1), then blend the result with the source by `amount`.
// Mirrors AE's CC Toner / colour-map controlled look without a LUT asset.
// Alpha is preserved.
struct ColorMapParams {
    std::array<float, 15> stops{
        0.0f, 0.0f, 0.0f,
        0.25f, 0.25f, 0.25f,
        0.5f, 0.5f, 0.5f,
        0.75f, 0.75f, 0.75f,
        1.0f, 1.0f, 1.0f};
    // Blend between the source colour (0) and the mapped colour (1).
    float amount{1.0f};
    void validate() const {
        for (const float value : stops) {
            if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
                throw std::invalid_argument("ColorMap stops must be finite in [0, 1]");
        }
        if (!std::isfinite(amount) || amount < 0.0f || amount > 1.0f)
            throw std::invalid_argument("ColorMap amount must be finite in [0, 1]");
    }
};

// Directional alpha wipe over the full layer surface. Progress 0 is hidden,
// progress 1 is fully revealed; angle 0 reveals left-to-right and 90 top-to-
// bottom. Feather is the transition width as a fraction of the projected
// frame span. RGB stays premultiplied and is scaled with alpha.
struct LinearWipeParams {
    float progress{1.0f};
    float angle_degrees{0.0f};
    float feather{0.0f};
    void validate() const {
        if (!std::isfinite(progress) || progress < 0.0f || progress > 1.0f ||
            !std::isfinite(angle_degrees) || angle_degrees < 0.0f || angle_degrees > 360.0f ||
            !std::isfinite(feather) || feather < 0.0f || feather > 1.0f)
            throw std::invalid_argument("LinearWipe parameters are outside their supported ranges");
    }
};

// Square-neighborhood component morphology on premultiplied RGBA, with
// transparent pixels beyond the layer. Maximum dilates; minimum erodes.
struct MinMaxParams {
    std::uint32_t radius{0}; // [0, 64] pixels; zero is exact identity
    bool maximum{true};
    void validate() const {
        if (radius > 64)
            throw std::invalid_argument("MinMax radius must be in [0, 64]");
    }
};

// Directional pixel stretch (CC Scale Wipe-style), not a wave/fold alias.
// Pixels beyond position sample the boundary; stretch controls the blend.
// Direction is in screen pixels (0 right, 90 down), independent of aspect.
struct StretchWarpParams {
    float position{0.5f};
    float stretch{0.0f};
    float angle_degrees{0.0f};
    void validate() const {
        if (!std::isfinite(position) || position < 0.0f || position > 1.0f ||
            !std::isfinite(stretch) || stretch < 0.0f || stretch > 1.0f ||
            !std::isfinite(angle_degrees) || angle_degrees < 0.0f || angle_degrees > 360.0f)
            throw std::invalid_argument("StretchWarp parameters are outside their supported ranges");
    }
};

// Highlight-driven lens artifacts around a normalized optical center.
// Thresholded premultiplied source energy drives a halo, mirrored ghost,
// and horizontal streak. Generated light is source-over with matching alpha.
struct LensFlareParams {
    Vec2 center{0.5f, 0.5f};
    float threshold{0.8f};
    float intensity{0.0f}; // [0, 8]; zero is exact identity
    float radius{0.15f}; // [0.001, 1], fraction of min(width, height)
    float halo{0.5f};
    float ghost{0.5f};
    float streak{0.5f};
    void validate() const {
        for (float value : {center.x, center.y, threshold, halo, ghost, streak})
            if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
                throw std::invalid_argument("LensFlare normalized parameters must be finite in [0, 1]");
        if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 8.0f ||
            !std::isfinite(radius) || radius < 0.001f || radius > 1.0f)
            throw std::invalid_argument("LensFlare intensity or radius is outside its supported range");
    }
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
    Mix,
    // Bodymovin/After Effects Fill replaces the layer color and applies its
    // animated opacity to the source alpha, including zero opacity.
    ReplaceAlpha
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

// Vignette is the only WIP-local parameter type kept here (tracked by issue #59); the Chromatic
// Aberration and Halftone structs above are the canonical (upstream) ones.
struct VignetteEffectParams { float amount{0.5f}; float radius{0.5f}; float softness{0.5f}; Color color{0,0,0,1}; };

// ── Post-process FX family (V1) ─────────────────────────────────────────────
// See docs/tickets/TICKET-POSTPROCESS-FX.md for the shader-math contracts.

// Scanlines — subtle per-pixel luminance modulation along one axis.
// axis 0 = lines repeat along Y (classic horizontal scanlines);
// axis 1 = lines repeat along X (vertical scanlines).
struct ScanlinesParams {
    std::uint8_t axis{0};      // 0 = horizontal lines (Y repeat), 1 = vertical
    float spacing_px{2.0f};    // line pitch in pixels [1, 64]
    float amount{0.08f};       // modulation depth [0, 1] (modern style: 0.03-0.08)
    float phase{0.0f};         // line grid phase offset [0, 1)
    void validate() const {
        if (!std::isfinite(spacing_px) || spacing_px < 1.0f || spacing_px > 64.0f ||
            !std::isfinite(amount) || amount < 0.0f || amount > 1.0f ||
            !std::isfinite(phase) || phase < 0.0f || phase >= 1.0f || axis > 1)
            throw std::invalid_argument("Scanlines parameters are outside the supported range");
    }
};

// Retro TV — analog signal degradation: horizontal tape jitter (time-driven,
// deterministic) plus chrominace smear (U/V low-pass while luma stays sharp).
struct RetroTvParams {
    float jitter_px{3.0f};     // max horizontal tape offset in pixels [0, 32]
    float jitter_speed{20.0f}; // jitter waveform frequency in Hz (0, 120]
    float smear_px{8.0f};      // chroma horizontal smear radius in pixels [0, 32]
    float bleed_strength{0.35f}; // chroma smear blend strength [0, 1]
    float luma_noise{0.06f};   // per-pixel luma interference noise [0, 1]
    std::uint32_t seed{0};     // deterministic jitter/noise salt
    void validate() const {
        if (!std::isfinite(jitter_px) || jitter_px < 0.0f || jitter_px > 32.0f ||
            !std::isfinite(jitter_speed) || jitter_speed < 0.0f || jitter_speed > 120.0f ||
            !std::isfinite(smear_px) || smear_px < 0.0f || smear_px > 32.0f ||
            !std::isfinite(bleed_strength) || bleed_strength < 0.0f || bleed_strength > 1.0f ||
            !std::isfinite(luma_noise) || luma_noise < 0.0f || luma_noise > 1.0f)
            throw std::invalid_argument("Retro TV parameters are outside the supported range");
    }
};

// Film halation — warm red/orange bounce light around high-luminance
// contours: highlight mask, blur in the warm hue, screen-mode composite.
struct HalationParams {
    float threshold{0.85f};    // highlight onset [0, 1]
    float softness{0.15f};     // highlight mask feather above the threshold [0, 1]
    float radius_px{12.0f};    // warm-blur radius in pixels [0, 64]
    float intensity{0.50f};    // composite strength [0, 8]
    Color tint{1.0f, 0.25f, 0.05f, 1.0f}; // anti-halation warm hue
    void validate() const {
        if (!std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f ||
            !std::isfinite(softness) || softness < 0.0f || softness > 1.0f ||
            !std::isfinite(radius_px) || radius_px < 0.0f || radius_px > 64.0f ||
            !std::isfinite(intensity) || intensity < 0.0f || intensity > 8.0f)
            throw std::invalid_argument("Film halation parameters are outside the supported range");
    }
};

// Anamorphic streak — thin horizontal highlight striature (1 px vertical,
// wide horizontal box blur) composited additively, anamorphic-lens style.
struct AnamorphicStreakParams {
    float threshold{0.85f};    // highlight onset [0, 1]
    float softness{0.10f};     // highlight mask feather above the threshold [0, 1]
    float streak_length_px{128.0f}; // horizontal blur run in pixels [0, 512]
    float intensity{0.35f};    // composite strength [0, 8]
    Color tint{0.55f, 0.75f, 1.0f, 1.0f}; // streak hue (cool cyan/blue)
    void validate() const {
        if (!std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f ||
            !std::isfinite(softness) || softness < 0.0f || softness > 1.0f ||
            !std::isfinite(streak_length_px) || streak_length_px < 0.0f || streak_length_px > 512.0f ||
            !std::isfinite(intensity) || intensity < 0.0f || intensity > 8.0f)
            throw std::invalid_argument("Anamorphic streak parameters are outside the supported range");
    }
};

// Type-erased variant: stores exactly one effect parameter struct, indexed by
// EffectType.  Extraction via std::get_if<T>() is O(1) with no type_info
// comparison — strictly faster than std::any_cast's runtime type check.
using EffectParams = std::variant<
    std::monostate,
    BlurParams,
    TintParams,
    LottieTintParams,
    LottieTritoneParams,
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
    LiftGammaGainParams, ColorMatrixParams, PosterizeParams, ColorMapParams, LinearWipeParams, MinMaxParams, StretchWarpParams, LensFlareParams, ScanlinesParams, RetroTvParams, HalationParams, AnamorphicStreakParams
>;

} // namespace chronon3d
