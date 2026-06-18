#pragma once

// ---------------------------------------------------------------------------
// register_builtin_compositions.hpp
//
// Replaces CHRONON_REGISTER_COMPOSITION static-initialiser macros with
// explicit registration functions.  Call register_builtin_compositions()
// once at startup (before CompositionRegistry construction) to populate
// the static builtin_composition_entries() vector so that populate() in
// CompositionRegistry finds the factories.
//
// This eliminates the need for WHOLE_ARCHIVE linker flags on targets
// that previously relied on static-initialisation registration.
// ---------------------------------------------------------------------------

namespace chronon3d {

/// Register DarkGridBackground + GridCleanBackground (src/scene/utils/dark_grid_background.cpp).
void register_scene_backgrounds();

/// Register CameraImageClip (src/scene/camera_tilt_clips.cpp).
void register_camera_tilt_clip();

/// Register all non-content built-in compositions.
/// Safe to call multiple times — add_builtin_composition checks are
/// handled by the caller (CompositionRegistry rejects duplicates).
inline void register_builtin_compositions() {
    register_scene_backgrounds();
    register_camera_tilt_clip();
}

} // namespace chronon3d
