#pragma once

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/animation/core/animated_value.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::effect_presets {

inline void rgb_split(LayerPlan& layer, float separation = 4.0f) {
    if (!std::isfinite(separation) || separation < 0.0f || separation > 128.0f)
        throw std::invalid_argument("RGB split separation must be in [0, 128]");
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
    effect.chromatic_red_offset = separation;
    effect.chromatic_blue_offset = -separation;
    layer.effects.push_back(std::move(effect));
}

/// Add the engine's isotropic Gaussian blur as a lightweight lens-softening preset.
inline void lens_blur(LayerPlan& layer, float radius = 8.0f) {
    if (!std::isfinite(radius) || radius < 0.0f || radius > 256.0f)
        throw std::invalid_argument("lens blur radius must be in [0, 256]");
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::GaussianBlur;
    effect.radius = radius;
    layer.effects.push_back(std::move(effect));
}

/// Add bounded horizontal scanline bands using the existing square WaveWarp.
inline void scanlines(LayerPlan& layer, float intensity = 0.02f,
                      float spacing = 0.035f) {
    if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 0.25f ||
        !std::isfinite(spacing) || spacing < 0.002f || spacing > 1.0f)
        throw std::invalid_argument("scanline intensity/spacing is outside supported bounds");
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::WaveWarp;
    effect.amount = intensity;
    effect.warp_wavelength = spacing;
    effect.warp_direction = {0.0f, 1.0f};
    effect.wave_type = 2;
    layer.effects.push_back(std::move(effect));
}

/// Compose horizontal square-wave distortion, RGB separation, and seeded RGB
/// grain. All three stages lower through the existing software effect stack.
inline void glitch(LayerPlan& layer, float intensity = 0.5f,
                   std::uint32_t seed = 0) {
    if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 1.0f)
        throw std::invalid_argument("glitch intensity must be in [0, 1]");
    LayerPlan::EffectPlan warp;
    warp.kind = LayerPlan::EffectKindPlan::WaveWarp;
    warp.amount = 0.025f * intensity;
    warp.warp_wavelength = 0.035f;
    warp.warp_speed = 0.0f;
    warp.warp_direction = {0.0f, 1.0f};
    warp.wave_type = 2; // WaveWarpParams::WaveType::Square
    layer.effects.push_back(std::move(warp));

    LayerPlan::EffectPlan split;
    split.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
    split.chromatic_red_offset = 8.0f * intensity;
    split.chromatic_blue_offset = -8.0f * intensity;
    layer.effects.push_back(std::move(split));

    LayerPlan::EffectPlan grain;
    grain.kind = LayerPlan::EffectKindPlan::Noise;
    grain.amount = 0.12f * intensity;
    grain.seed = seed;
    grain.animated = true;
    grain.rgb_noise = true;
    grain.noise_size = 1.0f;
    layer.effects.push_back(std::move(grain));
}

/// Build a bounded moving aurora palette for a filled rect/rounded-rect/ellipse
/// layer. Its period is baked at quarter-period keyframes over the layer span.
inline GradientAnimationPlan aurora_gradient(
    Frame start, Frame duration, FrameRate fps, float period_seconds = 6.0f) {
    if (start.integral() < 0 || duration.integral() < 1 || duration.integral() > 65535 ||
        !std::isfinite(period_seconds) || period_seconds <= 0.0f ||
        fps.num() <= 0 || fps.den() <= 0)
        throw std::invalid_argument("aurora gradient requires bounded positive timing and frame rate");
    const double raw_period = static_cast<double>(period_seconds) * fps.num() / fps.den();
    const auto period = static_cast<std::int64_t>(std::llround(raw_period));
    if (period < 4 || period > 65535)
        throw std::invalid_argument("aurora gradient period must fit in 4..65535 frames");

    const std::array<Color, 4> palette{
        Color{0.05f, 0.78f, 0.66f, 1.0f},
        Color{0.12f, 0.34f, 0.92f, 1.0f},
        Color{0.58f, 0.16f, 0.82f, 1.0f},
        Color{0.92f, 0.25f, 0.53f, 1.0f}};
    const auto color_at = [&](float position, float phase) {
        const float coordinate = std::fmod(position * 4.0f + phase * 4.0f, 4.0f);
        const auto left = static_cast<std::size_t>(std::floor(coordinate)) % palette.size();
        const auto right = (left + 1) % palette.size();
        return chronon3d::lerp(palette[left], palette[right], coordinate - std::floor(coordinate));
    };
    const auto gradient_at = [&](float phase) {
        return graphics::GradientDefinition::linear(
            Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f},
            {{0.0f, color_at(0.0f, phase)},
             {0.5f, color_at(0.5f, phase)},
             {1.0f, color_at(1.0f, phase)}});
    };

    GradientAnimationPlan result;
    result.easing = "linear";
    for (std::int64_t cycle = 0; cycle < duration.integral(); cycle += period) {
        for (int quarter = 0; quarter < 4; ++quarter) {
            const auto relative = cycle + static_cast<std::int64_t>(std::llround(
                static_cast<double>(period) * static_cast<double>(quarter) / 4.0));
            if (relative >= duration.integral()) continue;
            result.keyframes.push_back({start + Frame{relative}, gradient_at(
                static_cast<float>(quarter) * 0.25f)});
        }
    }
    const float final_phase = static_cast<float>(duration.integral() % period) /
                              static_cast<float>(period);
    result.keyframes.push_back({start + duration, gradient_at(final_phase)});
    return result;
}

/// Velocity-driven chromatic RGB separation. Offset direction and magnitude
/// scale strictly with layer velocity vector.
inline void velocity_rgb_split(LayerPlan& layer, Vec2 velocity, float separation = 4.0f) {
    if (!std::isfinite(separation) || separation < 0.0f || separation > 128.0f ||
        !std::isfinite(velocity.x) || !std::isfinite(velocity.y))
        throw std::invalid_argument("velocity RGB split parameters are invalid");
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
    effect.chromatic_red_offset = separation;
    effect.chromatic_blue_offset = -separation;
    effect.chromatic_offset_mode = 2; // Velocity mode
    effect.chromatic_velocity = {velocity.x, velocity.y};
    layer.effects.push_back(std::move(effect));
}

/// CRT display recipe: barrel lens curvature + scanline lattice + phosphor RGB separation + bloom glow + RF noise.
inline void crt(LayerPlan& layer, float curvature = 0.08f, float scanline_intensity = 0.035f,
                float rgb_separation = 2.5f, float bloom_amount = 0.35f,
                float rf_noise = 0.05f, std::uint32_t seed = 1984) {
    if (!std::isfinite(curvature) || curvature < -1.0f || curvature > 1.0f ||
        !std::isfinite(scanline_intensity) || scanline_intensity < 0.0f || scanline_intensity > 0.25f)
        throw std::invalid_argument("CRT recipe parameters are outside supported bounds");

    // 1. Lens Distortion (barrel curvature)
    LayerPlan::EffectPlan lens;
    lens.kind = LayerPlan::EffectKindPlan::LensDistortion;
    lens.lens_curvature = curvature;
    lens.lens_scale = 1.0f + std::abs(curvature) * 0.25f; // compensate zoom
    layer.effects.push_back(std::move(lens));

    // 2. Scanline bands
    scanlines(layer, scanline_intensity, 0.025f);

    // 3. Phosphor RGB split
    if (rgb_separation > 0.001f) {
        rgb_split(layer, rgb_separation);
    }

    // 4. Phosphor Bloom
    if (bloom_amount > 0.001f) {
        LayerPlan::EffectPlan bloom;
        bloom.kind = LayerPlan::EffectKindPlan::Bloom;
        bloom.bloom_threshold = 0.65f;
        bloom.bloom_intensity = bloom_amount;
        bloom.radius = 16.0f;
        layer.effects.push_back(std::move(bloom));
    }

    // 5. Analog RF noise
    if (rf_noise > 0.001f) {
        LayerPlan::EffectPlan grain;
        grain.kind = LayerPlan::EffectKindPlan::Noise;
        grain.amount = rf_noise;
        grain.seed = seed;
        grain.animated = true;
        layer.effects.push_back(std::move(grain));
    }
}

