#pragma once

#include <string_view>

namespace chronon3d::effects::ids {

inline constexpr std::string_view BlurGaussian = "blur.gaussian";
inline constexpr std::string_view ColorTint = "color.tint";
inline constexpr std::string_view ColorBrightness = "color.brightness";
inline constexpr std::string_view ColorContrast = "color.contrast";
inline constexpr std::string_view LightDropShadow = "light.drop_shadow";
inline constexpr std::string_view LightGlow = "light.glow";
inline constexpr std::string_view LightBloom = "light.bloom";
inline constexpr std::string_view DistortFake3DWave = "distort.fake_3d_wave";

// ── Color effects: Exposure + Levels (A2) ──
inline constexpr std::string_view ColorExposure = "color.exposure";
inline constexpr std::string_view ColorLevels   = "color.levels";
inline constexpr std::string_view ColorTemperatureTint = "color.temperature_tint";
inline constexpr std::string_view ColorLiftGammaGain = "color.lift_gamma_gain";

// ── A4: Fill, Noise, Offset ──
inline constexpr std::string_view GenerateFill   = "generate.fill";
inline constexpr std::string_view GenerateNoise  = "generate.noise";
inline constexpr std::string_view GenerateFractalNoise = "generate.fractal_noise";
inline constexpr std::string_view GenerateCheckerboard = "generate.checkerboard";
inline constexpr std::string_view GenerateFourColorGradient = "generate.four_color_gradient";
inline constexpr std::string_view GenerateLightRays = "generate.light_rays";
inline constexpr std::string_view DistortOffset  = "distort.offset";

// ── A5: Directional Blur ──
inline constexpr std::string_view DistortDirectionalBlur = "distort.directional_blur";

// ── A6: Radial Blur ──
inline constexpr std::string_view DistortRadialBlur = "distort.radial_blur";

// ── A3: Curves ──
inline constexpr std::string_view ColorCurves = "color.curves";

// ── A7: Stroke ──
inline constexpr std::string_view StylizeStroke = "stylize.stroke";

// ── Adjustment-layer color correction effects (AE-5) ──
inline constexpr std::string_view ColorSaturation = "color.saturation";
inline constexpr std::string_view ColorHueRotate   = "color.hue_rotate";
inline constexpr std::string_view ColorInvert      = "color.invert";
inline constexpr std::string_view ColorVignette    = "color.vignette";
inline constexpr std::string_view ColorLut3D       = "color.lut_3d";
inline constexpr std::string_view TemporalEcho     = "temporal.echo";
inline constexpr std::string_view MatteKeying      = "matte.luma_chroma_key";
inline constexpr std::string_view StylizeThreshold = "stylize.threshold";
inline constexpr std::string_view StylizeMosaic    = "stylize.mosaic";

// ── Warp family (AE distortion parity) ──
inline constexpr std::string_view DistortCornerPin = "distort.corner_pin";
inline constexpr std::string_view DistortTwirl     = "distort.twirl";
inline constexpr std::string_view DistortBulge     = "distort.bulge";
inline constexpr std::string_view DistortMeshWarp  = "distort.mesh_warp";
inline constexpr std::string_view StylizeSharpen = "sharpen";
inline constexpr std::string_view DistortTurbulentDisplace = "distort.turbulent_displace";
inline constexpr std::string_view StylizeRoughenEdges = "stylize.roughen_edges";
inline constexpr std::string_view DistortWaveWarp = "distort.wave_warp";
inline constexpr std::string_view DistortRipple = "distort.ripple";
inline constexpr std::string_view DistortDisplacementMap = "distort.displacement_map";
inline constexpr std::string_view StylizeChromaticAberration = "stylize.chromatic_aberration";
inline constexpr std::string_view StylizeHalftone = "stylize.halftone";
inline constexpr std::string_view DistortLensDistortion = "distort.lens_distortion";
inline constexpr std::string_view LightLuminanceResponse = "light.luminance_response";
inline constexpr std::string_view DistortFoldWarp = "distort.fold_warp";
inline constexpr std::string_view LightCreaseShading = "light.crease_shading";

} // namespace chronon3d::effects::ids
