// Content module registration helpers.
// Registers content ExtensionModules with the ExtensionRegistry.
// Call once at startup before any CompositionRegistry is constructed.
#pragma once

namespace chronon3d {

/// Register the Minimalist content module (requires chronon3d_content_anims).
void register_minimalist_content();

/// Register the 2.5D content module (requires chronon3d_content_2d5).
void register_two_point_five_d_content();

/// Register the Images content module (requires chronon3d_content_images).
/// Exposes ImgGradient, ImgChecker, ImgGridTest, ImgTestPattern,
/// ImgShakeZoom, ImgReferenceShakeReveal, ImgCornerSmoothing, ImageProofs.
void register_images_content();

/// Register the Shapes content module.
void register_shapes_content();

/// Register the Animation compositions module.
void register_anims_content();

/// Register the Grid content module.
void register_grid_content();

/// Register the Effects content module.
void register_effects_content();

/// Register all built-in content modules and initialize them.
/// Calls all per-module registration functions above.
/// Safe to call multiple times — modules are only initialized once.
void register_content_modules();

} // namespace chronon3d