/// Paper/document fold: geometric crease deformation and lighting shading.
inline void paper_fold(LayerPlan& layer, float amount = 0.30f, float position = 0.5f,
                       bool horizontal = false, float radius = 0.20f) {
    if (!std::isfinite(amount) || amount < -1.0f || amount > 1.0f ||
        !std::isfinite(position) || position < 0.0f || position > 1.0f ||
        !std::isfinite(radius) || radius < 0.01f || radius > 1.0f)
        throw std::invalid_argument("paper fold parameters are outside supported bounds");

    // 1. Fold warp
    LayerPlan::EffectPlan fold;
    fold.kind = LayerPlan::EffectKindPlan::FoldWarp;
    fold.fold_axis = horizontal ? 1 : 0;
    fold.fold_position = position;
    fold.fold_amount = amount;
    fold.fold_radius = radius;
    fold.fold_falloff = 1.2f;
    fold.fold_perspective = 0.06f;
    layer.effects.push_back(std::move(fold));

    // 2. Crease shading (shadow and specular highlight)
    LayerPlan::EffectPlan crease;
    crease.kind = LayerPlan::EffectKindPlan::CreaseShading;
    crease.crease_axis = horizontal ? 1 : 0;
    crease.crease_position = position;
    crease.crease_width = 28.0f;
    crease.crease_dark_side = 0.35f * std::abs(amount);
    crease.crease_light_side = 0.20f * std::abs(amount);
    crease.crease_softness = 0.50f;
    layer.effects.push_back(std::move(crease));
}

/// Film halation: warm photochemical bleed in high-luminance areas.
inline void halation(LayerPlan& layer, float intensity = 1.0f, float threshold = 0.75f,
                     float radius = 22.0f,
                     Color tint = Color{1.0f, 0.32f, 0.12f, 1.0f}) {
    if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 8.0f ||
        !std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f ||
        !std::isfinite(radius) || radius < 0.0f || radius > 256.0f)
        throw std::invalid_argument("halation parameters are outside supported bounds");

    LayerPlan::EffectPlan hal;
    hal.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    hal.response_threshold = threshold;
    hal.response_softness = 0.20f;
    hal.response_radius = radius;
    hal.response_tint = {tint.r, tint.g, tint.b, tint.a};
    hal.response_intensity = intensity;
    hal.response_blend = 0; // Screen
    layer.effects.push_back(std::move(hal));
}

/// Anamorphic lens flare streak: luminance trigger + horizontal streak convolution + additive tint.
inline void anamorphic_flare(LayerPlan& layer, float length = 56.0f, float intensity = 1.2f,
                             Color tint = Color{0.18f, 0.65f, 1.0f, 1.0f}) {
    if (!std::isfinite(length) || length < 0.0f || length > 512.0f ||
        !std::isfinite(intensity) || intensity < 0.0f || intensity > 8.0f)
        throw std::invalid_argument("anamorphic flare parameters are outside supported bounds");

    LayerPlan::EffectPlan flare;
    flare.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    flare.response_threshold = 0.80f;
    flare.response_softness = 0.15f;
    flare.response_radius = 8.0f;
    flare.response_tint = {tint.r, tint.g, tint.b, tint.a};
    flare.response_intensity = intensity;
    flare.response_blend = 1; // Add

    LayerPlan::EffectPlan streak;
    streak.kind = LayerPlan::EffectKindPlan::DirectionalBlur;
    streak.directional_angle = 0.0f; // Pure horizontal anamorphic streak
    streak.directional_length = length;
    streak.directional_samples = static_cast<int>(std::clamp(length * 0.5f, 8.0f, 64.0f));

    layer.effects.push_back(std::move(flare));
    layer.effects.push_back(std::move(streak));
}

/// Atmospheric convective heatwave: organic fractal turbulence with slow vertical drift.
inline void heatwave(LayerPlan& layer, float amount = 0.035f, float speed = 1.2f,
                     float size = 0.12f, float evolution = 0.0f) {
    if (!std::isfinite(amount) || amount < 0.0f || amount > 1.0f ||
        !std::isfinite(speed) || !std::isfinite(size) || size <= 0.0f)
        throw std::invalid_argument("heatwave parameters are outside supported bounds");

    LayerPlan::EffectPlan displace;
    displace.kind = LayerPlan::EffectKindPlan::TurbulentDisplace;
    displace.amount = amount;
    displace.warp_size = size;
    displace.warp_speed = speed;
    displace.warp_evolution = evolution;
    displace.warp_complexity = 3;
    layer.effects.push_back(std::move(displace));
}

/// Frosted glass backdrop composition.
inline void frosted_glass(LayerPlan& layer, const BackdropFilterPlan& filter = BackdropFilterPlan::glass_frosted()) {
    filter.validate();
    layer.backdrop_filter = filter;
}

// ── Light Leak V1 ──────────────────────────────────────────────────────────
//
// Ten canonical leak recipes composed from the existing luminance, glow,
// bloom, ray and dispersion primitives. Every recipe:
//   * is deterministic — identical inputs push identical EffectPlan fields;
//   * fails closed on out-of-range parameters;
//   * lowers intensity 0 to a fully transparent shape with no optional effect stages;
//   * bounds total energy through the underlying effect validators, so an
//     SDR white plate cannot explode: screen/add blends saturate at the
//     compositor and the response intensity ceiling is enforced here.

namespace light_leak {

inline constexpr float kMaxIntensity = 4.0f;
inline constexpr Color kWarmTint{1.0f, 0.62f, 0.24f, 1.0f};
inline constexpr Color kAmberTint{1.0f, 0.80f, 0.45f, 1.0f};
inline constexpr Color kCoolTint{0.55f, 0.78f, 1.0f, 1.0f};

enum class LightLeakShape : std::uint8_t { Edge, Corner, Sweep, Radial, Band };
enum class LightLeakBlend : std::uint8_t { Screen, Add, SoftLight };
enum class LightLeakEdge : std::uint8_t { Left, Top, Right, Bottom };
enum class LightLeakCorner : std::uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };

/// Declarative controls for one deterministic, screen-space light leak.
/// Geometry is lowered to the existing gradient/shape/effect/compositor path;
/// this profile does not introduce a second pixel renderer.
struct LightLeakProfile {
    LightLeakShape shape{LightLeakShape::Edge};
    LightLeakBlend blend{LightLeakBlend::Screen};
    LightLeakEdge edge{LightLeakEdge::Left};
    LightLeakCorner corner{LightLeakCorner::TopLeft};
    Color color_a{1.0f, 0.62f, 0.24f, 1.0f};
    Color color_b{1.0f, 0.90f, 0.62f, 1.0f};
    float intensity{1.0f};
    float softness{0.35f};       // normalized [0, 1]
    float spread{0.5f};          // normalized radial radius / extent
    float angle{0.0f};           // degrees, clockwise in screen coordinates
    float position{0.5f};        // normalized position; off-canvas allowed [-4, 5]
    float position_y{0.5f};      // normalized radial center Y; off-canvas allowed [-4, 5]
    float width{0.25f};          // normalized band width [0, 1]
    float chroma_split{0.0f};    // symmetric channel offset in pixels
    float grain{0.0f};           // spatially stable seeded grain [0, 1]
    float flicker{0.0f};         // explicit frame-varying noise amount [0, 1]
    std::uint32_t seed{0};

    [[nodiscard]] bool valid() const noexcept {
        const auto valid_color = [](const Color& color) {
            const auto unit = [](float value) {
                return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
            };
            return unit(color.r) && unit(color.g) && unit(color.b) && unit(color.a);
        };
        const bool valid_shape = shape == LightLeakShape::Edge ||
            shape == LightLeakShape::Corner || shape == LightLeakShape::Sweep ||
            shape == LightLeakShape::Radial || shape == LightLeakShape::Band;
        const bool valid_blend = blend == LightLeakBlend::Screen ||
            blend == LightLeakBlend::Add || blend == LightLeakBlend::SoftLight;
        const bool valid_edge = edge == LightLeakEdge::Left || edge == LightLeakEdge::Top ||
            edge == LightLeakEdge::Right || edge == LightLeakEdge::Bottom;
        const bool valid_corner = corner == LightLeakCorner::TopLeft ||
            corner == LightLeakCorner::TopRight || corner == LightLeakCorner::BottomLeft ||
            corner == LightLeakCorner::BottomRight;
        return valid_shape && valid_blend && valid_edge && valid_corner &&
            valid_color(color_a) && valid_color(color_b) &&
            std::isfinite(intensity) && intensity >= 0.0f && intensity <= kMaxIntensity &&
            std::isfinite(softness) && softness >= 0.0f && softness <= 1.0f &&
            std::isfinite(spread) && spread >= 0.0f && spread <= 1.0f &&
            std::isfinite(angle) && std::abs(angle) <= 3600.0f &&
            std::isfinite(position) && position >= -4.0f && position <= 5.0f &&
            std::isfinite(position_y) && position_y >= -4.0f && position_y <= 5.0f &&
            std::isfinite(width) && width >= 0.0f && width <= 1.0f &&
            std::isfinite(chroma_split) && chroma_split >= 0.0f && chroma_split <= 16.0f &&
            std::isfinite(grain) && grain >= 0.0f && grain <= 1.0f &&
            std::isfinite(flicker) && flicker >= 0.0f && flicker <= 1.0f;
    }
};

