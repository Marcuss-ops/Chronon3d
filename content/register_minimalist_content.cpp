#include "register_content_modules.hpp"

#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>

#include <memory>

namespace chronon3d {

std::unique_ptr<ExtensionModule> create_minimalist_module();

void register_minimalist_content() {
    auto& reg = ExtensionRegistry::instance();
    if (!reg.has_module("minimalist")) {
        reg.register_module(create_minimalist_module());
    }
    reg.initialize_all();
}

} // namespace chronon3d
