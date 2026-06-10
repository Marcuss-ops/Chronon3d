#include "register_content_modules.hpp"

#include <chronon3d/extension/extension_registry.hpp>

#include <memory>

namespace chronon3d {

std::unique_ptr<ExtensionModule> create_anims_module();

void register_anims_content() {
    auto& reg = ExtensionRegistry::instance();
    if (!reg.has_module("anims")) {
        reg.register_module(create_anims_module());
    }
    reg.initialize_all();
}

} // namespace chronon3d
