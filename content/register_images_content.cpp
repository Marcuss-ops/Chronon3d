#include "register_content_modules.hpp"

#include <chronon3d/extension/extension_registry.hpp>

#include <memory>

namespace chronon3d {

std::unique_ptr<ExtensionModule> create_images_module();

void register_images_content() {
    auto& reg = ExtensionRegistry::instance();
    if (!reg.has_module("images")) {
        reg.register_module(create_images_module());
    }
    // Initialize all registered modules (safe to call multiple times).
    reg.initialize_all();
}

} // namespace chronon3d
