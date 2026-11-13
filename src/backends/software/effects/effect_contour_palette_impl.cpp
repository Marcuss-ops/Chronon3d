// ---------------------------------------------------------------------------
// effect_contour_palette_impl.cpp — topographic palette rings from alpha SDF
//
// The field is the same one used by SdfBevel (sdf_field.hpp). This effect only
// recolors inside the existing alpha coverage, so it does not need a spatial
// halo and never changes the layer silhouette.
// ---------------------------------------------------------------------------

#include "render_effects_processor.hpp"
#include "sdf_field.hpp"

#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace renderer {
namespace {

float smoothstep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

void apply_contour_palette(
    Framebuffer& fb,
    const ContourPaletteParams& p,
    const std::optional<raster::BBox>& clip) {
    p.validate();
    if (p.width <= 0.0f || p.opacity <= 0.0f) return;

    const auto field = sdf_field::build_from_alpha(fb);
    if (!field.has_content) return;

    int x0 = 0;
    int y0 = 0;
    int x1 = fb.width();
    int y1 = fb.height();
    if (clip) {
        x0 = std::clamp(clip->x0, 0, x1);
        y0 = std::clamp(clip->y0, 0, y1);
        x1 = std::clamp(clip->x1, 0, x1);
        y1 = std::clamp(clip->y1, 0, y1);
    }

    const auto palette = p.palette_colors();
    const float half_width = p.width * 0.5f;
    const float inner = std::max(0.0f, half_width - p.softness * 0.5f);
    const float outer = half_width + p.softness * 0.5f;
    const float feather_span = std::max(outer - inner, 1.0e-6f);

    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            Color& pixel = row[x];
            if (pixel.a <= 1.0e-8f) continue;

            const float distance = std::abs(field.at(x, y));
            const float phase = distance / p.spacing + p.offset;
            const float level_float = std::floor(phase);
            const float fraction = phase - level_float;
            const float ring_distance =
                std::min(fraction, 1.0f - fraction) * p.spacing;

            float mask = 0.0f;
            if (p.softness <= 0.0f) {
                mask = ring_distance <= half_width ? 1.0f : 0.0f;
            } else {
                const float t = std::clamp(
                    (ring_distance - inner) / feather_span, 0.0f, 1.0f);
                mask = 1.0f - smoothstep01(t);
            }
            mask *= p.opacity;
            if (mask <= 0.0f) continue;

            int index = static_cast<int>(level_float) % 4;
            if (index < 0) index += 4;
            const Color& tint = palette[static_cast<std::size_t>(index)];

            Color straight = pixel.unpremultiplied();
            straight.r = std::lerp(straight.r, tint.r, mask);
            straight.g = std::lerp(straight.g, tint.g, mask);
            straight.b = std::lerp(straight.b, tint.b, mask);
            pixel = straight.premultiplied();
        }
    }
}

} // namespace renderer
} // namespace chronon3d
