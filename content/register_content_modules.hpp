// Content registration — single entry point.
// Call register_content_modules() once at startup before any
// CompositionRegistry is constructed.  Safe to call multiple times.
#pragma once

namespace chronon3d {

/// Register all built-in content compositions directly via
/// ExtensionRegistry::instance().register_composition(). Safe to
/// call multiple times (idempotent).
void register_content_modules();

} // namespace chronon3d
