#pragma once

#include <chronon3d/render_graph/builder/graph_build_pass.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::graph {

/// Ordering phases for graph build passes.  Passes within the same phase
/// run in registration order.
enum class GraphBuildPhase {
    PreResolve,    // before layer resolution (e.g. pre-validation)
    Resolve,       // resolve_layers + context population
    RootSources,   // ClearNode + root scene sources
    LayerPipeline, // per-layer iteration (sources, transforms, masks, effects, compositing)
    PostComposite, // after all layers composited (DOF, depth grade, bloom)
    Output,        // set_output + early-exit analysis
    PostOutput,    // post-processing / validation
};

[[nodiscard]] inline const char* to_string(GraphBuildPhase phase) {
    switch (phase) {
        case GraphBuildPhase::PreResolve:    return "PreResolve";
        case GraphBuildPhase::Resolve:       return "Resolve";
        case GraphBuildPhase::RootSources:   return "RootSources";
        case GraphBuildPhase::LayerPipeline: return "LayerPipeline";
        case GraphBuildPhase::PostComposite: return "PostComposite";
        case GraphBuildPhase::Output:        return "Output";
        case GraphBuildPhase::PostOutput:    return "PostOutput";
    }
    return "Unknown";
}

/// Entry in the graph build registry.
struct GraphBuildPassEntry {
    std::string                    id;
    GraphBuildPhase                phase;
    std::unique_ptr<GraphBuildPass> pass;
    bool                           enabled{true};
};

/// Domain-specific registry for `GraphBuildPass` instances.
///
/// Features and modules can register passes into specific phases without
/// modifying the core builder pipeline.
///
/// Singleton — use `GraphBuildRegistry::instance()`.
class GraphBuildRegistry {
public:
    static GraphBuildRegistry& instance();

    GraphBuildRegistry();
    ~GraphBuildRegistry();

    /// Register a pass into a given phase.  The `id` must be unique.
    void register_pass(std::string id,
                       GraphBuildPhase phase,
                       std::unique_ptr<GraphBuildPass> pass);

    /// Enable or disable a previously registered pass by id.
    void set_enabled(std::string_view id, bool enabled);

    /// Check if a pass with the given id is registered.
    [[nodiscard]] bool contains(std::string_view id) const;

    /// Get the number of registered passes.
    [[nodiscard]] std::size_t size() const;

    /// Get all registered pass ids.
    [[nodiscard]] std::vector<std::string> registered_ids() const;

    /// Build the default pass sequence (Resolve → RootSources → LayerPipeline → Output).
    /// Does not clear previously registered passes.
    void register_default_passes();

    /// Collect all enabled passes, sorted by phase then registration order,
    /// and move them into the given pipeline.
    void apply_to_pipeline(class GraphBuildPipeline& pipeline);

private:
    std::vector<GraphBuildPassEntry> m_entries;
};

} // namespace chronon3d::graph
