#pragma once

#include "execution_state.hpp"

#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

namespace chronon3d {
struct RenderCounters;

namespace graph {

// Private executor-module boundary for fully recorded CompiledFrameProgram
// execution. This header is src-local and is not part of the public SDK API.
bool execute_compiled_program(
    const CompiledFrameProgram& program,
    RenderGraphContext& ctx,
    ExecutionState& state,
    RenderCounters* counters);

} // namespace graph
} // namespace chronon3d
