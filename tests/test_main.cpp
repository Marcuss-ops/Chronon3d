#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <chronon3d/registry/font_registry.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <spdlog/spdlog.h>

int main(int argc, char** argv) {
    doctest::Context context;
    context.applyCommandLine(argc, argv);

    // Global setup: Register default fonts for tests
    
    // Mount AssetRegistry to current path so that relative paths work correctly
    chronon3d::AssetRegistry::mount(std::filesystem::current_path());
    
    // Enable debug logging to see why assets might fail to load
    spdlog::set_level(spdlog::level::debug);

    chronon3d::FontRegistry::register_font({
        .family = "Inter",
        .weight = 400,
        .style = "normal",
        .path = "assets/fonts/Inter-Regular.ttf"
    });
    chronon3d::FontRegistry::register_font({
        .family = "Inter",
        .weight = 700,
        .style = "normal",
        .path = "assets/fonts/Inter-Bold.ttf"
    });

    int res = context.run();

    if (context.shouldExit())
        return res;

    return res;
}
