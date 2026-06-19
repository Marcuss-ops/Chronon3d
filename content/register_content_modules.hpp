// Content registration — single entry point.
// Call register_content_modules(catalog) once at startup before any
// CompositionRegistry is constructed.  Safe to call multiple times.
#pragma once

namespace chronon3d {

class ExtensionCatalog;

/// Register all built-in content compositions into the given extension
/// catalog.  Safe to call multiple times (idempotent).
void register_content_modules(ExtensionCatalog& catalog);

} // namespace chronon3d
