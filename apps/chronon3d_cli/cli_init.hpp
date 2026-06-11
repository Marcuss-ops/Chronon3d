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
#include <chronon3d/core/composition/register_builtin_compositions.hpp>

namespace chronon3d::cli {

/// Register built-in content ExtensionModules and scene/animation compositions.
/// Must be called *before* CompositionRegistry is constructed so that
/// module compositions are available when populate() runs.
/// Safe to call multiple times — modules initialise only once.
inline void init_content_modules() {
#ifdef CHRONON3D_BUILD_CONTENT
    chronon3d::register_content_modules();
#endif
    // Register scene/animation built-in compositions (DarkGridBackground,
    // GridCleanBackground, CameraImageClip).  Replaces the old
    // CHRONON_REGISTER_COMPOSITION static-initialiser macros.
    chronon3d::register_builtin_compositions();
}

} // namespace chronon3d::cli
