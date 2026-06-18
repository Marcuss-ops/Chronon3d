// Content registration — single entry point.
// Call register_content_modules() once at startup before any
// CompositionRegistry is constructed.  Safe to call multiple times.
//
// PR 7: ExtensionRegistry removed — each domain registers via
// detail::add_builtin_composition() directly.
#pragma once

namespace chronon3d {

/// Register all built-in content compositions.  Safe to call multiple
/// times (idempotent).
void register_content_modules();

} // namespace chronon3d
