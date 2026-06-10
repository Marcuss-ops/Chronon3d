#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::shapes {

// Forward declarations (definitions in separate .cpp files)
Composition shape_proofs();
Composition shape_motion_proofs();

} // namespace chronon3d::content::shapes

namespace chronon3d {

class ShapesModule : public ExtensionModule {
public:
    std::string_view id() const override { return "shapes"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::shapes;

        registry.register_composition("ShapeProofs", shape_proofs);
        registry.register_composition("ShapeMotionProofs", shape_motion_proofs);
    }
};

std::unique_ptr<ExtensionModule> create_shapes_module() {
    return std::make_unique<ShapesModule>();
}

} // namespace chronon3d
