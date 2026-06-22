// ==============================================================================
// tests/scene/camera/test_camera_registry.cpp
//
// PR2+ — Camera V1 registration tests.
//
// CameraMotionRegistry removed (PR7), CameraConstraintRegistry removed (PR9).
// Factory functions removed (PR12).  The legacy static-singleton
// `register_camera_v1_builtins()` (PR12) and `get_transition_catalog()`
// (CAM-05) were removed: both violated the per-job ownership rule
// (DOC 03 §5) by exposing a process-wide singleton.  The replacement
// per-job API is `register_camera_v1_builtins_into(catalog)` — callers
// inject their own catalog reference.
//
// 2 TEST_CASE:
//   1. register_camera_v1_builtins_into() injects the 5 built-in transitions
//      into a caller-owned catalog (Cut / SmoothBlend / Push / WhipPan /
//      FocusHandoff).
//   2. register_camera_v1_builtins_into() is idempotent — calling twice on
//      the same catalog yields the same frozen state without errors.
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/register_camera_v1.hpp>
#include <chronon3d/scene/camera/camera_v1/shot_timeline.hpp>

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {

// ==============================================================================
// 1 — register_camera_v1_builtins_into() injects the 5 built-in transitions.
// ==============================================================================
TEST_CASE("CAM-05: register_camera_v1_builtins_into() injects the 5 built-in transitions") {
    // Caller-owned catalog — no global state involved (DOC 03 §5).
    CameraTransitionCatalog catalog;
    register_camera_v1_builtins_into(catalog);

    // Default policy: after the call the catalog is frozen.
    CHECK(catalog.is_frozen());

    // Each registered transition must be present in the catalog.
    CHECK(catalog.has(CameraTransitionKind::Cut));
    CHECK(catalog.has(CameraTransitionKind::SmoothBlend));
    CHECK(catalog.has(CameraTransitionKind::Push));
    CHECK(catalog.has(CameraTransitionKind::WhipPan));
    CHECK(catalog.has(CameraTransitionKind::FocusHandoff));

    // Exercise ALL 5 transition factories to confirm the catalog itself
    // works (a registry without working factories is a hollow test).
    // Full coverage is intentional: any future PR that adds SDK-callback
    // wiring or factory-registration branching to a specific kind will
    // immediately surface here.  Per-kind semantic coverage:
    //   Cut           = discrete step (basic factory)
    //   SmoothBlend   = eased blend (easing registrar)
    //   WhipPan       = large angular delta (vector-delta registrar)
    //   Push          = forward-push (translation-only registrar)
    //   FocusHandoff  = dof-anchor shift (focus registrar)
    auto cut      = catalog.create(CameraTransitionKind::Cut);
    auto blend    = catalog.create(CameraTransitionKind::SmoothBlend);
    auto whippan  = catalog.create(CameraTransitionKind::WhipPan);
    auto push     = catalog.create(CameraTransitionKind::Push);
    auto handoff  = catalog.create(CameraTransitionKind::FocusHandoff);
    CHECK(cut != nullptr);
    CHECK(blend != nullptr);
    CHECK(whippan != nullptr);
    CHECK(push != nullptr);
    CHECK(handoff != nullptr);
}

// ==============================================================================
// 2 — register_camera_v1_builtins_into() is idempotent.
// ==============================================================================
TEST_CASE("CAM-05: register_camera_v1_builtins_into() is idempotent") {
    CameraTransitionCatalog catalog;

    // First call populates and freezes.
    register_camera_v1_builtins_into(catalog);
    CHECK(catalog.is_frozen());

    // Second call must be a no-op (frozen catalog ignores new
    // registrations). The function's documentation calls this out and
    // here we verify it on a real catalog without crashing.
    register_camera_v1_builtins_into(catalog);

    // Catalog state unchanged: still frozen, same 5 transitions.
    CHECK(catalog.is_frozen());
    CHECK(catalog.has(CameraTransitionKind::Cut));
    CHECK(catalog.has(CameraTransitionKind::SmoothBlend));
    CHECK(catalog.has(CameraTransitionKind::Push));
    CHECK(catalog.has(CameraTransitionKind::WhipPan));
    CHECK(catalog.has(CameraTransitionKind::FocusHandoff));
}

} // namespace
