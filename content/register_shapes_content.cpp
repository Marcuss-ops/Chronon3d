#include "register_content_modules.hpp"

#include <chronon3d/extension/extension_registry.hpp>

#include <memory>

namespace chronon3d {

std::unique_ptr<ExtensionModule> create_shapes_module();

void register_shapes_content() {
    auto& reg = ExtensionRegistry::instance();
    if (!reg.has_module("shapes")) {
        reg.register_module(create_shapes_module());
    }
    reg.initialize_all();
}

} // namespace chronon3d
