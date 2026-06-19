// Content registration — single entry point.
// Call register_content_modules() once at startup before any
// CompositionRegistry is constructed.  Safe to call multiple times.
//
// Fase 5: Now delegates to ExtensionRegistry + ContentExtension module.
#pragma once

namespace chronon3d {

/// Register all built-in content compositions.  Safe to call multiple
/// times (idempotent).
void register_content_modules();

} // namespace chronon3d