/// Deterministic time driver for one leak emitter. Time is supplied by the
/// caller (normally frame / fps); no frame-local random state is used.
struct AnimatedLightLeakEmitter {
    LightLeakProfile profile{};
    Vec2 velocity{0.0f, 0.0f}; // normalized canvas units per second
    float pulse_amount{0.0f};
    float pulse_speed{0.0f};  // radians per second
    float phase{0.0f};
    std::string id{"light_leak"};
};

/// Canonical lowering for profile-authored leaks. The generated full-canvas
/// shape uses a gradient mask, then the ordinary ordered effect stack and
/// layer blend mode; equal profile/canvas/id values produce equal plans.
class LightLeakResolver {
public:
    [[nodiscard]] static LayerPlan resolve(const LightLeakProfile& profile,
                                           Vec2 canvas_size,
                                           std::string id = "light_leak") {
        if (!profile.valid() || !std::isfinite(canvas_size.x) ||
            !std::isfinite(canvas_size.y) || canvas_size.x <= 0.0f ||
            canvas_size.y <= 0.0f || canvas_size.x > 1000000.0f ||
            canvas_size.y > 1000000.0f)
            throw std::invalid_argument("light leak profile or canvas is outside supported bounds");

        LayerPlan layer;
        layer.id = std::move(id);
        layer.type = LayerType::Shape;
        layer.size = {canvas_size.x, canvas_size.y};
        layer.size_dimensions = 2;
        layer.position = {canvas_size.x * 0.5f, canvas_size.y * 0.5f, 0.0f};
        layer.position_dimensions = 2;
        layer.screen_space = true;
        layer.blend_mode = profile.blend == LightLeakBlend::Add
            ? chronon3d::BlendMode::Add
            : profile.blend == LightLeakBlend::SoftLight
                ? chronon3d::BlendMode::SoftLight : chronon3d::BlendMode::Screen;

        ShapeContentPlan shape;
        shape.type = ShapeContentTypePlan::Rect;
        shape.fill_color = std::array<float, 4>{
            profile.color_a.r, profile.color_a.g, profile.color_a.b, 0.0f};
        const Vec2 center{profile.position, profile.position_y};
        const float strength = profile.intensity;
        const Color clear = Color::transparent();
        const Color a{profile.color_a.r, profile.color_a.g,
                      profile.color_a.b, profile.color_a.a};
        const Color b{profile.color_b.r, profile.color_b.g,
                      profile.color_b.b, profile.color_b.a};
        const auto with_intensity = [strength](Color color) {
            color.r = std::min(color.r * strength, kMaxIntensity);
            color.g = std::min(color.g * strength, kMaxIntensity);
            color.b = std::min(color.b * strength, kMaxIntensity);
            color.a = std::clamp(color.a * strength, 0.0f, 1.0f);
            return color;
        };
        const auto with_opacity = [](Color color, float opacity) {
            color.a *= opacity;
            return color;
        };
        const float diagonal_extent = std::sqrt(1.0f +
            std::pow(std::min(canvas_size.x, canvas_size.y) /
                     std::max(canvas_size.x, canvas_size.y), 2.0f));
        const float radial_radius = std::max(
            profile.spread * diagonal_extent, 0.001f);
        const float sweep_length = std::max(1.0f,
            std::abs(std::cos(profile.angle * 0.01745329251994329577f)) +
            std::abs(std::sin(profile.angle * 0.01745329251994329577f)));

        switch (profile.shape) {
            case LightLeakShape::Edge: {
                Vec2 edge_center{};
                switch (profile.edge) {
                    case LightLeakEdge::Left: edge_center = {0.0f, profile.position}; break;
                    case LightLeakEdge::Top: edge_center = {profile.position, 0.0f}; break;
                    case LightLeakEdge::Right: edge_center = {1.0f, profile.position}; break;
                    case LightLeakEdge::Bottom: edge_center = {profile.position, 1.0f}; break;
                }
                const Color peak = with_intensity(a);
                shape.gradient = graphics::GradientDefinition::radial(
                    edge_center, radial_radius,
                    {{0.0f, peak}, {1.0f, clear}});
                break;
            }
            case LightLeakShape::Corner: {
                constexpr std::array<Vec2, 4> corners{
                    Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f},
                    Vec2{0.0f, 1.0f}, Vec2{1.0f, 1.0f}};
                const auto corner = static_cast<std::size_t>(profile.corner);
                const Color peak = with_intensity(a);
                shape.gradient = graphics::GradientDefinition::radial(
                    corners[corner], radial_radius,
                    {{0.0f, peak}, {1.0f, clear}});
                break;
            }
            case LightLeakShape::Radial: {
                const Color color_a = with_intensity(a);
                const Color color_b = with_intensity(b);
                shape.gradient = graphics::GradientDefinition::radial(
                    center, radial_radius,
                    {{0.0f, color_a}, {0.55f, color_b}, {1.0f, clear}});
                break;
            }
            case LightLeakShape::Sweep:
            case LightLeakShape::Band: {
                const float radians = profile.angle *
                    (3.14159265358979323846f / 180.0f);
                const Vec2 tangent{std::cos(radians), std::sin(radians)};
                const Vec2 normal{-tangent.y, tangent.x};
                const Vec2 band_center = Vec2{0.5f, 0.5f} + normal *
                    ((profile.position - 0.5f) * sweep_length);
                const float half_width = std::max(profile.width, 0.001f) * 0.5f;
                const float half_width_t = std::clamp(
                    half_width / (2.0f * sweep_length), 0.001f, 0.499f);
                const float extension = 1.5f * sweep_length;
                const Vec2 start = band_center - normal * extension;
                const Vec2 end = band_center + normal * extension;
                const float band_opacity = profile.shape == LightLeakShape::Band
                    ? 1.0f : 0.5f + 0.5f * std::clamp(profile.spread, 0.0f, 1.0f);
                Color band_color = with_intensity(with_opacity(a, band_opacity));
                shape.gradient = graphics::GradientDefinition::linear(
                    start, end,
                    {{0.0f, clear}, {0.5f - half_width_t, clear},
                     {0.5f, band_color}, {0.5f + half_width_t, clear}, {1.0f, clear}});
                break;
            }
        }
        layer.shape = std::move(shape);
        if (profile.intensity == 0.0f) return layer;

