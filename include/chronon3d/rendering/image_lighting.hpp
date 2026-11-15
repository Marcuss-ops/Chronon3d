#pragma once

// ---------------------------------------------------------------------------
// 2.5D image lighting.
//
// The classic 2.5D path treats every layer as a flat plane with a single
// geometric normal. This module adds the optical cues that make a photograph,
// painting, paper texture or cut-out read as a surface:
//
//   * a synthetic normal derived from the image's own luminance (Scharr),
//   * HDR, unclamped accumulation in linear light,
//   * an alpha-gradient rim light for cut-out silhouettes,
//   * light wrap from an already-blurred background,
//   * a world-space gobo projection (stable while the layer pans/zooms),
//   * a deterministic sub-LSB dither for the final 8-bit output.
//
// The existing color::OutputTransformOptions owns ACES tone mapping and the
// sRGB OETF; this header only supplies the dither offset so both paths share
// one canonical output transform.
// ---------------------------------------------------------------------------

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>
#include <chronon3d/rendering/lighting_rig.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chronon3d::rendering {

struct ImageTexelView;

// Tunables for one image layer. Defaults describe a matte paper surface; a
// UI card or glossy gel should lower roughness and raise specular_intensity.
struct ImageLightingParams {
    f32 bump_strength{0.15f};        // synthetic normal relief; 0 = flat plane
    f32 roughness{0.65f};            // 0 = glass, 1 = matte paper
    f32 specular_intensity{0.25f};   // Blinn-Phong / sheen weight
    f32 diffuse{1.0f};               // Lambert coefficient
    f32 ambient_multiplier{1.0f};    // scales the scene ambient term

    f32 rim_strength{0.0f};          // alpha-edge rim light weight
    f32 rim_width_px{4.0f};          // blur radius used to derive the edge
    f32 rim_power{2.0f};             // angular falloff of the rim

    f32 light_wrap_strength{0.0f};   // background bleed on the inner edge

    // World-space gobo. `gobo_scale` maps canvas pixels to gobo UV; the
    // sample is independent of the layer's own image UVs.
    const ImageTexelView* gobo{nullptr};
    f32 gobo_scale{0.0008f};
    Vec2 gobo_offset{0.0f, 0.0f};
};

// Non-owning, clamped view over an RGBA image (linear premultiplied pixels).
struct ImageTexelView {
    const Color* pixels{nullptr};
    i32 width{0};
    i32 height{0};
    i32 stride{0}; // pixels per row

    [[nodiscard]] bool valid() const {
        return pixels != nullptr && width > 0 && height > 0 && stride > 0;
    }

    [[nodiscard]] Color at_clamped(i32 x, i32 y) const {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                      static_cast<std::size_t>(x)];
    }

    [[nodiscard]] f32 alpha_clamped(i32 x, i32 y) const {
        return at_clamped(x, y).a;
    }
};

