// ═══════════════════════════════════════════════════════════════════════════
// frame_graph_builder.cpp — compiled frame graph builder aggregation
// ═══════════════════════════════════════════════════════════════════════════
// Implementation is split by compiler responsibility while remaining a
// single translation unit, preserving the existing linkage/CMake boundary.

#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/render_graph/nodes/video_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/composite_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/render_graph/nodes/adjustment_node.hpp>
#include <chronon3d/render_graph/nodes/dof_node.hpp>

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <typeindex>

namespace chronon3d::graph {

#include "frame_graph_builder_levels_detail.hpp"
#include "frame_graph_builder_metadata_detail.hpp"
#include "frame_graph_builder_validation_detail.hpp"
#include "frame_graph_builder_program_detail.hpp"
#include "frame_graph_builder_allocation_detail.hpp"

} // namespace chronon3d::graph
