#include "register_content_modules.hpp"

#include <chronon3d/extension/extension_registry.hpp>

#include <memory>

namespace chronon3d {

std::unique_ptr<ExtensionModule> create_effects_module();

void register_effects_content() {
    auto& reg = ExtensionRegistry::instance();
    if (!reg.has_module("effects")) {
        reg.register_module(create_effects_module());
    }
    reg.initialize_all();
}

} // namespace chronon3d
