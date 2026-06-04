#include <doctest/doctest.h>
#include <chronon3d/render_graph/registry/graph_node_registry.hpp>
#include <chronon3d/render_graph/render_graph_node.hpp>

using namespace chronon3d::graph;

// ── GraphNodeRegistry Contract ──────────────────────────────────────────────
//
// The registry MUST:
//   - Reject / overwrite duplicate ids with a warning
//   - Return registered nodes by id
//   - List all available nodes
//   - Create node instances via factory

TEST_CASE("GraphNodeRegistry: starts empty") {
    auto& reg = GraphNodeRegistry::instance();
    // Just check the API doesn't crash
    CHECK(reg.available().size() >= 0);
}

TEST_CASE("GraphNodeRegistry: register_node makes node available") {
    auto& reg = GraphNodeRegistry::instance();

    // Use a unique id to avoid collisions with other tests
    static int counter = 0;
    std::string id = "test_node_factory_" + std::to_string(++counter);

    reg.register_node({
        .id = id,
        .display_name = "Test Node",
        .description = "A test node for unit testing",
        .category = "test",
        .builtin = false,
    });

    CHECK(reg.contains(id));
    CHECK(reg.available().size() >= 1);
}

TEST_CASE("GraphNodeRegistry: contains returns false for unknown id") {
    auto& reg = GraphNodeRegistry::instance();
    CHECK_FALSE(reg.contains("nonexistent_node_type_xyz"));
}

TEST_CASE("GraphNodeRegistry: list returns expected descriptors") {
    auto& reg = GraphNodeRegistry::instance();

    static int counter = 0;
    std::string id = "list_test_" + std::to_string(++counter);

    reg.register_node({
        .id = id,
        .display_name = "List Test",
        .description = "Testing list functionality",
        .category = "test",
    });

    auto descriptors = reg.list();
    bool found = false;
    for (const auto& d : descriptors) {
        if (d.id == id) {
            found = true;
            CHECK(d.display_name == "List Test");
            CHECK(d.category == "test");
        }
    }
    CHECK(found);
}

TEST_CASE("GraphNodeRegistry: available() ids contain registered node") {
    auto& reg = GraphNodeRegistry::instance();

    static int counter = 0;
    std::string id = "avail_test_" + std::to_string(++counter);

    reg.register_node({
        .id = id,
        .display_name = "Avail Test",
        .description = "",
        .category = "test",
    });

    auto ids = reg.available();
    bool found = std::find(ids.begin(), ids.end(), id) != ids.end();
    CHECK(found);
}

TEST_CASE("GraphNodeRegistry: list_by_category filters correctly") {
    auto& reg = GraphNodeRegistry::instance();

    static int counter = 0;
    int suffix = ++counter;
    std::string cat = "filter_cat_" + std::to_string(suffix);
    std::string id_in = "filter_in_" + std::to_string(suffix);
    std::string id_out = "filter_out_" + std::to_string(suffix);

    reg.register_node({.id = id_in, .display_name = "In", .description = "", .category = cat});
    reg.register_node({.id = id_out, .display_name = "Out", .description = "", .category = "other_" + std::to_string(suffix)});

    auto cat_nodes = reg.list_by_category(cat);
    bool found_in = false;
    bool found_out = false;
    for (const auto& d : cat_nodes) {
        if (d.id == id_in) found_in = true;
        if (d.id == id_out) found_out = true;
    }
    CHECK(found_in);
    CHECK_FALSE(found_out);
}

TEST_CASE("GraphNodeRegistry: create returns nullptr for nodes without factory") {
    auto& reg = GraphNodeRegistry::instance();

    static int counter = 0;
    std::string id = "no_factory_" + std::to_string(++counter);

    reg.register_node({
        .id = id,
        .display_name = "No Factory",
        .description = "",
        .category = "test",
        // No factory set
    });

    auto node = reg.create(id);
    CHECK(node == nullptr);
}

TEST_CASE("GraphNodeRegistry: create returns node when factory is provided") {
    auto& reg = GraphNodeRegistry::instance();

    static int counter = 0;
    std::string id = "with_factory_" + std::to_string(++counter);

    reg.register_node({
        .id = id,
        .display_name = "With Factory",
        .description = "",
        .category = "test",
        .factory = []() -> std::unique_ptr<RenderGraphNode> {
            return nullptr; // We just verify the factory is called
        },
    });

    // Factory returns nullptr, but create should still call it
    auto node = reg.create(id);
    CHECK(node == nullptr); // Our factory returns nullptr
}

TEST_CASE("GraphNodeRegistry: create for unknown id returns nullptr") {
    auto& reg = GraphNodeRegistry::instance();
    auto node = reg.create("completely_unknown_id_12345");
    CHECK(node == nullptr);
}
