// ── color_pipeline.cpp — Fused color pipeline implementation ──────────────

#include <chronon3d/effects/color_pipeline.hpp>
#include <chronon3d/effects/color_contract.hpp>
#include <chronon3d/effects/compose_color_op.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {

// =============================================================================
// Stage application helpers
// =============================================================================

namespace {

/// Apply exposure to a single straight-RGB pixel.
inline void apply_exposure_stage(color::StraightRgb& srgb, const ExposureStage& stage) {
    const float mult = std::exp2(stage.stops);
    srgb.r *= mult;
    srgb.g *= mult;
    srgb.b *= mult;
}

/// Apply a single levels channel to a value.
inline float apply_levels_channel(float v, const LevelsChannelStage& ch) {
    float n = (v - ch.input_black) / std::max(ch.input_white - ch.input_black, 1.0e-6f);
    // Signed pow for HDR
    n = (n >= 0.0f)
        ? std::pow(n, 1.0f / std::max(ch.gamma, 1.0e-6f))
        : -std::pow(-n, 1.0f / std::max(ch.gamma, 1.0e-6f));
    return ch.output_black + n * (ch.output_white - ch.output_black);
}

/// Apply a full LevelsStage to straight-RGB.
inline void apply_levels_stage(color::StraightRgb& srgb, const LevelsStage& stage) {
    // Per-channel
    float r = apply_levels_channel(srgb.r, stage.red);
    float g = apply_levels_channel(srgb.g, stage.green);
    float b = apply_levels_channel(srgb.b, stage.blue);
    // Master
    srgb.r = apply_levels_channel(r, stage.master);
    srgb.g = apply_levels_channel(g, stage.master);
    srgb.b = apply_levels_channel(b, stage.master);
}

/// Apply a CurvesStage to straight-RGB.
inline void apply_curves_stage(color::StraightRgb& srgb, const CurvesStage& stage) {
    if (stage.master) {
        srgb.r = stage.master->evaluate(srgb.r);
        srgb.g = stage.master->evaluate(srgb.g);
        srgb.b = stage.master->evaluate(srgb.b);
    }
    if (stage.red)   srgb.r = stage.red->evaluate(srgb.r);
    if (stage.green) srgb.g = stage.green->evaluate(srgb.g);
    if (stage.blue)  srgb.b = stage.blue->evaluate(srgb.b);
}

/// Apply a ComposeStage to straight-RGB.
inline void apply_compose_stage(color::StraightRgb& srgb, const ComposeStage& stage) {
    srgb = apply_compose_op(srgb, stage.op, stage.blend);
}

inline void apply_color_matrix_stage(color::StraightRgb& rgb, float& alpha,
                                     const ColorMatrixParams& matrix) {
    const std::array<float, 4> in{rgb.r, rgb.g, rgb.b, alpha};
    std::array<float, 4> out{};
    for (std::size_t row = 0; row < 4; ++row) {
        const auto i = row * 5;
        out[row] = matrix.values[i] * in[0] + matrix.values[i + 1] * in[1] +
                   matrix.values[i + 2] * in[2] + matrix.values[i + 3] * in[3] +
                   matrix.values[i + 4];
    }
    rgb = {out[0], out[1], out[2]};
    alpha = std::clamp(out[3], 0.0f, 1.0f);
}

inline void apply_posterize_stage(color::StraightRgb& rgb,
                                  const PosterizeStage& stage) {
    const float scale = static_cast<float>(stage.levels - 1);
    const auto quantize = [scale](float value) {
        return std::floor(std::clamp(value, 0.0f, 1.0f) * scale + 0.5f) / scale;
    };
    rgb.r = quantize(rgb.r);
    rgb.g = quantize(rgb.g);
    rgb.b = quantize(rgb.b);
}

inline void apply_color_map_stage(color::StraightRgb& rgb,
                                  const ColorMapStage& stage) {
    const float luma = 0.2126f * rgb.r + 0.7152f * rgb.g + 0.0722f * rgb.b;
    const float scaled = std::clamp(luma, 0.0f, 1.0f) * 4.0f;
    const int lower = std::min(3, static_cast<int>(scaled));
    const float t = scaled - static_cast<float>(lower);
    const std::size_t lo_index = static_cast<std::size_t>(lower) * 3;
    const std::size_t hi_index = static_cast<std::size_t>(lower + 1) * 3;
    const float mapped_r = stage.stops[lo_index] +
                           (stage.stops[hi_index] - stage.stops[lo_index]) * t;
    const float mapped_g = stage.stops[lo_index + 1] +
                           (stage.stops[hi_index + 1] - stage.stops[lo_index + 1]) * t;
    const float mapped_b = stage.stops[lo_index + 2] +
                           (stage.stops[hi_index + 2] - stage.stops[lo_index + 2]) * t;
    rgb.r += (mapped_r - rgb.r) * stage.amount;
    rgb.g += (mapped_g - rgb.g) * stage.amount;
    rgb.b += (mapped_b - rgb.b) * stage.amount;
}

inline std::array<float, 3> sample_lut(const Lut3DParams& lut, float r, float g, float b) {
    const auto n = static_cast<int>(lut.dimension);
    const auto coordinate = [&](float value, std::size_t channel) {
        return std::clamp((value - lut.domain_min[channel]) /
                          (lut.domain_max[channel] - lut.domain_min[channel]), 0.0f, 1.0f)
               * static_cast<float>(n - 1);
    };
    const float x = coordinate(r, 0), y = coordinate(g, 1), z = coordinate(b, 2);
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y), z0 = static_cast<int>(z);
    const int x1 = std::min(x0 + 1, n - 1), y1 = std::min(y0 + 1, n - 1), z1 = std::min(z0 + 1, n - 1);
    const float tx = x - x0, ty = y - y0, tz = z - z0;
    const auto at = [&](int ix, int iy, int iz) -> const std::array<float, 3>& {
        return (*lut.values)[static_cast<std::size_t>(iz * n * n + iy * n + ix)];
    };
    std::array<float, 3> out{};
    for (std::size_t c = 0; c < 3; ++c) {
        const float c00 = std::lerp(at(x0,y0,z0)[c], at(x1,y0,z0)[c], tx);
        const float c10 = std::lerp(at(x0,y1,z0)[c], at(x1,y1,z0)[c], tx);
        const float c01 = std::lerp(at(x0,y0,z1)[c], at(x1,y0,z1)[c], tx);
        const float c11 = std::lerp(at(x0,y1,z1)[c], at(x1,y1,z1)[c], tx);
        out[c] = std::lerp(std::lerp(c00, c10, ty), std::lerp(c01, c11, ty), tz);
    }
    return out;
}