        if (profile.softness > 0.0f) {
            LayerPlan::EffectPlan blur;
            blur.kind = LayerPlan::EffectKindPlan::GaussianBlur;
            blur.radius = std::min(profile.softness *
                std::min(canvas_size.x, canvas_size.y) * 0.10f, 256.0f);
            layer.effects.push_back(std::move(blur));
        }
        if (profile.chroma_split > 0.0f) {
            LayerPlan::EffectPlan split;
            split.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
            split.chromatic_red_offset = profile.chroma_split;
            split.chromatic_blue_offset = -profile.chroma_split;
            layer.effects.push_back(std::move(split));
        }
        const auto append_noise = [&](float amount, std::uint32_t seed, bool animated) {
            if (amount <= 0.0f) return;
            LayerPlan::EffectPlan noise;
            noise.kind = LayerPlan::EffectKindPlan::Noise;
            noise.amount = amount;
            noise.seed = seed;
            noise.animated = animated;
            noise.rgb_noise = true;
            noise.noise_size = 1.0f;
            layer.effects.push_back(std::move(noise));
        };
        // Keep the seeded film grain spatially stable even when the caller
        // separately requests frame-varying flicker.
        append_noise(profile.grain, profile.seed, false);
        append_noise(profile.flicker, profile.seed ^ 0x9e3779b9U, true);
        // Chroma separation precedes grain so seeded noise does not decorrelate
        // channel displacement or add an unrequested fringe of its own.
        return layer;
    }

    /// Resolve a bounded emitter set at an explicit timestamp. Position and
    /// intensity are pure functions of time, so random-access frames match a
    /// sequential render.
    [[nodiscard]] static std::vector<LayerPlan> resolve_many(
        const std::vector<AnimatedLightLeakEmitter>& emitters,
        Vec2 canvas_size, double time_seconds) {
        if (emitters.empty() || emitters.size() > 8 ||
            !std::isfinite(time_seconds) || std::abs(time_seconds) > 1.0e9)
            throw std::invalid_argument("light leak emitter set or time is outside supported bounds");
        std::vector<LayerPlan> layers;
        layers.reserve(emitters.size());
        std::vector<std::string> ids;
        ids.reserve(emitters.size());
        for (const auto& emitter : emitters) {
            if (emitter.id.empty() || !std::isfinite(emitter.velocity.x) ||
                !std::isfinite(emitter.velocity.y) || std::abs(emitter.velocity.x) > 100.0f ||
                std::abs(emitter.velocity.y) > 100.0f ||
                !std::isfinite(emitter.pulse_amount) || emitter.pulse_amount < 0.0f ||
                emitter.pulse_amount > 1.0f || !std::isfinite(emitter.pulse_speed) ||
                std::abs(emitter.pulse_speed) > 1000.0f || !std::isfinite(emitter.phase))
                throw std::invalid_argument("light leak emitter animation is outside supported bounds");
            if (std::find(ids.begin(), ids.end(), emitter.id) != ids.end())
                throw std::invalid_argument("light leak emitter ids must be unique");
            ids.push_back(emitter.id);
            auto profile = emitter.profile;
            profile.position += emitter.velocity.x * static_cast<float>(time_seconds);
            profile.position_y += emitter.velocity.y * static_cast<float>(time_seconds);
            const float pulse = 1.0f + emitter.pulse_amount * std::sin(
                emitter.phase + emitter.pulse_speed * static_cast<float>(time_seconds));
            profile.intensity = std::clamp(profile.intensity * pulse, 0.0f, kMaxIntensity);
            layers.push_back(resolve(profile, canvas_size, emitter.id));
        }
        return layers;
    }
};

/// A soft, warm emitter centered on the frame. The radial fill is generated
/// by LightLeakResolver and remains editable through the ordinary RenderPlan
/// shape and effect contracts.
[[nodiscard]] inline LayerPlan cinematic_soft_blob(
    Vec2 canvas_size, float intensity = 1.0f,
    Color inner = kWarmTint, Color outer = kAmberTint,
    std::uint32_t seed = 0, std::string id = "cinematic_soft_blob") {
    LightLeakProfile profile;
    profile.shape = LightLeakShape::Radial;
    profile.blend = LightLeakBlend::Screen;
    profile.color_a = inner;
    profile.color_b = outer;
    profile.position = 0.5f;
    profile.position_y = 0.5f;
    profile.spread = 0.72f;
    profile.softness = 0.22f;
    profile.intensity = intensity;
    profile.grain = 0.035f;
    profile.seed = seed;
    auto layer = LightLeakResolver::resolve(profile, canvas_size, std::move(id));
    if (intensity > 0.0f) {
        LayerPlan::EffectPlan bloom;
        bloom.kind = LayerPlan::EffectKindPlan::Bloom;
        bloom.bloom_threshold = 0.58f;
        bloom.bloom_intensity = std::min(0.18f * intensity, 0.72f);
        bloom.radius = 32.0f;
        layer.effects.push_back(std::move(bloom));
    }
    return layer;
}

/// Large emitter anchored to a selected frame edge. Position is normalized
/// along that edge, so callers can animate it with ordinary layer tracks.
[[nodiscard]] inline LayerPlan cinematic_edge_burn(
    Vec2 canvas_size, LightLeakEdge edge = LightLeakEdge::Right,
    float position = 0.5f, float intensity = 1.0f,
    Color tint = kWarmTint, std::uint32_t seed = 0,
    std::string id = "cinematic_edge_burn") {
    LightLeakProfile profile;
    profile.shape = LightLeakShape::Edge;
    profile.edge = edge;
    profile.position = position;
    profile.position_y = position;
    profile.spread = 0.82f;
    profile.softness = 0.3f;
    profile.intensity = intensity;
    profile.color_a = tint;
    profile.blend = LightLeakBlend::Screen;
    profile.grain = 0.04f;
    profile.seed = seed;
    return LightLeakResolver::resolve(profile, canvas_size, std::move(id));
}

/// Diagonal broad streak suitable for the moving ribbon/sweep portion of a
/// cinematic background. Motion is authored by normal layer transforms.
[[nodiscard]] inline LayerPlan cinematic_diagonal_streak(
    Vec2 canvas_size, float angle = 35.0f, float position = 0.5f,
    float width = 0.2f, float intensity = 0.8f,
    Color tint = kAmberTint, std::string id = "cinematic_diagonal_streak") {
    LightLeakProfile profile;
    profile.shape = LightLeakShape::Band;
    profile.angle = angle;
    profile.position = position;
    profile.width = width;
    profile.spread = 0.8f;
    profile.softness = 0.18f;
    profile.intensity = intensity;
    profile.color_a = tint;
    profile.blend = LightLeakBlend::Add;
    auto layer = LightLeakResolver::resolve(profile, canvas_size, std::move(id));
    if (intensity > 0.0f) {
        LayerPlan::EffectPlan blur;
        blur.kind = LayerPlan::EffectKindPlan::GaussianBlur;
        blur.radius = std::min(std::min(canvas_size.x, canvas_size.y) * 0.035f, 96.0f);
        layer.effects.push_back(std::move(blur));
    }
    return layer;
}

[[nodiscard]] inline LayerPlan cinematic_bezier_ribbon(
    Vec2 canvas_size, Color tint = kAmberTint, float width_fraction = 0.12f,
    float softness = 0.8f, std::string id = "cinematic_bezier_ribbon") {
    if (!std::isfinite(canvas_size.x) || !std::isfinite(canvas_size.y) ||
        canvas_size.x <= 0.0f || canvas_size.y <= 0.0f ||
        canvas_size.x > 1000000.0f || canvas_size.y > 1000000.0f ||
        !std::isfinite(width_fraction) || width_fraction <= 0.0f || width_fraction > 0.5f ||
        !std::isfinite(softness) || softness < 0.0f || softness > 1.0f)
        throw std::invalid_argument("cinematic Bezier ribbon inputs are outside supported bounds");
    LightLeakProfile color_check;
    color_check.color_a = tint;
    if (!color_check.valid())
        throw std::invalid_argument("cinematic Bezier ribbon tint must be an SDR color");

    LayerPlan layer;
    layer.id = std::move(id);
    layer.type = LayerType::Shape;
    layer.size = {canvas_size.x, canvas_size.y};
    layer.size_dimensions = 2;
    layer.position = {canvas_size.x * 0.5f, canvas_size.y * 0.5f, 0.0f};
    layer.position_dimensions = 2;
    layer.screen_space = true;
    layer.blend_mode = BlendMode::Add;
    layer.color = {tint.r, tint.g, tint.b, tint.a};

    ShapeContentPlan shape;
    shape.type = ShapeContentTypePlan::Path;
    ShapePathCommandPlan move;
    move.type = ShapePathCommandTypePlan::MoveTo;
    move.point = {0.0f, canvas_size.y * 0.62f};
    ShapePathCommandPlan curve;
    curve.type = ShapePathCommandTypePlan::CubicTo;
    curve.point = {canvas_size.x, canvas_size.y * 0.38f};
    curve.control1 = {canvas_size.x * 0.28f, canvas_size.y * 0.06f};
    curve.control2 = {canvas_size.x * 0.72f, canvas_size.y * 0.94f};
    shape.path = {move, curve};
    StrokeStyle stroke;
    stroke.width = std::min(canvas_size.x, canvas_size.y) * width_fraction;
    stroke.solid_color = std::array<float, 4>{tint.r, tint.g, tint.b, tint.a};
    stroke.cap = "round";
    stroke.join = "round";
    graphics::BrushProfile brush;
    brush.size = *stroke.width;
    brush.hardness = 1.0f - softness;
    brush.edge_softness = softness;
    stroke.brush = std::move(brush);
    shape.stroke = std::move(stroke);
    layer.shape = std::move(shape);

    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = 0.5f;
    bloom.bloom_intensity = 0.55f;
    bloom.radius = std::min(std::min(canvas_size.x, canvas_size.y) * 0.06f, 96.0f);
    layer.effects.push_back(std::move(bloom));
    return layer;
}

