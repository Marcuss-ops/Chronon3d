#pragma once

#include <memory>
#include <string_view>

namespace chronon3d::graph {

struct GraphBuildContext;

/// Abstract interface for a single step in the render-graph build pipeline.
///
/// Each pass receives the mutable build context and may add nodes to the graph,
/// update the current output pointer, or modify shared state (e.g. early-exit
/// skip table, DOF depth buffer).
///
/// Passes are executed in registration order.  The pipeline guarantees that
/// earlier passes have finished before later ones begin.
///
class GraphBuildPass {
public:
    virtual ~GraphBuildPass() = default;

    /// Human-readable name for diagnostics / profiling.
    [[nodiscard]] virtual std::string_view name() const = 0;

    /// Execute this pass.  Implementations mutate `ctx` as needed.
    virtual void run(GraphBuildContext& ctx) = 0;

    /// Create a polymorphic clone.  Required by GraphBuildRegistry so that
    /// the registry can hand out copies without losing ownership of the
    /// original pass instances.
    [[nodiscard]] virtual std::unique_ptr<GraphBuildPass> clone() const = 0;
};

} // namespace chronon3d::graph
