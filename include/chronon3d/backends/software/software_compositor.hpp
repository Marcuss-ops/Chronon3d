#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/compositor/composite_operator.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <optional>

namespace chronon3d {

struct RenderCounters;

class SoftwareCompositor {
public:
    static void composite_layer(
        Framebuffer& dest,
        const Framebuffer& src,
        BlendMode mode,
        const std::optional<raster::BBox>& clip = std::nullopt,
        CompositeOperator op = CompositeOperator::SourceOver
    );

private:
    static bool composite_layer_normal_optimized(
        Framebuffer& dst, const Framebuffer& src, i32 x0, i32 y0, i32 x1, i32 y1,
        RenderCounters* cnt, profiling::Clock::time_point t_setup0);

    /// Highway-accelerated + TBB-parallelized path for Add / Multiply / Screen / Overlay.
    /// Returns true if processed (SIMD path available), false to fall back to scalar.
    static bool composite_layer_non_normal_optimized(
        Framebuffer& dst, const Framebuffer& src, BlendMode mode, i32 x0, i32 y0, i32 x1, i32 y1,
        RenderCounters* cnt, profiling::Clock::time_point t_setup0);
};

} // namespace chronon3d
