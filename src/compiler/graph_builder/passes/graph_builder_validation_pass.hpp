#pragma once

#include <chronon3d/render_graph/builder/graph_build_pass.hpp>
#include <string_view>

namespace chronon3d::graph::detail {

/// Phase 7 pass: validates the scene before graph construction.
/// Appends validation diagnostics to the build context.
class ValidationPass final : public GraphBuildPass {
public:
    [[nodiscard]] std::string_view name() const override { return "Validation"; }
    void run(GraphBuildContext& ctx) override;
    [[nodiscard]] std::unique_ptr<GraphBuildPass> clone() const override { return std::make_unique<ValidationPass>(*this); }
};

} // namespace chronon3d::graph::detail
