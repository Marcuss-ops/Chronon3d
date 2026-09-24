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

    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            Color c = row[x];
            if (c.a <= 0.0f) continue;

            // 1. Unpremultiply
            auto srgb = color::unpremultiply_rgb(c);

            // 2. Apply all stages in order
            for (const auto& stage : m_stages) {
                if (auto* exp = std::get_if<ExposureStage>(&stage))
                    apply_exposure_stage(srgb, *exp);
                else if (auto* lvl = std::get_if<LevelsStage>(&stage))
                    apply_levels_stage(srgb, *lvl);
                else if (auto* curv = std::get_if<CurvesStage>(&stage))
                    apply_curves_stage(srgb, *curv);
                else if (auto* lut = std::get_if<Lut3DParams>(&stage))
                    apply_lut_stage(srgb, *lut);
                else if (auto* comp = std::get_if<ComposeStage>(&stage))
                    apply_compose_stage(srgb, *comp);
            }

            // 3. Premultiply
            row[x] = color::premultiply_rgb(srgb, c.a);
        }
    }
}

} // namespace chronon3d
