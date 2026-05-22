#include <chronon3d/render_graph/graph_executor.hpp>
#include <thread>

namespace chronon3d::graph {

// GraphExecutor::build_execution_plan lives in executor/graph_executor_plan.cpp
// GraphExecutor::execute and internal helpers live in executor/graph_executor_phases.cpp

GraphExecutor::GraphExecutor()
    : m_arena(std::max(1u, std::thread::hardware_concurrency())) {}

} // namespace chronon3d::graph
