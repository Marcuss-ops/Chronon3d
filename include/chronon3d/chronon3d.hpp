#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// Chronon3d — Public API Umbrella Header
//
// Includes the public scene-building API types.  For backward compatibility
// this header also re-exports the full runtime and internal headers, so
// existing code that includes <chronon3d/chronon3d.hpp> continues to work.
//
// New projects should prefer including exactly what they need:
//
//   <chronon3d/chronon3d.hpp>    — scene-building API only
//   <chronon3d/runtime.hpp>      — + renderer / export / cache / camera
//   <chronon3d/internal.hpp>     — + graph / executor / effects / presets
// ═════════════════════════════════════════════════════════════════════════════

// ── Public scene-building API ──────────────────────────────────────────

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/animations.hpp>
#include <chronon3d/api/backgrounds.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/sequence.hpp>
#include <chronon3d/timeline/timeline_builder.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>
#include <chronon3d/animation/core/keyframe.hpp>
#include <chronon3d/animation/effects/animated_transform.hpp>
#include <chronon3d/animation/easing/spring.hpp>
#include <chronon3d/animation/effects/stagger.hpp>
#include <chronon3d/geometry/vertex.hpp>
#include <chronon3d/geometry/bounds.hpp>
#include <chronon3d/geometry/ray.hpp>
#include <chronon3d/geometry/mesh.hpp>

// ── Backward-compat: re-export runtime & internal headers ──────────────

#include <chronon3d/runtime.hpp>
#include <chronon3d/internal.hpp>

namespace chronon3d {
    // Umbrella header for Chronon3d
}
