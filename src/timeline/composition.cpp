// =============================================================================
// src/timeline/composition.cpp
//
// P3-F stub — NIL bodies after Composition camera state was removed.
//
// TICKET-034 history: this file used to host the bodies for
//   * Composition::default_camera_descriptor(...)             setter
//   * Composition::invalidate_default_camera_program()        cache reset
//   * Composition::default_camera_program()                   lazy compile
//   * Composition::redecompose_camera_from_descriptor(...)    inverse project
//
// All four bodies are gone now (P3-F: "remove mutable camera state"):
//   * The setter body is inlined in composition.hpp.
//   * The cache + lazy compile + inverse projection are REMOVED entirely.
//   * This TU no longer transitively pulls camera_v1/camera_program_compiler
//     or spdlog on behalf of Composition consumers.
//
// This file is intentionally kept in the build so the translation unit
// flagging future Composition-source changes stays in place; if it ever
// becomes empty enough to be linkable, the linker can fold it.
// =============================================================================

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d {
// All Composition bodies are now inlined in the header.
} // namespace chronon3d