[[nodiscard]] inline f32 image_luminance(const Color& c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

// Scharr gradient of the image luminance, returned as a tangent-space normal.
// Image rows grow downward; the world-space Y axis grows upward, so the Y
// derivative is negated into the normal (see the sign convention test).
[[nodiscard]] inline Vec3 compute_image_normal(
    const ImageTexelView& view, i32 x, i32 y, f32 bump_strength) {
    if (!view.valid() || bump_strength <= 0.0f) return Vec3{0.0f, 0.0f, 1.0f};

    const f32 tl = image_luminance(view.at_clamped(x - 1, y - 1));
    const f32 tc = image_luminance(view.at_clamped(x,     y - 1));
    const f32 tr = image_luminance(view.at_clamped(x + 1, y - 1));
    const f32 ml = image_luminance(view.at_clamped(x - 1, y));
    const f32 mr = image_luminance(view.at_clamped(x + 1, y));
    const f32 bl = image_luminance(view.at_clamped(x - 1, y + 1));
    const f32 bc = image_luminance(view.at_clamped(x,     y + 1));
    const f32 br = image_luminance(view.at_clamped(x + 1, y + 1));

    // Scharr kernel normalized by 32: less rotational bias than Sobel.
    const f32 gx = (3.0f * (tr - tl) + 10.0f * (mr - ml) + 3.0f * (br - bl)) / 32.0f;
    const f32 gy = (3.0f * (bl - tl) + 10.0f * (bc - tc) + 3.0f * (br - tr)) / 32.0f;

    const Vec3 local{-gx * bump_strength, gy * bump_strength, 1.0f};
    const f32 length = std::sqrt(local.x * local.x + local.y * local.y + local.z * local.z);
    if (length <= 1e-8f) return Vec3{0.0f, 0.0f, 1.0f};
    return local / length;
}

// Separable box blur of the alpha channel, used as the soft distance field for
// the rim light. Radius is clamped to 64 px to keep the cost bounded.
[[nodiscard]] inline std::vector<f32> blur_alpha_box(
    const ImageTexelView& view, i32 radius) {
    if (!view.valid()) return {};
    radius = std::clamp(radius, 0, 64);
    const i32 width = view.width;
    const i32 height = view.height;
    const std::size_t count = static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height);
    std::vector<f32> horizontal(count, 0.0f);
    std::vector<f32> blurred(count, 0.0f);
    const f32 inv = 1.0f / static_cast<f32>(radius * 2 + 1);

    for (i32 y = 0; y < height; ++y) {
        f32 sum = 0.0f;
        for (i32 k = -radius; k <= radius; ++k)
            sum += view.alpha_clamped(k, y);
        for (i32 x = 0; x < width; ++x) {
            horizontal[static_cast<std::size_t>(y) * width + x] = sum * inv;
            sum += view.alpha_clamped(x + radius + 1, y);
            sum -= view.alpha_clamped(x - radius, y);
        }
    }
    for (i32 x = 0; x < width; ++x) {
        f32 sum = 0.0f;
        for (i32 k = -radius; k <= radius; ++k)
            sum += horizontal[static_cast<std::size_t>(std::clamp(k, 0, height - 1)) * width + x];
        for (i32 y = 0; y < height; ++y) {
            blurred[static_cast<std::size_t>(y) * width + x] = sum * inv;
            const i32 add_y = std::clamp(y + radius + 1, 0, height - 1);
            const i32 sub_y = std::clamp(y - radius, 0, height - 1);
            sum += horizontal[static_cast<std::size_t>(add_y) * width + x];
            sum -= horizontal[static_cast<std::size_t>(sub_y) * width + x];
        }
    }
    return blurred;
}

// Separable box blur of premultiplied RGBA, used to soften the background
// plate that feeds light wrap. Radius is clamped to 64 px like the alpha
// blur so a malformed authoring value cannot stall the CPU relight pass.
[[nodiscard]] inline std::vector<Color> blur_image_box(
    const ImageTexelView& view, i32 radius) {
    if (!view.valid()) return {};
    radius = std::clamp(radius, 0, 64);
    const i32 width = view.width;
    const i32 height = view.height;
    const std::size_t count = static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height);
    std::vector<Color> horizontal(count, Color::transparent());
    std::vector<Color> blurred(count, Color::transparent());
    const f32 inv = 1.0f / static_cast<f32>(radius * 2 + 1);

    for (i32 y = 0; y < height; ++y) {
        f32 r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
        for (i32 k = -radius; k <= radius; ++k) {
            const Color c = view.at_clamped(k, y);
            r += c.r; g += c.g; b += c.b; a += c.a;
        }
        for (i32 x = 0; x < width; ++x) {
            horizontal[static_cast<std::size_t>(y) * width + x] =
                Color{r * inv, g * inv, b * inv, a * inv};
            const Color add = view.at_clamped(x + radius + 1, y);
            const Color sub = view.at_clamped(x - radius, y);
            r += add.r - sub.r;
            g += add.g - sub.g;
            b += add.b - sub.b;
            a += add.a - sub.a;
        }
    }
    for (i32 x = 0; x < width; ++x) {
        f32 r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
        for (i32 k = -radius; k <= radius; ++k) {
            const Color c = horizontal[
                static_cast<std::size_t>(std::clamp(k, 0, height - 1)) * width + x];
            r += c.r; g += c.g; b += c.b; a += c.a;
        }
        for (i32 y = 0; y < height; ++y) {
            blurred[static_cast<std::size_t>(y) * width + x] =
                Color{r * inv, g * inv, b * inv, a * inv};
            const i32 add_y = std::clamp(y + radius + 1, 0, height - 1);
            const i32 sub_y = std::clamp(y - radius, 0, height - 1);
            const Color add = horizontal[static_cast<std::size_t>(add_y) * width + x];
            const Color sub = horizontal[static_cast<std::size_t>(sub_y) * width + x];
            r += add.r - sub.r;
            g += add.g - sub.g;
            b += add.b - sub.b;
            a += add.a - sub.a;
        }
    }
    return blurred;
}

