#pragma once

#include <chronon3d/compiler/detail/graph_compile_pipeline.hpp>

namespace chronon3d::graph {
// Test names remain stable while production uses the compiler-owned pipeline.
using FrameGraphCompiler = GraphCompilePipeline;
}
