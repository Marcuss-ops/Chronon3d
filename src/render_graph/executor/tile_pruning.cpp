#include "execution_state.hpp"
#include <chronon3d/core/dirty_fallback_reason.hpp>
#include <chronon3d/core/profiling/counters.hpp>

namespace chronon3d::graph {

std::optional<raster::BBox> compute_dirty_clip(
    const RenderGraphContext& ctx,
    const RenderGraphNode& node,
    const std::optional<raster::BBox>& predicted_bbox
) {
    CHRONON_ZONE_C("dirty_rect_clip", trace_category::kPipeline);

    std::optional<raster::BBox> clip = predicted_bbox;
    if (ctx.dirty_rect) {
        if (clip) {
            clip = raster::BBox{
                .x0 = std::max(clip->x0, ctx.dirty_rect->x0),
                .y0 = std::max(clip->y0, ctx.dirty_rect->y0),
                .x1 = std::min(clip->x1, ctx.dirty_rect->x1),
                .y1 = std::min(clip->y1, ctx.dirty_rect->y1)
            };
            if (clip->x0 >= clip->x1 || clip->y0 >= clip->y1) {
                clip = raster::BBox{0, 0, 0, 0};
            }
        } else {
            clip = ctx.dirty_rect;
        }
    }

    if (ctx.counters) {
        if (predicted_bbox) {
            const int w = std::max(0, clip->x1 - clip->x0);
            const int h = std::max(0, clip->y1 - clip->y0);
            ctx.counters->dirty_rect_count.fetch_add(1, std::memory_order_relaxed);
            ctx.counters->dirty_pixels.fetch_add(
                static_cast<uint64_t>(w) * static_cast<uint64_t>(h),
                std::memory_order_relaxed
            );
        } else {
            ctx.counters->dirty_full_fallbacks.fetch_add(1, std::memory_order_relaxed);
            const auto reason = [&]() -> DirtyFallbackReason {
                switch (node.kind()) {
                    case RenderGraphNodeKind::Composite:
                        return DirtyFallbackReason::CompositeMissingInputBounds;
                    case RenderGraphNodeKind::Transform:
                        return DirtyFallbackReason::TransformBoundsUnknown;
                    case RenderGraphNodeKind::Effect:
                    case RenderGraphNodeKind::Mask:
                    case RenderGraphNodeKind::Adjustment:
                    case RenderGraphNodeKind::MotionBlur:
                    case RenderGraphNodeKind::ColorConvert:
                    case RenderGraphNodeKind::TrackMatte:
                    case RenderGraphNodeKind::Transition:
                        return DirtyFallbackReason::EffectBoundsUnknown;
                    case RenderGraphNodeKind::Source:
                    case RenderGraphNodeKind::Precomp:
                    case RenderGraphNodeKind::Video:
                    case RenderGraphNodeKind::Output:
                        return DirtyFallbackReason::PredictedBoundsMissing;
                }
                return DirtyFallbackReason::PredictedBoundsMissing;
            }();
            ctx.counters->increment_dirty_full_fallback_reason(reason);
        }
    }

    return clip;
}

} // namespace chronon3d::graph
