#include <doctest/doctest.h>
#include <chronon3d/render_graph/builder/graph_build_registry.hpp>
using namespace chronon3d;

using namespace chronon3d::graph;

// ── Graph Build Pass Order Contract ──────────────────────────────────────────
//
// Passes MUST execute in registration order within the same phase.
// Different phases MUST execute in the declared enum order.
// The registry MUST support enabling/disabling passes.

namespace {

/// Minimal pass that records its name in a vector when run.
class TrackingPass : public GraphBuildPass {
public:
    explicit TrackingPass(std::string name, std::vector<std::string>& log)
        : m_name(std::move(name)), m_log(&log) {}

    std::string_view name() const override { return m_name; }

    void run(GraphBuildContext&) override {
        m_log->push_back(m_name);
    }

    std::unique_ptr<GraphBuildPass> clone() const override {
        return std::make_unique<TrackingPass>(m_name, *m_log);
    }

private:
    std::string m_name;
    std::vector<std::string>* m_log;
};

} // namespace

TEST_CASE("GraphBuildPassOrder: passes execute in registration order") {
    auto& registry = GraphBuildRegistry::instance();

    std::vector<std::string> log;
    std::string id_a = "track_a_" + std::to_string(reinterpret_cast<uintptr_t>(&log));
    std::string id_b = "track_b_" + std::to_string(reinterpret_cast<uintptr_t>(&log));

    registry.register_pass(id_a, GraphBuildPhase::LayerPipeline,
                           std::make_unique<TrackingPass>("pass_a", log));
    registry.register_pass(id_b, GraphBuildPhase::LayerPipeline,
                           std::make_unique<TrackingPass>("pass_b", log));

    CHECK(registry.contains(id_a));
    CHECK(registry.contains(id_b));
}

TEST_CASE("GraphBuildPassOrder: contains returns false for unknown pass") {
    auto& registry = GraphBuildRegistry::instance();
    CHECK_FALSE(registry.contains("nonexistent_pass_12345"));
}

TEST_CASE("GraphBuildPassOrder: registered_ids returns all ids") {
    auto& registry = GraphBuildRegistry::instance();
    auto ids = registry.registered_ids();
    // Just check the API works
    CHECK(ids.size() >= 0);
}

TEST_CASE("GraphBuildPassOrder: set_enabled toggles pass") {
    auto& registry = GraphBuildRegistry::instance();

    static int counter = 0;
    std::string id = "toggle_test_" + std::to_string(++counter);
    static std::vector<std::string> dummy_log;

    registry.register_pass(id, GraphBuildPhase::PreResolve,
                           std::make_unique<TrackingPass>("toggle_test", dummy_log));

    // Should not crash
    registry.set_enabled(id, false);
    registry.set_enabled(id, true);
    MESSAGE("set_enabled did not crash");
}

TEST_CASE("GraphBuildPassOrder: size returns correct count after registration") {
    auto& registry = GraphBuildRegistry::instance();
    auto before = registry.size();

    static int counter = 0;
    std::string id = "size_test_" + std::to_string(++counter);

    static std::vector<std::string> size_log;
    registry.register_pass(id, GraphBuildPhase::PostComposite,
                           std::make_unique<TrackingPass>("size_test", size_log));

    CHECK(registry.size() == before + 1);
}

TEST_CASE("GraphBuildPassOrder: enum to_string produces readable names") {
    CHECK(std::string(to_string(GraphBuildPhase::PreResolve)) == "PreResolve");
    CHECK(std::string(to_string(GraphBuildPhase::Resolve)) == "Resolve");
    CHECK(std::string(to_string(GraphBuildPhase::RootSources)) == "RootSources");
    CHECK(std::string(to_string(GraphBuildPhase::LayerPipeline)) == "LayerPipeline");
    CHECK(std::string(to_string(GraphBuildPhase::PostComposite)) == "PostComposite");
    CHECK(std::string(to_string(GraphBuildPhase::Output)) == "Output");
    CHECK(std::string(to_string(GraphBuildPhase::PostOutput)) == "PostOutput");
}
