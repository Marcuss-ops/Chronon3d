#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>

#if defined(CHRONON3D_HAS_CONTENT_MINIMALIST) || defined(CHRONON3D_HAS_CONTENT_2D5)
#include "content/register_content_modules.hpp"
#include <chronon3d/extension/extension_catalog.hpp>
#include <chronon3d/extension/extension_context.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#endif

int main(int argc, char** argv) {
    doctest::Context context;
    context.applyCommandLine(argc, argv);

    // Create a static AssetRegistry for the entire test process.
    static chronon3d::AssetRegistry test_assets;

    // Mount to current path so relative asset paths resolve correctly.
    test_assets.mount(std::filesystem::current_path());

#if defined(CHRONON3D_HAS_CONTENT_MINIMALIST) || defined(CHRONON3D_HAS_CONTENT_2D5)
    // Register content modules into a test registry via ExtensionContext.
    static chronon3d::ExtensionCatalog test_catalog;
    static chronon3d::CompositionRegistry test_registry;
    static chronon3d::graph::GraphNodeCatalog test_nodes;
    static chronon3d::effects::EffectCatalog test_effects;
    static chronon3d::ExtensionContext test_ctx{
        test_registry, test_nodes, test_effects, test_assets};
    chronon3d::register_content_modules(test_catalog, test_ctx);
#endif

    // Mount AssetRegistry to current path so that relative paths work correctly
    test_assets.mount(std::filesystem::current_path());
    
    // Enable debug logging to see why assets might fail to load
    spdlog::set_level(spdlog::level::debug);

    int res = context.run();

    if (context.shouldExit())
        return res;

    return res;
}
