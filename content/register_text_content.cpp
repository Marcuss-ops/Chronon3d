#include "register_content_modules.hpp"

#include <chronon3d/extension/extension_registry.hpp>

#include <memory>

namespace chronon3d {

std::unique_ptr<ExtensionModule> create_text_module();

void register_text_content() {
    auto& reg = ExtensionRegistry::instance();
    if (!reg.has_module("text")) {
        reg.register_module(create_text_module());
    }
    // Initialize all registered modules (safe to call multiple times).
    reg.initialize_all();
}

} // namespace chronon3d
