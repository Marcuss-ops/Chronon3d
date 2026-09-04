#include "execution_state.hpp"
#include "cache_evaluator.hpp"
#include "node_runner.hpp"
#include "node_executor.hpp"
#include "node_skip_policy.hpp"
#include "node_state_commit.hpp"
#include "tile_pruning.hpp"
#include "telemetry_emitter.hpp"
#include "text_bbox_reconcile.hpp"
#include <chronon3d/cache/persistent_framebuffer_store.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/internal/render_graph/node_memory_tracker.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/composite_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <blend2d.h>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/media/media_placement.hpp>
#include "../nodes/native_surface.hpp"
#include "../nodes/text_run/text_run_execution.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <unordered_map>

namespace chronon3d::graph {

#include "node_runner_helpers_detail.hpp"
#include "node_runner_fused_batch_detail.hpp"
#include "node_runner_single_node_detail.hpp"

} // namespace chronon3d::graph
