#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::grid {

// Forward declarations (definitions in separate .cpp files)
Composition grid_color_showcase();

} // namespace chronon3d::content::grid

namespace chronon3d {

class GridModule : public ExtensionModule {
public:
    std::string_view id() const override { return "grid"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::grid;

        registry.register_composition("GridColorShowcase", grid_color_showcase);
    }
};

std::unique_ptr<ExtensionModule> create_grid_module() {
    return std::make_unique<GridModule>();
}

} // namespace chronon3d