inline void apply_lut_stage(color::StraightRgb& rgb, const Lut3DParams& lut) {
    if (lut.dimension < 2 || !lut.values ||
        lut.values->size() != static_cast<std::size_t>(lut.dimension) * lut.dimension * lut.dimension ||
        !std::isfinite(lut.amount) || lut.amount <= 0.0f || lut.amount > 1.0f)
        return;
    for (std::size_t c = 0; c < 3; ++c)
        if (!std::isfinite(lut.domain_min[c]) || !std::isfinite(lut.domain_max[c]) ||
            lut.domain_max[c] <= lut.domain_min[c]) return;
    const auto mapped = sample_lut(lut, rgb.r, rgb.g, rgb.b);
    rgb.r = std::lerp(rgb.r, mapped[0], lut.amount);
    rgb.g = std::lerp(rgb.g, mapped[1], lut.amount);
    rgb.b = std::lerp(rgb.b, mapped[2], lut.amount);
}

// ── Resolved stage dispatch ────────────────────────────────────────────────
// One indirect call per stage per pixel instead of a std::get_if chain per
// pixel: the chain is loop-invariant and blocks auto-vectorization of the
// fused pass.  Resolution happens once per apply(), the pixel loop only calls.
using StageFn = void (*)(color::StraightRgb&, float&, const void*);

