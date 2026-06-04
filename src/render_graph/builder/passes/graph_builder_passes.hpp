#pragma once

#include <chronon3d/render_graph/builder/graph_build_pass.hpp>
#include "graph_builder_validation_pass.hpp"
#include <string_view>

namespace chronon3d::graph::detail {

/// Phase 7 pass: validates scene before graph construction.
/// Logs warnings/errors for duplicate names, missing parents, etc.
// (ValidationPass is declared in graph_builder_validation_pass.hpp)

/// Phase 4 pass: resolves layers from scene, populates GraphBuildContext
/// with resolved data, cam25d, static cache, matte sources, and name lookup.
class ResolvePass final : public GraphBuildPass {
public:
    [[nodiscard]] std::string_view name() const override { return "ResolveLayers"; }
    void run(GraphBuildContext& ctx) override;
    [[nodiscard]] std::unique_ptr<GraphBuildPass> clone() const override { return std::make_unique<ResolvePass>(*this); }
};

/// Phase 4 pass: creates ClearNode + appends root source nodes from scene.
/// Sets ctx.current_output to the resulting graph node.
class RootSourcesPass final : public GraphBuildPass {
public:
    [[nodiscard]] std::string_view name() const override { return "RootSources"; }
    void run(GraphBuildContext& ctx) override;
    [[nodiscard]] std::unique_ptr<GraphBuildPass> clone() const override { return std::make_unique<RootSourcesPass>(*this); }
};

/// Phase 4 pass: main layer iteration loop. Handles 3D bins, matte sources,
/// culling, and per-layer pipeline (source, transform, mask, lighting, shadow,
/// depth-grade, effect, track-matte, transition, composite).
class LayerPipelinePass final : public GraphBuildPass {
public:
    [[nodiscard]] std::string_view name() const override { return "LayerPipeline"; }
    void run(GraphBuildContext& ctx) override;
    [[nodiscard]] std::unique_ptr<GraphBuildPass> clone() const override { return std::make_unique<LayerPipelinePass>(*this); }
};

/// Phase 4 pass: DOF post-composite, sets graph output, runs early-exit analysis.
class OutputPass final : public GraphBuildPass {
public:
    [[nodiscard]] std::string_view name() const override { return "Output"; }
    void run(GraphBuildContext& ctx) override;
    [[nodiscard]] std::unique_ptr<GraphBuildPass> clone() const override { return std::make_unique<OutputPass>(*this); }
};

} // namespace chronon3d::graph::detail