inline void require_intensity(float intensity) {
    if (!std::isfinite(intensity) || intensity < 0.0f || intensity > kMaxIntensity)
        throw std::invalid_argument("light leak intensity must be in [0, 4]");
}

inline void require_radius(float radius) {
    if (!std::isfinite(radius) || radius < 0.0f || radius > 256.0f)
        throw std::invalid_argument("light leak radius must be in [0, 256]");
}

/// Warm light bleeding in from one frame edge: luminance response (screen)
/// plus a matching warm glow.
inline void edge_warm(LayerPlan& layer, float intensity = 1.0f,
                      Color tint = kWarmTint, float radius = 28.0f) {
    require_intensity(intensity);
    require_radius(radius);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan response;
    response.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    response.response_threshold = 0.55f;
    response.response_softness = 0.35f;
    response.response_radius = radius;
    response.response_tint = {tint.r, tint.g, tint.b, tint.a};
    response.response_intensity = std::min(intensity, 2.0f);
    response.response_blend = 0; // Screen
    layer.effects.push_back(std::move(response));

    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = std::min(radius * 1.5f, 256.0f);
    glow.glow_intensity = std::min(0.30f * intensity, 1.2f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));
}

/// Bloom growing from one corner: god rays anchored at the corner origin
/// plus a low-threshold bloom.
inline void corner_bloom(LayerPlan& layer, float intensity = 1.0f,
                         int corner = 0, Color tint = kAmberTint) {
    require_intensity(intensity);
    if (corner < 0 || corner > 3)
        throw std::invalid_argument("light leak corner must be 0..3 (TL, TR, BL, BR)");
    if (intensity <= 0.0f) return;
    const std::array<Vec2, 4> origins{Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f},
                                      Vec2{0.0f, 1.0f}, Vec2{1.0f, 1.0f}};
    LayerPlan::EffectPlan rays;
    rays.kind = LayerPlan::EffectKindPlan::LightRays;
    rays.light_rays_origin = {origins[corner].x, origins[corner].y};
    rays.light_rays_length = 420.0f;
    rays.light_rays_density = std::min(0.35f + 0.15f * intensity, 0.95f);
    rays.light_rays_color = {tint.r, tint.g, tint.b, tint.a};
    rays.light_rays_decay = 0.90f;
    layer.effects.push_back(std::move(rays));

    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = 0.55f;
    bloom.bloom_intensity = std::min(0.45f * intensity, 1.8f);
    bloom.radius = 32.0f;
    layer.effects.push_back(std::move(bloom));
}

/// Luminous band sweeping horizontally: warm response plus a long horizontal
/// streak convolution (the band motion itself is plan-level animation).
inline void horizontal_sweep(LayerPlan& layer, float intensity = 1.0f,
                             Color tint = kWarmTint) {
    require_intensity(intensity);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan response;
    response.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    response.response_threshold = 0.60f;
    response.response_softness = 0.30f;
    response.response_radius = 24.0f;
    response.response_tint = {tint.r, tint.g, tint.b, tint.a};
    response.response_intensity = std::min(intensity, 2.5f);
    response.response_blend = 0; // Screen
    layer.effects.push_back(std::move(response));

    LayerPlan::EffectPlan streak;
    streak.kind = LayerPlan::EffectKindPlan::DirectionalBlur;
    streak.directional_angle = 0.0f;
    streak.directional_length = 96.0f;
    streak.directional_samples = 32;
    layer.effects.push_back(std::move(streak));
}

/// Diagonal variant of the sweep at 45 degrees.
inline void diagonal_sweep(LayerPlan& layer, float intensity = 1.0f,
                           Color tint = kAmberTint) {
    require_intensity(intensity);
    if (intensity <= 0.0f) return;
    horizontal_sweep(layer, intensity, tint);
    LayerPlan::EffectPlan streak = layer.effects.back();
    layer.effects.pop_back();
    streak.directional_angle = 45.0f;
    layer.effects.push_back(std::move(streak));
}

/// Two opposed leaks: symmetric warm/cool response pair meeting mid-frame.
inline void dual_edge(LayerPlan& layer, float intensity = 1.0f) {
    require_intensity(intensity);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan warm;
    warm.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    warm.response_threshold = 0.58f;
    warm.response_softness = 0.32f;
    warm.response_radius = 26.0f;
    warm.response_tint = {kWarmTint.r, kWarmTint.g, kWarmTint.b, kWarmTint.a};
    warm.response_intensity = std::min(intensity, 2.0f);
    warm.response_blend = 0;
    layer.effects.push_back(std::move(warm));

    LayerPlan::EffectPlan cool;
    cool.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    cool.response_threshold = 0.62f;
    cool.response_softness = 0.30f;
    cool.response_radius = 26.0f;
    cool.response_tint = {kCoolTint.r, kCoolTint.g, kCoolTint.b, kCoolTint.a};
    cool.response_intensity = std::min(0.8f * intensity, 2.0f);
    cool.response_blend = 0;
    layer.effects.push_back(std::move(cool));
}

/// Elegant prismatic edge: a small constant RGB dispersion (2 px per channel
/// by default) confined to the luminous zone by the response mask.
inline void prismatic(LayerPlan& layer, float intensity = 1.0f,
                      float dispersion_px = 2.0f) {
    require_intensity(intensity);
    if (!std::isfinite(dispersion_px) || dispersion_px < 0.0f || dispersion_px > 16.0f)
        throw std::invalid_argument("light leak prismatic dispersion must be in [0, 16] px");
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan response;
    response.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    response.response_threshold = 0.65f;
    response.response_softness = 0.25f;
    response.response_radius = 20.0f;
    response.response_tint = {1.0f, 1.0f, 1.0f, 1.0f};
    response.response_intensity = std::min(intensity, 2.0f);
    response.response_blend = 0;
    layer.effects.push_back(std::move(response));

    LayerPlan::EffectPlan split;
    split.kind = LayerPlan::EffectKindPlan::ChromaticAberration;
    split.chromatic_red_offset = dispersion_px;
    split.chromatic_blue_offset = -dispersion_px;
    layer.effects.push_back(std::move(split));
}

/// Pulse look: one warm glow whose breathing is driven plan-level through the
/// light.glow intensity parameter track. The recipe pins the warm tint and a
/// safe radius; the animation owns the on-grow-off curve.
inline void pulse(LayerPlan& layer, float peak_intensity = 1.0f,
                  Color tint = kWarmTint, float radius = 48.0f) {
    require_intensity(peak_intensity);
    require_radius(radius);
    if (peak_intensity <= 0.0f) return;
    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = radius;
    glow.glow_intensity = std::min(0.35f * peak_intensity, 1.4f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));
}

/// Film burn: intense warm response over a low threshold (additive) plus
/// bloom — bright enough to read as a transition matte between scenes.
inline void film_burn(LayerPlan& layer, float intensity = 2.0f,
                      float threshold = 0.35f, Color tint = kWarmTint) {
    require_intensity(intensity);
    if (!std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f)
        throw std::invalid_argument("light leak film burn threshold must be in [0, 1]");
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan burn;
    burn.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    burn.response_threshold = threshold;
    burn.response_softness = 0.22f;
    burn.response_radius = 30.0f;
    burn.response_tint = {tint.r, tint.g, tint.b, tint.a};
    burn.response_intensity = std::min(intensity, kMaxIntensity);
    burn.response_blend = 1; // Add — the burn must lift to white
    layer.effects.push_back(std::move(burn));

    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = threshold + 0.10f;
    bloom.bloom_intensity = std::min(0.5f * intensity, 2.0f);
    bloom.radius = 24.0f;
    layer.effects.push_back(std::move(bloom));
}

