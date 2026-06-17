#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

#if defined(CHRONON3D_HAS_CONTENT_MINIMALIST) || defined(CHRONON3D_HAS_CONTENT_2D5)
#include <content/register_content_modules.hpp>
using namespace chronon3d;
#endif

int main(int argc, char** argv) {
    doctest::Context context;
    context.applyCommandLine(argc, argv);

#if defined(CHRONON3D_HAS_CONTENT_MINIMALIST) || defined(CHRONON3D_HAS_CONTENT_2D5)
    chronon3d::register_content_modules();
#endif

    // Mount AssetRegistry to current path so that relative paths work correctly
    chronon3d::AssetRegistry::mount(std::filesystem::current_path());
    
    // Enable debug logging to see why assets might fail to load
    spdlog::set_level(spdlog::level::debug);

    int res = context.run();

    if (context.shouldExit())
        return res;

    return res;
}
