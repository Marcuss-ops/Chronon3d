// ============================================================================
// effect_stack_node.cpp — EffectStackNode implementation.
//
// Extracted from effect_stack_node.hpp so the public header doesn't need
// to #include <spdlog/spdlog.h>.
// ============================================================================

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/effect_gpu_capability.hpp>
#include <chronon3d/runtime/frame/frame_execution.hpp>
#include <chronon3d/runtime/job/job_session.hpp>
#include <spdlog/spdlog.h>
#include <cmath>
#include <initializer_list>
#include <vector>

#include "native_surface.hpp"

namespace chronon3d::graph {

#include "effect_stack_node_native_detail.hpp"
#include "effect_stack_node_native_color_detail.hpp"
#include "effect_stack_node_native_echo_detail.hpp"
#include "effect_stack_node_bbox_detail.hpp"
#include "effect_stack_node_execute_detail.hpp"

NodeExecResult execute_strict_native_adjustment_stack(
    RenderGraphContext& ctx, const EffectStack& effects,
    std::span<const FramebufferRef> inputs,
    std::string_view temporal_history_key) {
    EffectStackNode native_stack{EffectStack(effects)};
    if (!temporal_history_key.empty())
        native_stack.set_temporal_history_key(std::string(temporal_history_key));
    // AdjustmentNode transforms its input framebuffer in place conceptually:
    // its output bounds are fixed to the source extent. Do not feed the source
    // content bbox into EffectStackNode's halo expansion planner.
    return native_stack.execute(ctx, inputs,
        std::span<const std::optional<raster::BBox>>{});
}

} // namespace chronon3d::graph