/// Soft wide flare: very diffuse warm halation without any streak.
inline void soft_flare(LayerPlan& layer, float intensity = 1.0f,
                       Color tint = kAmberTint, float radius = 96.0f) {
    require_intensity(intensity);
    require_radius(radius);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan response;
    response.kind = LayerPlan::EffectKindPlan::LuminanceResponse;
    response.response_threshold = 0.45f;
    response.response_softness = 0.45f;
    response.response_radius = radius;
    response.response_tint = {tint.r, tint.g, tint.b, tint.a};
    response.response_intensity = std::min(intensity, 2.0f);
    response.response_blend = 0; // Screen
    layer.effects.push_back(std::move(response));

    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = std::min(radius, 256.0f);
    glow.glow_intensity = std::min(0.25f * intensity, 1.0f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));
}

/// Focus glow behind a selected subject: a soft radial emphasis (large warm
/// glow plus a gentle bloom) that plans combine with the subject's own
/// 1.00 -> 1.02 scale emphasis.
inline void focus_glow(LayerPlan& layer, float intensity = 1.0f,
                       Color tint = kAmberTint) {
    require_intensity(intensity);
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = 64.0f;
    glow.glow_intensity = std::min(0.30f * intensity, 1.2f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));

    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = 0.70f;
    bloom.bloom_intensity = std::min(0.30f * intensity, 1.2f);
    bloom.radius = 20.0f;
    layer.effects.push_back(std::move(bloom));
}

/// Bokeh pass bonus: radial blur sampling plus a soft glow — background
/// lights melt into round bokeh discs.
inline void bokeh_pass(LayerPlan& layer, float intensity = 1.0f,
                       float amount = 0.35f, Color tint = kAmberTint) {
    require_intensity(intensity);
    if (!std::isfinite(amount) || amount < 0.0f || amount > 1.0f)
        throw std::invalid_argument("light leak bokeh amount must be in [0, 1]");
    if (intensity <= 0.0f) return;
    LayerPlan::EffectPlan blur;
    blur.kind = LayerPlan::EffectKindPlan::RadialBlur;
    blur.radial_center = {0.5f, 0.5f};
    blur.radial_amount = amount * 0.5f;
    blur.radial_render_samples = 16;
    layer.effects.push_back(std::move(blur));

    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = 40.0f;
    glow.glow_intensity = std::min(0.30f * intensity, 1.2f);
    glow.effect_color = {tint.r, tint.g, tint.b, tint.a};
    layer.effects.push_back(std::move(glow));
}

} // namespace light_leak

