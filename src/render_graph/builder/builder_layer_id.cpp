#include <chronon3d/render_graph/render_graph.hpp>

namespace chronon3d::graph {

thread_local std::string g_current_builder_layer_id;
thread_local RenderGraphNode::OpacityEvaluator g_current_builder_opacity_evaluator;

} // namespace chronon3d::graph
