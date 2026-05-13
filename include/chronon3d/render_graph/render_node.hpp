#pragma once

#include <chronon3d/renderer/software/render_graph.hpp>

namespace chronon3d::render_graph {

using NodeId = chronon3d::rendergraph::NodeId;
using RenderNodeKind = chronon3d::rendergraph::RenderNodeKind;
using RenderPassKind = chronon3d::rendergraph::RenderPassKind;
using RenderCacheKey = chronon3d::rendergraph::RenderCacheKey;
using RenderCacheKeyHash = chronon3d::rendergraph::RenderCacheKeyHash;
using RenderPassResult = chronon3d::rendergraph::RenderPassResult;
using RenderGraphExecutionState = chronon3d::rendergraph::RenderGraphExecutionState;
using RenderGraphExecutionContext = chronon3d::rendergraph::RenderGraphExecutionContext;
using RenderNode = chronon3d::rendergraph::RenderNode;
using RenderPass = chronon3d::rendergraph::RenderPass;

} // namespace chronon3d::render_graph