void exposure_fn(color::StraightRgb& rgb, float&, const void* stage) {
    apply_exposure_stage(rgb, *static_cast<const ExposureStage*>(stage));
}

void levels_fn(color::StraightRgb& rgb, float&, const void* stage) {
    apply_levels_stage(rgb, *static_cast<const LevelsStage*>(stage));
}

void curves_fn(color::StraightRgb& rgb, float&, const void* stage) {
    apply_curves_stage(rgb, *static_cast<const CurvesStage*>(stage));
}

void lut_fn(color::StraightRgb& rgb, float&, const void* stage) {
    apply_lut_stage(rgb, *static_cast<const Lut3DParams*>(stage));
}

void compose_fn(color::StraightRgb& rgb, float&, const void* stage) {
    apply_compose_stage(rgb, *static_cast<const ComposeStage*>(stage));
}

void matrix_fn(color::StraightRgb& rgb, float& alpha, const void* stage) {
    apply_color_matrix_stage(rgb, alpha,
                             static_cast<const ColorMatrixStage*>(stage)->params);
}

void posterize_fn(color::StraightRgb& rgb, float&, const void* stage) {
    apply_posterize_stage(rgb, *static_cast<const PosterizeStage*>(stage));
}

void color_map_fn(color::StraightRgb& rgb, float&, const void* stage) {
    apply_color_map_stage(rgb, *static_cast<const ColorMapStage*>(stage));
}

/// Maps a variant stage to its direct entry point.  A stage without a kernel
/// (std::monostate) resolves to nullptr and is skipped — same behaviour as the
/// previous per-pixel chain.
[[nodiscard]] StageFn resolve_stage_fn(const ColorStage& stage) {
    if (std::get_if<ExposureStage>(&stage)) return &exposure_fn;
    if (std::get_if<LevelsStage>(&stage)) return &levels_fn;
    if (std::get_if<CurvesStage>(&stage)) return &curves_fn;
    if (std::get_if<Lut3DParams>(&stage)) return &lut_fn;
    if (std::get_if<ComposeStage>(&stage)) return &compose_fn;
    if (std::get_if<ColorMatrixStage>(&stage)) return &matrix_fn;
    if (std::get_if<PosterizeStage>(&stage)) return &posterize_fn;
    if (std::get_if<ColorMapStage>(&stage)) return &color_map_fn;
    return nullptr;
}

} // anonymous namespace

// =============================================================================
// ColorPipeline::apply
// =============================================================================

void ColorPipeline::apply(
    Framebuffer& fb,
    const std::optional<raster::BBox>& clip) const
{
    if (m_stages.empty()) return;

    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }
    if (y0 >= y1 || x0 >= x1) return;

    // Variant dispatch resolved once outside the pixel loop (resolve_stage_fn
    // above): the loop only performs direct calls, same stages, same order,
    // same arithmetic.
    struct ResolvedStage {
        StageFn fn;
        const void* stage;
    };
    std::vector<ResolvedStage> resolved;
    resolved.reserve(m_stages.size());
    for (const auto& stage : m_stages) {
        if (const StageFn fn = resolve_stage_fn(stage)) {
            resolved.push_back({fn, &stage});
        }
    }

    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            Color c = row[x];
            if (c.a <= 0.0f) continue;

            // 1. Unpremultiply
            auto srgb = color::unpremultiply_rgb(c);

            // 2. Apply all stages in order
            float alpha = c.a;
            for (const auto& entry : resolved) {
                entry.fn(srgb, alpha, entry.stage);
            }

            // 3. Premultiply
            row[x] = color::premultiply_rgb(srgb, alpha);
        }
    }
}

} // namespace chronon3d
