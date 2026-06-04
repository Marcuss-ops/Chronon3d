#include <chronon3d/render_graph/builder/graph_build_registry.hpp>
#include <chronon3d/render_graph/builder/graph_build_pipeline.hpp>
#include "passes/graph_builder_passes.hpp"
#include "passes/graph_builder_validation_pass.hpp"

#include <algorithm>
#include <stdexcept>

namespace chronon3d::graph {

GraphBuildRegistry& GraphBuildRegistry::instance() {
    static GraphBuildRegistry s_instance;
    return s_instance;
}

GraphBuildRegistry::GraphBuildRegistry() = default;
GraphBuildRegistry::~GraphBuildRegistry() = default;

void GraphBuildRegistry::register_pass(std::string id,
                                       GraphBuildPhase phase,
                                       std::unique_ptr<GraphBuildPass> pass) {
    if (!pass) {
        throw std::invalid_argument("GraphBuildRegistry::register_pass: null pass");
    }
    // Check for duplicate id.
    for (const auto& e : m_entries) {
        if (e.id == id) {
            throw std::invalid_argument(
                "GraphBuildRegistry::register_pass: duplicate id '" + id + "'");
        }
    }
    m_entries.push_back(GraphBuildPassEntry{
        .id    = std::move(id),
        .phase = phase,
        .pass  = std::move(pass),
        .enabled = true,
    });
}

void GraphBuildRegistry::set_enabled(std::string_view id, bool enabled) {
    for (auto& e : m_entries) {
        if (e.id == id) {
            e.enabled = enabled;
            return;
        }
    }
    throw std::invalid_argument(
        std::string("GraphBuildRegistry::set_enabled: unknown id '") +
        std::string(id) + "'");
}

bool GraphBuildRegistry::contains(std::string_view id) const {
    return std::any_of(m_entries.begin(), m_entries.end(),
                       [id](const auto& e) { return e.id == id; });
}

std::size_t GraphBuildRegistry::size() const {
    return m_entries.size();
}

std::vector<std::string> GraphBuildRegistry::registered_ids() const {
    std::vector<std::string> ids;
    ids.reserve(m_entries.size());
    for (const auto& e : m_entries) {
        ids.push_back(e.id);
    }
    return ids;
}

void GraphBuildRegistry::register_default_passes() {
    // Only register defaults if no passes exist yet (idempotent).
    if (!m_entries.empty()) return;

    register_pass("validation",    GraphBuildPhase::PreResolve,
                  std::make_unique<detail::ValidationPass>());
    register_pass("resolve",       GraphBuildPhase::Resolve,
                  std::make_unique<detail::ResolvePass>());
    register_pass("root_sources",  GraphBuildPhase::RootSources,
                  std::make_unique<detail::RootSourcesPass>());
    register_pass("layer_pipeline", GraphBuildPhase::LayerPipeline,
                  std::make_unique<detail::LayerPipelinePass>());
    register_pass("output",        GraphBuildPhase::Output,
                  std::make_unique<detail::OutputPass>());
}

void GraphBuildRegistry::apply_to_pipeline(GraphBuildPipeline& pipeline) {
    // Sort: first by phase, then by insertion order (stable).
    std::vector<const GraphBuildPassEntry*> sorted;
    sorted.reserve(m_entries.size());
    for (const auto& e : m_entries) {
        if (e.enabled) {
            sorted.push_back(&e);
        }
    }
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const GraphBuildPassEntry* a, const GraphBuildPassEntry* b) {
                         return static_cast<int>(a->phase) < static_cast<int>(b->phase);
                     });

    // Clone passes — the registry retains ownership of the originals.
    // This makes apply_to_pipeline idempotent: it can be called multiple
       // times (e.g. once per GraphBuilder::build() call) without draining
    // the registry.
    for (const auto* entry : sorted) {
        pipeline.add_pass(entry->pass->clone());
    }
}

} // namespace chronon3d::graph
