// =============================================================================
// include/chronon3d/scene/camera/scene_camera_facade.hpp
//
// Public `SceneCameraFacade` — the chainable scene.camera() surface.
//
// P3-H + feat(api) public camera facade — the canonical way an external
// consumer attaches a camera to a scene.  Each setter returns
// `*this` so call sites can chain multiple operations in a single
// expression (e.g. `scene.camera().descriptor(d).timeline(tl)`).
//
// The facade is a thin back-reference to `Scene`; the actual storage
// lives on the Scene (which forwards to the underlying `Composition`).
// It hides:
//   * `chronon3d::camera_v1::CameraSession` (per-frame mutable state)
//   * `chronon3d::graph::RenderGraph`      (internal pipeline topology)
//   * the compile / evaluate pipeline internals
//
// P3-H bug fix (code-review verdict): the facade is RETURNED BY VALUE
// from `Scene::camera()`, not by reference.  This avoids the
// chicken-and-egg init-order problem (a `unique_ptr<SceneCameraFacade>`
// member would need a back-reference to `*this` which is not
// available in the member init list).  The facade is a lightweight
// 1-pointer struct (just `Scene& scene_`) — return-by-value is
// zero-allocation (NRVO) and self-contained.
//
// Cat-3 compliance: this header introduces NO new singleton / registry /
// resolver / sampler.  `SceneCameraFacade` is a stateless proxy — every
// setter just delegates to the bound `Scene`.  The Scene owns its own
// state; the facade is a back-reference.
// =============================================================================
#pragma once

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <memory>
#include <string>

namespace chronon3d {

class Scene;

// Forward declarations for opaque public types referenced by the facade
// setters.  The full definitions live in their respective public headers
// (which the consumer can include directly, OR — more commonly — never
// need to include, because the setters are pass-through).
namespace camera_v1 {
    class ShotTimeline;
}

class CameraPresetCatalog;  // forward decl; full type in camera_catalog.hpp

// ── SceneCameraFacade ───────────────────────────────────────────────────
//
// Returned BY VALUE from `Scene::camera()`.  Lightweight (one `Scene&`
// member, ~8 bytes) — copy/move are trivial.  Holds a non-owning
// back-reference to the bound Scene; all setters mutate the Scene's
// internal state via the reference.
class SceneCameraFacade {
public:
    explicit SceneCameraFacade(Scene& scene) noexcept;
    ~SceneCameraFacade() = default;

    SceneCameraFacade(const SceneCameraFacade&) = default;
    SceneCameraFacade& operator=(const SceneCameraFacade&) = default;
    SceneCameraFacade(SceneCameraFacade&&) noexcept = default;
    SceneCameraFacade& operator=(SceneCameraFacade&&) noexcept = default;

    // ── descriptor(d) — set the default-camera descriptor on the scene ──
    //
    // Forwards to the bound `Scene` which propagates it to the underlying
    // `Composition::default_camera_descriptor(...)` at evaluation time.
    SceneCameraFacade& descriptor(camera_v1::CameraDescriptor d);

    // ── program(p) — set the pre-compiled camera program ──────────────
    //
    // Forwards to `Scene::set_default_camera_program(...)`.  The OPP
    // renderer consumes the pre-compiled program at evaluation time,
    // skipping the `compile_camera(...)` step on the render hot path.
    SceneCameraFacade& program(camera_v1::CameraProgram p);

    // ── timeline(tl) — set the shot timeline ──────────────────────────
    //
    // Stores a `shared_ptr<ShotTimeline>` on the bound `Scene`.  The
    // timeline drives per-shot constraint state and transition
    // interpolation; the consumer does not need to know the
    // `CameraSession` details.
    SceneCameraFacade& timeline(std::shared_ptr<camera_v1::ShotTimeline> tl);

    // ── preset(name, catalog) — resolve a preset by name ──────────────
    //
    // Looks up `name` in `catalog` and stores the resolved descriptor
    // on the bound `Scene`.  The catalog lookup API is documented in
    // `chronon3d/scene/camera/camera_v1/camera_catalog.hpp`.
    SceneCameraFacade& preset(const std::string& preset_id,
                              const CameraPresetCatalog& catalog);

private:
    Scene& scene_;
};

} // namespace chronon3d
