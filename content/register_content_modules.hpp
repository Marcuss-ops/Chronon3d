// Content module registration — single entry point.
// Call register_content_modules() once at startup before any
// CompositionRegistry is constructed.  Safe to call multiple times.
#pragma once

#include <chronon3d/extension/extension_registry.hpp>
#include <memory>

namespace chronon3d {

/// Inline helper: registers a module with the ExtensionRegistry if not
/// already present.  Used by register_content_modules() to consolidate
/// all 9 per-module registration functions into a single call site.
inline void register_module_if_needed(std::string_view id,
                                      std::unique_ptr<ExtensionModule> (*creator)()) {
    auto& reg = ExtensionRegistry::instance();
    if (!reg.has_module(id)) {
        reg.register_module(creator());
    }
}

/// Register all built-in content modules and initialize them.
/// Replaces 9 individual register_X_content() functions with a single
/// consolidated call that registers every module then calls
/// initialize_all() exactly once.
void register_content_modules();

} // namespace chronon3d
