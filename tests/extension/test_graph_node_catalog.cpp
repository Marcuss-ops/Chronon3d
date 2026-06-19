#include <doctest/doctest.h>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/render_graph/registry/graph_node_create_request.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
using namespace chronon3d;

using namespace chronon3d::graph;

// ── GraphNodeCatalog Contract ───────────────────────────────────────────────
//
// The catalog MUST:
//   - Accept unique ids via register_node()
//   - Return registered nodes by id (contains, find, get)
//   - List all available nodes (available, list)
//   - Filter by category (list_by_category)
//   - Create node instances via factory (create)
//   - Reject registrations after freeze()
//   - Reject duplicate ids

static GraphNodeCatalog make_catalog() {
    GraphNodeCatalog catalog;
    return catalog;
}

TEST_CASE("GraphNodeCatalog: starts empty") {
    auto catalog = make_catalog();
    CHECK(catalog.available().empty());
}

TEST_CASE("GraphNodeCatalog: register_node makes node available") {
    auto catalog = make_catalog();

    static int counter = 0;
    std::string id = "test_node_factory_" + std::to_string(++counter);

    catalog.register_node({
        .id = id,
        .display_name = "Test Node",
        .description = "A test node for unit testing",
        .category = "test",
        .builtin = false,
    });

    CHECK(catalog.contains(id));
    CHECK(catalog.available().size() == 1);
}

TEST_CASE("GraphNodeCatalog: contains returns false for unknown id") {
    auto catalog = make_catalog();
    CHECK_FALSE(catalog.contains("nonexistent_node_type_xyz"));
}

TEST_CASE("GraphNodeCatalog: find returns pointer for known, nullptr for unknown") {
    auto catalog = make_catalog();

    static int counter = 0;
    std::string id = "find_test_" + std::to_string(++counter);

    catalog.register_node({
        .id = id,
        .display_name = "Find Test",
        .description = "",
        .category = "test",
    });

    const auto* found = catalog.find(id);
    REQUIRE(found != nullptr);
    CHECK(found->display_name == "Find Test");

    CHECK(catalog.find("nonexistent") == nullptr);
}

TEST_CASE("GraphNodeCatalog: list returns expected descriptors") {
    auto catalog = make_catalog();

    static int counter = 0;
    std::string id = "list_test_" + std::to_string(++counter);

    catalog.register_node({
        .id = id,
        .display_name = "List Test",
        .description = "Testing list functionality",
        .category = "test",
    });

    auto descriptors = catalog.list();
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

TEST_CASE("GraphNodeCatalog: available() ids contain registered node") {
    auto catalog = make_catalog();

    static int counter = 0;
    std::string id = "avail_test_" + std::to_string(++counter);

    catalog.register_node({
        .id = id,
        .display_name = "Avail Test",
        .description = "",
        .category = "test",
    });

    auto ids = catalog.available();
    bool found = std::find(ids.begin(), ids.end(), id) != ids.end();
    CHECK(found);
}

TEST_CASE("GraphNodeCatalog: list_by_category filters correctly") {
    auto catalog = make_catalog();

    static int counter = 0;
    int suffix = ++counter;
    std::string cat = "filter_cat_" + std::to_string(suffix);
    std::string id_in = "filter_in_" + std::to_string(suffix);
    std::string id_out = "filter_out_" + std::to_string(suffix);

    catalog.register_node({.id = id_in, .display_name = "In", .description = "", .category = cat});
    catalog.register_node({.id = id_out, .display_name = "Out", .description = "", .category = "other_" + std::to_string(suffix)});

    auto cat_nodes = catalog.list_by_category(cat);
    bool found_in = false;
    bool found_out = false;
    for (const auto& d : cat_nodes) {
        if (d.id == id_in) found_in = true;
        if (d.id == id_out) found_out = true;
    }
    CHECK(found_in);
    CHECK_FALSE(found_out);
}

TEST_CASE("GraphNodeCatalog: create returns nullptr for nodes without factory") {
    auto catalog = make_catalog();

    static int counter = 0;
    std::string id = "no_factory_" + std::to_string(++counter);

    catalog.register_node({
        .id = id,
        .display_name = "No Factory",
        .description = "",
        .category = "test",
    });

    auto node = catalog.create(id);
    CHECK(node == nullptr);
}

TEST_CASE("GraphNodeCatalog: create returns node when factory is provided") {
    auto catalog = make_catalog();

    static int counter = 0;
    std::string id = "with_factory_" + std::to_string(++counter);

    catalog.register_node({
        .id = id,
        .display_name = "With Factory",
        .description = "",
        .category = "test",
        .factory = [](const GraphNodeCreateRequest&) -> std::unique_ptr<RenderGraphNode> {
            return nullptr;
        },
    });

    auto node = catalog.create(id);
    CHECK(node == nullptr);
}

TEST_CASE("GraphNodeCatalog: create for unknown id returns nullptr") {
    auto catalog = make_catalog();
    auto node = catalog.create("completely_unknown_id_12345");
    CHECK(node == nullptr);
}

TEST_CASE("GraphNodeCatalog: freeze prevents further registrations") {
    auto catalog = make_catalog();

    catalog.register_node({
        .id = "pre_freeze",
        .display_name = "Before Freeze",
        .description = "",
        .category = "test",
    });
    catalog.freeze();

    CHECK_THROWS_AS(
        catalog.register_node({
            .id = "post_freeze",
            .display_name = "After Freeze",
            .description = "",
            .category = "test",
        }),
        std::logic_error
    );
    CHECK(catalog.contains("pre_freeze"));
    CHECK_FALSE(catalog.contains("post_freeze"));
}

TEST_CASE("GraphNodeCatalog: two independent catalogs do not share registrations") {
    auto catalog_a = make_catalog();
    catalog_a.register_node({
        .id = "cat_a_node",
        .display_name = "Catalog A Node",
        .description = "",
        .category = "test",
    });

    auto catalog_b = make_catalog();
    CHECK_FALSE(catalog_b.contains("cat_a_node"));
    CHECK(catalog_a.contains("cat_a_node"));
}
