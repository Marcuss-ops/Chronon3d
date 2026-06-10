#include "register_content_modules.hpp"

#include <chronon3d/extension/extension_registry.hpp>

#include <memory>

namespace chronon3d {

std::unique_ptr<ExtensionModule> create_grid_module();

void register_grid_content() {
    auto& reg = ExtensionRegistry::instance();
    if (!reg.has_module("grid")) {
        reg.register_module(create_grid_module());
    }
    reg.initialize_all();
}

} // namespace chronon3d
