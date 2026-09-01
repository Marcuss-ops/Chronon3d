// ============================================================================
// effect_stack_node.cpp — EffectStackNode implementation.
//
// Extracted from effect_stack_node.hpp so the public header doesn't need
// to #include <spdlog/spdlog.h>.
// ============================================================================

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <spdlog/spdlog.h>
#include <cmath>
#include <vector>

#include "native_surface.hpp"

namespace chronon3d::graph {

#include "effect_stack_node_native.inc"
#include "effect_stack_node_bbox.inc"
#include "effect_stack_node_execute.inc"

} // namespace chronon3d::graph
