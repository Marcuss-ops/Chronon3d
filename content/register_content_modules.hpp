// Content module registration helpers.
// Registers content ExtensionModules with the ExtensionRegistry.
// Call once at startup before any CompositionRegistry is constructed.
#pragma once

namespace chronon3d {

/// Register the Minimalist content module (requires chronon3d_content_anims).
void register_minimalist_content();

/// Register the Text content module (requires chronon3d_content_text).
void register_text_content();

/// Register the 2.5D content module (requires chronon3d_content_2d5).
void register_two_point_five_d_content();

/// Register all built-in content modules and initialize them.
/// Calls all three per-module registration functions above.
/// Safe to call multiple times — modules are only initialized once.
void register_content_modules();

} // namespace chronon3d