/// Procedural abstract backgrounds composed from canonical Field2D, gradient,
/// shape and effect contracts. These recipes author layers only; rendering,
/// blending and animation continue through the normal RenderPlan pipeline.
namespace abstract_background {

using namespace light_leak;

inline LayerPlan field_layer(Vec2 canvas_size, std::string id, Color tint,
                             graphics::Field2D field,
                             BlendMode blend = BlendMode::Screen,
                             std::uint32_t render_scale = 4,
                             std::optional<graphics::GradientDefinition> field_ramp = std::nullopt) {
    const auto channel_valid = [](float channel) {
        return std::isfinite(channel) && channel >= 0.0f && channel <= 1.0f;
    };
    if (!std::isfinite(canvas_size.x) || !std::isfinite(canvas_size.y) ||
        canvas_size.x <= 0.0f || canvas_size.y <= 0.0f ||
        canvas_size.x > 1000000.0f || canvas_size.y > 1000000.0f ||
        (render_scale != 1 && render_scale != 2 && render_scale != 4) ||
        !channel_valid(tint.r) || !channel_valid(tint.g) ||
        !channel_valid(tint.b) || !channel_valid(tint.a) ||
        !std::isfinite(field.frequency) || field.frequency <= 0.0f ||
        field.frequency > 4096.0f ||
        (field.generator == graphics::Field2DGenerator::Fractal &&
         (field.octaves == 0 || field.octaves > 16 ||
          !std::isfinite(field.persistence) || field.persistence < 0.0f ||
          field.persistence > 1.0f || !std::isfinite(field.lacunarity) ||
          field.lacunarity <= 0.0f || field.lacunarity > 8.0f)))
        throw std::invalid_argument("abstract background inputs are outside supported bounds");
    LayerPlan layer;
    layer.id = std::move(id);
    layer.type = LayerType::Shape;
    layer.size = {canvas_size.x, canvas_size.y};
    layer.size_dimensions = 2;
    layer.position = {canvas_size.x * 0.5f, canvas_size.y * 0.5f, 0.0f};
    layer.position_dimensions = 2;
    layer.screen_space = true;
    layer.blend_mode = blend;
    layer.color = {tint.r, tint.g, tint.b, tint.a};
    ShapeContentPlan shape;
    shape.type = ShapeContentTypePlan::Rect;
    shape.fill_color = std::array<float, 4>{tint.r, tint.g, tint.b, tint.a};
    shape.field = std::move(field);
    shape.field_render_scale = render_scale;
    shape.field_ramp = std::move(field_ramp);
    layer.shape = std::move(shape);
    return layer;
}

inline graphics::Field2D warped_field(std::uint32_t seed, float frequency,
                                      float warp, std::uint32_t octaves = 4) {
    graphics::Field2D field;
    field.generator = graphics::Field2DGenerator::Fractal;
    field.seed = seed;
    field.frequency = frequency;
    field.octaves = octaves;
    if (warp > 0.0f) {
        graphics::Field2DOperator op;
        op.kind = graphics::Field2DOperatorKind::DomainWarp;
        op.amount = warp;
        field.operators.push_back(op);
    }
    return field;
}

inline graphics::GradientDefinition color_ramp(std::vector<graphics::GradientStop> stops) {
    return graphics::GradientDefinition::linear({0.0f, 0.5f}, {1.0f, 0.5f},
                                                  std::move(stops));
}

inline graphics::GradientDefinition neon_ramp() {
    return color_ramp({{0.0f, Color{0.015f, 0.025f, 0.16f, 1.0f}},
                       {0.32f, Color{0.24f, 0.035f, 0.58f, 1.0f}},
                       {0.62f, Color{0.88f, 0.055f, 0.68f, 1.0f}},
                       {0.82f, Color{0.12f, 0.7f, 0.94f, 1.0f}},
                       {1.0f, Color{1.0f, 0.68f, 0.92f, 1.0f}}});
}

inline graphics::GradientDefinition magenta_ramp() {
    return color_ramp({{0.0f, Color{0.08f, 0.008f, 0.22f, 1.0f}},
                       {0.42f, Color{0.56f, 0.025f, 0.48f, 1.0f}},
                       {0.78f, Color{0.98f, 0.12f, 0.62f, 1.0f}},
                       {1.0f, Color{1.0f, 0.72f, 0.9f, 1.0f}}});
}

inline graphics::GradientDefinition cyan_ramp() {
    return color_ramp({{0.0f, Color{0.008f, 0.035f, 0.16f, 1.0f}},
                       {0.38f, Color{0.025f, 0.2f, 0.62f, 1.0f}},
                       {0.74f, Color{0.04f, 0.72f, 0.86f, 1.0f}},
                       {1.0f, Color{0.68f, 0.98f, 1.0f, 1.0f}}});
}

inline std::vector<LayerPlan> aurora_horizon(Vec2 canvas_size,
                                              std::uint32_t seed = 1) {
    std::vector<LayerPlan> layers;
    LightLeakProfile base;
    base.shape = LightLeakShape::Band;
    base.angle = 90.0f;
    base.position = 0.72f;
    base.width = 0.42f;
    base.spread = 0.75f;
    base.softness = 0.5f;
    base.color_a = Color{0.34f, 0.08f, 0.72f, 0.72f};
    base.color_b = Color{0.95f, 0.32f, 0.74f, 1.0f};
    base.seed = seed;
    layers.push_back(LightLeakResolver::resolve(base, canvas_size, "aurora_horizon"));
    auto field = warped_field(seed, 2.2f, 0.16f, 5);
    layers.push_back(field_layer(canvas_size, "aurora_horizon_ridge",
        Color{0.64f, 0.22f, 0.9f, 0.38f}, std::move(field)));
    return layers;
}

inline std::vector<LayerPlan> liquid_neon(Vec2 canvas_size,
                                          std::uint32_t seed = 7,
                                          Color tint = Color{1.0f, 1.0f, 1.0f, 1.0f},
                                          std::optional<graphics::GradientDefinition> ramp = neon_ramp()) {
    std::vector<LayerPlan> layers;
    layers.push_back(field_layer(canvas_size, "liquid_neon_field_a",
        tint, warped_field(seed, 2.0f, 0.22f, 5), BlendMode::Screen, 4, ramp));
    layers.push_back(field_layer(canvas_size, "liquid_neon_field_b",
        Color{0.12f, 0.55f, 0.95f, 0.44f},
        warped_field(seed ^ 0x9e3779b9U, 3.7f, 0.12f, 4), BlendMode::Screen, 4, ramp));
    return layers;
}

inline std::vector<LayerPlan> liquid_magenta(Vec2 canvas_size,
                                             std::uint32_t seed = 43) {
    return liquid_neon(canvas_size, seed, Color{0.92f, 0.08f, 0.62f, 0.86f}, magenta_ramp());
}

inline std::vector<LayerPlan> liquid_cyan(Vec2 canvas_size,
                                           std::uint32_t seed = 47) {
    return liquid_neon(canvas_size, seed, Color{0.04f, 0.62f, 0.9f, 0.82f}, cyan_ramp());
}

inline std::vector<LayerPlan> metaball_soft(Vec2 canvas_size,
                                             std::uint32_t seed = 11) {
    auto field = warped_field(seed, 3.0f, 0.05f, 2);
    field.generator = graphics::Field2DGenerator::Cellular;
    graphics::Field2DOperator soften;
    soften.kind = graphics::Field2DOperatorKind::Smoothstep;
    soften.edge = 0.42f;
    soften.softness = 0.28f;
    field.operators.push_back(soften);
    return {field_layer(canvas_size, "metaball_soft", Color{0.48f, 0.12f, 0.82f, 0.72f},
                        std::move(field))};
}

inline std::vector<LayerPlan> metaball_neon(Vec2 canvas_size,
                                             std::uint32_t seed = 53) {
    auto layers = metaball_soft(canvas_size, seed);
    layers.front().shape->fill_color = std::array<float, 4>{0.92f, 0.08f, 0.68f, 0.9f};
    layers.front().color = {0.92f, 0.08f, 0.68f, 0.9f};
    LayerPlan::EffectPlan glow;
    glow.kind = LayerPlan::EffectKindPlan::Glow;
    glow.radius = 42.0f;
    glow.glow_intensity = 0.8f;
    glow.effect_color = {0.7f, 0.16f, 1.0f, 1.0f};
    layers.front().effects.push_back(std::move(glow));
    return layers;
}

inline std::vector<LayerPlan> glass_orb(Vec2 canvas_size,
                                         std::uint32_t seed = 17) {
    const float diameter = std::min(canvas_size.x, canvas_size.y) * 0.56f;
    LightLeakProfile profile;
    profile.shape = LightLeakShape::Radial;
    profile.position = 0.5f;
    profile.position_y = 0.5f;
    profile.spread = 0.62f;
    profile.softness = 0.18f;
    profile.color_a = Color{0.72f, 0.24f, 1.0f, 0.72f};
    profile.color_b = Color{0.96f, 0.4f, 0.82f, 0.62f};
    profile.seed = seed;
    auto orb = LightLeakResolver::resolve(profile, canvas_size, "glass_orb");
    orb.size = {diameter, diameter};
    orb.position = {canvas_size.x * 0.5f, canvas_size.y * 0.48f, 0.0f};
    orb.shape->type = ShapeContentTypePlan::Ellipse;
    auto optical = BackdropFilterPlan::glass_refractive();
    optical.refraction_field = warped_field(seed ^ 0xa511e9b3U, 3.2f, 0.035f, 3);
    optical.rim_strength = 1.1f;
    optical.rim_width = std::min(diameter * 0.04f, 12.0f);
    optical.validate();
    orb.backdrop_filter = std::move(optical);
    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = 0.56f;
    bloom.bloom_intensity = 0.45f;
    bloom.radius = 28.0f;
    orb.effects.push_back(std::move(bloom));
    return {std::move(orb)};
}

inline std::vector<LayerPlan> dual_orbs(Vec2 canvas_size,
                                         std::uint32_t seed = 19) {
    auto layers = glass_orb(canvas_size, seed);
    LightLeakProfile second;
    second.shape = LightLeakShape::Radial;
    second.position = 0.72f;
    second.position_y = 0.38f;
    second.spread = 0.28f;
    second.softness = 0.2f;
    second.color_a = Color{0.12f, 0.7f, 1.0f, 0.72f};
    second.color_b = Color{0.45f, 0.84f, 1.0f, 0.58f};
    second.seed = seed ^ 0x85ebca6bU;
    layers.push_back(LightLeakResolver::resolve(second, canvas_size, "dual_orbs_cool"));
    return layers;
}

inline std::vector<LayerPlan> topographic_contours(Vec2 canvas_size,
                                                    std::uint32_t seed = 23,
                                                    float frequency = 8.0f,
                                                    float line_width = 0.08f) {
    if (!std::isfinite(frequency) || frequency < 1.0f || frequency > 128.0f ||
        !std::isfinite(line_width) || line_width < 0.01f || line_width > 0.4f)
        throw std::invalid_argument("contour frequency/line width is outside supported bounds");
    graphics::Field2D field;
    field.generator = graphics::Field2DGenerator::Rings;
    field.seed = seed;
    field.frequency = frequency;
    graphics::Field2DOperator warp;
    warp.kind = graphics::Field2DOperatorKind::DomainWarp;
    warp.amount = 0.12f;
    field.operators.push_back(warp);
    graphics::Field2DOperator isolate_lines;
    isolate_lines.kind = graphics::Field2DOperatorKind::Smoothstep;
    isolate_lines.edge = 1.0f - line_width * 0.5f;
    isolate_lines.softness = line_width;
    field.operators.push_back(isolate_lines);
    return {field_layer(canvas_size, "topographic_contours",
                        Color{0.86f, 0.91f, 1.0f, 0.68f}, std::move(field), BlendMode::Screen)};
}

inline std::vector<LayerPlan> cinematic_warped_ring(Vec2 canvas_size,
                                                     std::uint32_t seed = 59,
                                                     Color tint = Color{1.0f, 0.38f, 0.12f, 0.9f}) {
    graphics::Field2D field;
    field.generator = graphics::Field2DGenerator::Rings;
    field.frequency = 1.6f;
    field.seed = seed;
    graphics::Field2DOperator warp;
    warp.kind = graphics::Field2DOperatorKind::DomainWarp;
    warp.amount = 0.11f;
    field.operators.push_back(warp);
    graphics::Field2DOperator isolate;
    isolate.kind = graphics::Field2DOperatorKind::Threshold;
    isolate.edge = 0.92f;
    field.operators.push_back(isolate);
    auto ring = field_layer(canvas_size, "cinematic_warped_ring", tint,
                            std::move(field), BlendMode::Add);
    LayerPlan::EffectPlan blur;
    blur.kind = LayerPlan::EffectKindPlan::GaussianBlur;
    blur.radius = 14.0f;
    ring.effects.push_back(std::move(blur));
    return {std::move(ring)};
}

inline std::vector<LayerPlan> cinematic_radial_burst(
    Vec2 canvas_size, std::uint32_t lobes = 8,
    Color tint = Color{1.0f, 0.58f, 0.12f, 0.9f}) {
    const auto channel_valid = [](float channel) {
        return std::isfinite(channel) && channel >= 0.0f && channel <= 1.0f;
    };
    if (lobes < 5 || lobes > 12 || !std::isfinite(canvas_size.x) ||
        !std::isfinite(canvas_size.y) || canvas_size.x <= 0.0f || canvas_size.y <= 0.0f ||
        canvas_size.x > 1000000.0f || canvas_size.y > 1000000.0f ||
        !channel_valid(tint.r) || !channel_valid(tint.g) ||
        !channel_valid(tint.b) || !channel_valid(tint.a))
        throw std::invalid_argument("cinematic burst inputs are outside supported bounds");
    graphics::Field2D field;
    field.generator = graphics::Field2DGenerator::Burst;
    field.seed = 0;
    field.frequency = 1.0f;
    field.octaves = lobes;
    auto layer = field_layer(canvas_size, "cinematic_radial_burst", tint,
                             std::move(field), BlendMode::Add);
    LayerPlan::EffectPlan blur;
    blur.kind = LayerPlan::EffectKindPlan::GaussianBlur;
    blur.radius = std::min(std::min(canvas_size.x, canvas_size.y) * 0.025f, 64.0f);
    layer.effects.push_back(std::move(blur));
    LayerPlan::EffectPlan bloom;
    bloom.kind = LayerPlan::EffectKindPlan::Bloom;
    bloom.bloom_threshold = 0.42f;
    bloom.bloom_intensity = 0.72f;
    bloom.radius = 36.0f;
    layer.effects.push_back(std::move(bloom));
    return {std::move(layer)};
}

inline std::vector<LayerPlan> cinematic_dual_blob(Vec2 canvas_size,
                                                   std::uint32_t seed = 73) {
    auto layers = liquid_magenta(canvas_size, seed);
    LightLeakProfile cool;
    cool.shape = LightLeakShape::Radial;
    cool.position = 0.72f;
    cool.position_y = 0.35f;
    cool.spread = 0.32f;
    cool.softness = 0.28f;
    cool.color_a = Color{0.08f, 0.44f, 1.0f, 0.8f};
    cool.color_b = Color{0.22f, 0.82f, 1.0f, 0.6f};
    cool.seed = seed ^ 0x9e3779b9U;
    layers.push_back(LightLeakResolver::resolve(cool, canvas_size, "cinematic_dual_blob_cool"));
    return layers;
}

inline std::vector<LayerPlan> cinematic_film_burn(Vec2 canvas_size,
                                                   float intensity = 2.0f,
                                                   std::uint32_t seed = 79) {
    light_leak::require_intensity(intensity);
    LightLeakProfile source;
    source.shape = LightLeakShape::Radial;
    source.position = 0.5f;
    source.position_y = 0.5f;
    source.spread = 0.88f;
    source.softness = 0.42f;
    source.intensity = intensity;
    source.color_a = Color{0.64f, 0.04f, 0.01f, 1.0f};
    source.color_b = Color{1.0f, 0.34f, 0.04f, 0.92f};
    source.blend = LightLeakBlend::Add;
    source.grain = 0.06f;
    source.seed = seed;
    auto layer = LightLeakResolver::resolve(source, canvas_size, "cinematic_film_burn");
    light_leak::film_burn(layer, source.intensity, 0.35f, light_leak::kWarmTint);
    return {std::move(layer)};
}

inline std::vector<LayerPlan> plasma_orb(Vec2 canvas_size,
                                         std::uint32_t seed = 61) {
    auto layers = glass_orb(canvas_size, seed);
    auto surface = warped_field(seed ^ 0x9e3779b9U, 6.0f, 0.025f, 3);
    graphics::Field2DOperator edge;
    edge.kind = graphics::Field2DOperatorKind::Threshold;
    edge.edge = 0.72f;
    surface.operators.push_back(edge);
    layers.push_back(field_layer(canvas_size, "plasma_orb_surface",
        Color{0.95f, 0.56f, 1.0f, 0.3f}, std::move(surface), BlendMode::Add));
    return layers;
}

inline std::vector<LayerPlan> flowlines_vertical(Vec2 canvas_size,
                                                  std::uint32_t seed = 29,
                                                  std::uint32_t line_count = 80,
                                                  float displacement = 0.004f,
                                                  bool allow_crossing = false) {
    if (line_count < 4 || line_count > 512 || !std::isfinite(displacement) ||
        displacement < 0.0f || displacement > 0.25f ||
        (!allow_crossing && displacement > 0.5f / static_cast<float>(line_count)))
        throw std::invalid_argument("flowline spacing/displacement is outside supported bounds");
    graphics::Field2D field;
    field.generator = graphics::Field2DGenerator::Stripes;
    field.seed = seed;
    field.frequency = static_cast<float>(line_count);
    graphics::Field2DOperator warp;
    warp.kind = graphics::Field2DOperatorKind::DomainWarp;
    warp.amount = displacement;
    field.operators.push_back(warp);
    return {field_layer(canvas_size, "flowlines_vertical",
                        Color{0.82f, 0.9f, 1.0f, 0.6f}, std::move(field), BlendMode::Screen)};
}

inline std::vector<LayerPlan> flowlines_mesh(Vec2 canvas_size,
                                              std::uint32_t seed = 67,
                                              std::uint32_t line_count = 48,
                                              float displacement = 0.003f,
                                              bool allow_crossing = false) {
    auto vertical = flowlines_vertical(canvas_size, seed, line_count,
                                       displacement, allow_crossing);
    graphics::Field2D horizontal;
    horizontal.generator = graphics::Field2DGenerator::Stripes;
    horizontal.seed = seed ^ 0x85ebca6bU;
    horizontal.frequency = static_cast<float>(line_count);
    horizontal.transform.rotation_radians = 1.57079632679f;
    graphics::Field2DOperator warp;
    warp.kind = graphics::Field2DOperatorKind::DomainWarp;
    warp.amount = displacement;
    horizontal.operators.push_back(warp);
    vertical.push_back(field_layer(canvas_size, "flowlines_mesh_horizontal",
        Color{0.62f, 0.76f, 1.0f, 0.42f}, std::move(horizontal), BlendMode::Screen));
    return vertical;
}

inline std::vector<LayerPlan> folded_ribbon(Vec2 canvas_size,
                                            std::uint32_t seed = 31,
                                            float repeat = 5.0f,
                                            float warp_amount = 0.08f) {
    if (!std::isfinite(repeat) || repeat < 1.0f || repeat > 64.0f ||
        !std::isfinite(warp_amount) || warp_amount < 0.0f || warp_amount > 0.25f)
        throw std::invalid_argument("fold repeat/warp is outside supported bounds");
    graphics::Field2D field;
    field.generator = graphics::Field2DGenerator::Stripes;
    field.seed = seed;
    field.frequency = repeat;
    graphics::Field2DOperator warp;
    warp.kind = graphics::Field2DOperatorKind::DomainWarp;
    warp.amount = warp_amount;
    if (warp_amount > 0.0f) field.operators.push_back(warp);
    return {field_layer(canvas_size, "folded_ribbon",
                        Color{0.54f, 0.26f, 0.9f, 0.8f}, std::move(field), BlendMode::Screen)};
}

inline std::vector<LayerPlan> gradient_fan(Vec2 canvas_size,
                                           std::uint32_t seed = 71) {
    graphics::Field2D field;
    field.generator = graphics::Field2DGenerator::Conic;
    field.frequency = 1.0f;
    field.seed = seed;
    graphics::Field2DOperator warp;
    warp.kind = graphics::Field2DOperatorKind::DomainWarp;
    warp.amount = 0.045f;
    field.operators.push_back(warp);
    return {field_layer(canvas_size, "gradient_fan",
                        Color{0.58f, 0.32f, 0.92f, 0.58f}, std::move(field), BlendMode::Screen)};
}

inline std::vector<LayerPlan> blue_atmosphere(Vec2 canvas_size,
                                               std::uint32_t seed = 37) {
    LightLeakProfile profile;
    profile.shape = LightLeakShape::Radial;
    profile.position = 0.38f;
    profile.position_y = 0.48f;
    profile.spread = 0.9f;
    profile.softness = 0.62f;
    profile.color_a = Color{0.05f, 0.22f, 0.58f, 0.64f};
    profile.color_b = Color{0.08f, 0.62f, 0.92f, 0.38f};
    profile.seed = seed;
    return {LightLeakResolver::resolve(profile, canvas_size, "blue_atmosphere")};
}

inline std::vector<LayerPlan> mega_mix(Vec2 canvas_size,
                                       std::uint32_t seed = 41) {
    auto layers = aurora_horizon(canvas_size, seed);
    auto liquid = liquid_neon(canvas_size, seed ^ 0x9e3779b9U);
    auto orbs = dual_orbs(canvas_size, seed ^ 0x85ebca6bU);
    auto contours = topographic_contours(canvas_size, seed ^ 0xc2b2ae35U);
    layers.insert(layers.end(), std::make_move_iterator(liquid.begin()),
                  std::make_move_iterator(liquid.end()));
    layers.insert(layers.end(), std::make_move_iterator(orbs.begin()),
                  std::make_move_iterator(orbs.end()));
    layers.insert(layers.end(), std::make_move_iterator(contours.begin()),
                  std::make_move_iterator(contours.end()));
    return layers;
}

} // namespace abstract_background

} // namespace chronon3d::render_plan::effect_presets
