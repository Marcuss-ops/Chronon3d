#pragma once

// ── color_pipeline.hpp — Fused color operations pipeline ──────────────────
//
// ColorPipeline bundles consecutive fusible color effects (Exposure, Levels,
// Curves, etc.) into a single per-pixel processing pass.
//
// A spatial effect (Blur, Offset, etc.) acts as a barrier:
//   Exposure + Levels + Blur + Curves → [Exposure, Levels] + [Blur] + [Curves]
//                                     → Pipeline 1 + Blur + Pipeline 2
//
// Usage:
//   ColorPipeline pipeline;
//   pipeline.add_exposure(stops);
//   pipeline.add_levels(master, r, g, b);
//   pipeline.add_curves(compiled_curve);
//   pipeline.apply(fb, clip);

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/effects/curves.hpp>
#include <chronon3d/effects/compose_color_op.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace chronon3d {

// ── ColorStage — a single fused color operation ──────────────────────────

struct ExposureStage { float stops{0.0f}; };

struct LevelsChannelStage {
    float input_black{0.0f};
    float input_white{1.0f};
    float gamma{1.0f};
    float output_black{0.0f};
    float output_white{1.0f};
};

struct LevelsStage {
    LevelsChannelStage master;
    LevelsChannelStage red;
    LevelsChannelStage green;
    LevelsChannelStage blue;
};

struct CurvesStage {
    std::shared_ptr<const CompiledCurve> master;
    std::shared_ptr<const CompiledCurve> red;
    std::shared_ptr<const CompiledCurve> green;
    std::shared_ptr<const CompiledCurve> blue;
};

/// A ComposeColorOp stage — applies a single ComposeOp with a blend colour.
/// Used internally by Exposure (Multiply) and available for general compositing.
struct ComposeStage {
    ComposeOp op{ComposeOp::Multiply};
    color::StraightRgb blend{1.0f, 1.0f, 1.0f};  // straight RGB blend colour
};

using ColorStage = std::variant<
    std::monostate,
    ExposureStage,
    LevelsStage,
    CurvesStage,
    ComposeStage
>;

// ── ColorPipeline ─────────────────────────────────────────────────────────

class ColorPipeline {
public:
    /// Append a fusible color stage.
    void add_stage(ColorStage stage) { m_stages.push_back(std::move(stage)); }

    /// Clear all stages.
    void clear() { m_stages.clear(); }

    /// Number of fused stages.
    [[nodiscard]] std::size_t stage_count() const { return m_stages.size(); }

    /// Whether the pipeline is empty (no stages).
    [[nodiscard]] bool empty() const { return m_stages.empty(); }

    /// Apply all fused stages to the given framebuffer.
    /// Follows the color contract: unpremultiply → transform → premultiply.
    void apply(Framebuffer& fb,
               const std::optional<raster::BBox>& clip = std::nullopt) const;

private:
    std::vector<ColorStage> m_stages;
};

} // namespace chronon3d
