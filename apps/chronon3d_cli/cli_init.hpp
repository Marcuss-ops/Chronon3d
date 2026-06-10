#pragma once

// ---------------------------------------------------------------------------
// CLI Initialisation Hooks
//
// Clean separation between main.cpp entry point and library initialisation.
// Each hook lives in its own header so main.cpp only includes what it needs
// without dragging in internal content headers or other libraries.
// ---------------------------------------------------------------------------

#ifdef CHRONON3D_BUILD_CONTENT
#include <content/register_content_modules.hpp>
#endif

namespace chronon3d::cli {

/// Register built-in content ExtensionModules (Minimalist, Text, 2D5, Images
/// and effects).  Must be called *before* CompositionRegistry is constructed
/// so that module compositions are available when populate() runs.
/// Safe to call multiple times — modules initialise only once.
inline void init_content_modules() {
#ifdef CHRONON3D_BUILD_CONTENT
    chronon3d::register_content_modules();
#endif
}

} // namespace chronon3d::cli
