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
                else if (auto* comp = std::get_if<ComposeStage>(&stage))
                    apply_compose_stage(srgb, *comp);
            }

            // 3. Premultiply
            row[x] = color::premultiply_rgb(srgb, c.a);
        }
    }
}

} // namespace chronon3d