struct AlphaEdgeSample {
    Vec2 outward{0.0f, 0.0f}; // points from the silhouette into the background
    f32 mask{0.0f};           // peaks just inside the contour
};

// Derive the outward edge direction and edge mask from the blurred alpha.
// The gradient of a blurred alpha field points inward (towards full coverage),
// so the outward direction is its negation.
[[nodiscard]] inline AlphaEdgeSample sample_alpha_edge(
    const std::vector<f32>& blurred_alpha, i32 width, i32 height,
    i32 x, i32 y, f32 coverage) {
    if (width <= 0 || height <= 0 ||
        blurred_alpha.size() < static_cast<std::size_t>(width) * height)
        return {};
    const auto at = [&](i32 sx, i32 sy) {
        sx = std::clamp(sx, 0, width - 1);
        sy = std::clamp(sy, 0, height - 1);
        return blurred_alpha[static_cast<std::size_t>(sy) * width + sx];
    };
    const f32 gx = (at(x + 1, y) - at(x - 1, y)) * 0.5f;
    const f32 gy = (at(x, y + 1) - at(x, y - 1)) * 0.5f;
    const f32 length = std::sqrt(gx * gx + gy * gy);

    AlphaEdgeSample result;
    if (length > 1e-6f) {
        result.outward = Vec2{-gx / length, -gy / length};
    }
    // smoothstep(0.05, 0.85, 1 - blurred) * coverage
    const f32 t = std::clamp((1.0f - at(x, y) - 0.05f) / 0.8f, 0.0f, 1.0f);
    result.mask = t * t * (3.0f - 2.0f * t) * std::clamp(coverage, 0.0f, 1.0f);
    return result;
}

// Angular weight of the rim for a light arriving from `light_xy`.
[[nodiscard]] inline f32 alpha_rim_alignment(
    Vec2 outward, Vec2 light_xy, f32 power) {
    const f32 light_length = std::sqrt(light_xy.x * light_xy.x + light_xy.y * light_xy.y);
    if (light_length <= 1e-8f) return 0.0f;
    const f32 alignment = (outward.x * light_xy.x + outward.y * light_xy.y) / light_length;
    return std::pow(std::max(alignment, 0.0f), std::max(power, 0.01f));
}

// Z-layered soft-shadow offset, blur growth and opacity falloff are owned by
// ShadowNode (see `ShadowSettings` and the depth-aware shadow tests); this
// header deliberately does not duplicate that math.

// Gobo UVs live in receiving-plane space, not the layer's image UVs, so a
// Ken Burns pan/zoom does not drag the projected light across the frame.
[[nodiscard]] inline Vec2 gobo_uv(
    Vec2 world_xy, f32 scale, Vec2 offset) {
    return Vec2{world_xy.x * scale + 0.5f + offset.x,
                world_xy.y * scale + 0.5f + offset.y};
}

// Bilinear, repeat-wrapped gobo sample. The red channel is the mask.
[[nodiscard]] inline f32 sample_gobo(const ImageTexelView& gobo, Vec2 uv) {
    if (!gobo.valid()) return 1.0f;
    const auto wrap = [](f32 value, i32 size) {
        f32 wrapped = value - std::floor(value);
        if (wrapped < 0.0f) wrapped += 1.0f;
        return std::clamp(static_cast<i32>(wrapped * static_cast<f32>(size)),
                          0, size - 1);
    };
    return gobo.at_clamped(wrap(uv.x, gobo.width), wrap(uv.y, gobo.height)).r;
}

struct ImageLightingSample {
    Color albedo{1.0f, 1.0f, 1.0f, 1.0f};
    Vec3 normal_world{0.0f, 0.0f, 1.0f};
    Vec3 pixel_world{0.0f, 0.0f, 0.0f};
    f32 coverage{1.0f};
    f32 edge_mask{0.0f};
    Vec2 edge_outward{0.0f, 0.0f};
    Color background_wrap{0.0f, 0.0f, 0.0f, 1.0f};
};

// Unclamped, scene-linear result: values above 1.0 are intentional and are
// compressed later by the shared ACES output transform.
[[nodiscard]] inline Color evaluate_image_lighting(
    const ImageLightingSample& sample, const LightContext& light,
    const ImageLightingParams& params, const RimLight* rim_override = nullptr) {
    if (!light.enabled && light.point_lights.empty()) return sample.albedo;

    const Vec3 n = safe_normalize(sample.normal_world);
    const Vec3 v{0.0f, 0.0f, 1.0f};
    Vec3 factor{0.0f, 0.0f, 0.0f};

    const f32 shininess = 128.0f + (4.0f - 128.0f) *
        std::clamp(params.roughness, 0.0f, 1.0f);

    const auto add_light = [&](Vec3 to_light, const Color& color,
                               f32 intensity, f32 attenuation) {
        const Vec3 l = safe_normalize(to_light);
        const f32 ndotl = std::max(0.0f, glm::dot(n, l));
        const Vec3 h = safe_normalize(l + v, v);
        const f32 ndoth = std::max(0.0f, glm::dot(n, h));
        const f32 specular = std::pow(ndoth, std::max(shininess, 1.0f)) *
                             std::clamp(params.specular_intensity, 0.0f, 1.0f);
        const f32 diffuse = ndotl * params.diffuse;
        const f32 energy = intensity * attenuation;
        factor += Vec3{color.r, color.g, color.b} * (energy * (diffuse + specular));

        if (params.rim_strength > 0.0f && sample.edge_mask > 0.0f) {
            const f32 alignment = alpha_rim_alignment(
                sample.edge_outward, Vec2{l.x, l.y}, params.rim_power);
            const f32 rim = alignment * params.rim_strength * sample.edge_mask;
            factor += Vec3{color.r, color.g, color.b} * (energy * rim);
        }
    };

    if (light.enabled) {
        if (light.ambient_enabled) {
            factor += Vec3{light.ambient_color.r, light.ambient_color.g,
                           light.ambient_color.b} *
                      (light.ambient * params.ambient_multiplier);
        }
        if (light.directional_enabled) {
            f32 attenuation = 1.0f;
            if (params.gobo) {
                const Vec2 uv = gobo_uv(
                    Vec2{sample.pixel_world.x, sample.pixel_world.y},
                    params.gobo_scale, params.gobo_offset);
                attenuation *= sample_gobo(*params.gobo, uv);
            }
            add_light(light.direction, light.directional_color, light.diffuse,
                      attenuation);
        }
    }

    for (const auto& source : light.point_lights) {
        Vec3 to_light{0.0f, 0.0f, 1.0f};
        f32 attenuation = 1.0f;
        if (source.kind == PointLightKind::Directional) {
            to_light = -safe_normalize(source.direction);
        } else {
            const Vec3 delta = source.position - sample.pixel_world;
            const f32 distance = std::sqrt(delta.x * delta.x + delta.y * delta.y +
                                           delta.z * delta.z);
            if (source.range > 0.0f && distance > source.range) continue;
            if (distance > 1e-6f) to_light = delta / distance;
            attenuation = point_light_attenuation(distance, source);
            if (source.kind == PointLightKind::Spot) {
                const Vec3 axis = safe_normalize(source.direction,
                                                 Vec3{0.0f, 0.0f, -1.0f});
                const f32 cos_to_pixel = glm::dot(-to_light, axis);
                const f32 cos_outer = std::cos(source.angle);
                const f32 cos_inner = std::cos(source.angle * (1.0f - source.penumbra));
                if (cos_to_pixel <= cos_outer) {
                    attenuation = 0.0f;
                } else if (cos_to_pixel < cos_inner && cos_inner > cos_outer) {
                    const f32 t = (cos_to_pixel - cos_outer) / (cos_inner - cos_outer);
                    attenuation *= t * t * (3.0f - 2.0f * t);
                }
            }
        }
        if (params.gobo) {
            const Vec2 uv = gobo_uv(
                Vec2{sample.pixel_world.x, sample.pixel_world.y},
                params.gobo_scale, params.gobo_offset);
            attenuation *= sample_gobo(*params.gobo, uv);
        }
        // PointLightSource::color already carries its authored intensity.
        add_light(to_light, source.color, 1.0f, attenuation);
    }

    // Independent Fresnel rim from the scene rig (normal-based, distinct from
    // the alpha-edge rim above which follows the silhouette).
    if (rim_override && rim_override->enabled) {
        const f32 ndotv = std::clamp(glm::dot(n, v), 0.0f, 1.0f);
        const f32 fresnel = std::pow(1.0f - ndotv,
                                     std::max(rim_override->power, 0.01f)) *
                            std::max(0.0f, rim_override->intensity);
        factor += Vec3{rim_override->color.r, rim_override->color.g,
                       rim_override->color.b} * fresnel;
    }

    Color out{
        sample.albedo.r * factor.x,
        sample.albedo.g * factor.y,
        sample.albedo.b * factor.z,
        sample.albedo.a};

    // Light wrap: the blurred background bleeds over the inner contour,
    // strongest on dark foreground pixels.
    if (params.light_wrap_strength > 0.0f && sample.edge_mask > 0.0f) {
        const f32 weight = sample.edge_mask * params.light_wrap_strength;
        out.r += sample.background_wrap.r * weight *
                 std::max(0.0f, 1.0f - std::clamp(sample.albedo.r, 0.0f, 1.0f));
        out.g += sample.background_wrap.g * weight *
                 std::max(0.0f, 1.0f - std::clamp(sample.albedo.g, 0.0f, 1.0f));
        out.b += sample.background_wrap.b * weight *
                 std::max(0.0f, 1.0f - std::clamp(sample.albedo.b, 0.0f, 1.0f));
    }
    return out;
}

// Interleaved-gradient noise (Jimenez 2014), deterministic in pixel space and
// free of the clumping of a naive hash. Returns [0, 1).
[[nodiscard]] inline f32 interleaved_gradient_noise(f32 x, f32 y) {
    const f32 f = 0.06711056f * x + 0.00583715f * y;
    const f32 wrapped = f - std::floor(f);
    const f32 g = 52.9829189f * wrapped;
    const f32 value = g - std::floor(g);
    return std::clamp(value, 0.0f, 1.0f);
}

// Sub-LSB dither offset for an 8-bit target: ±0.5/255.
[[nodiscard]] inline f32 dither_offset_8bit(i32 x, i32 y) {
    return (interleaved_gradient_noise(static_cast<f32>(x) + 0.5f,
                                       static_cast<f32>(y) + 0.5f) - 0.5f) /
           255.0f;
}

} // namespace chronon3d::rendering
