// =============================================================================
// src/scene/camera/scene_camera_facade.cpp
//
// Out-of-line bodies for `chronon3d::SceneCameraFacade`.  The header
// (`include/chronon3d/scene/camera/scene_camera_facade.hpp`) forward-declares
// the bound types to keep the public include footprint minimal; the full
// definitions are brought in here.
//
// Implementation rule: the facade is a stateless back-reference to `Scene`.
// Every setter delegates to a `Scene` method that updates the scene's
// internal camera state.  No singletons, no registries, no caching.
// =============================================================================
#include <chronon3d/scene/camera/scene_camera_facade.hpp>

#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_catalog.hpp>
#include <chronon3d/scene/camera/camera_v1/shot_timeline.hpp>

#include <utility>

// Provide the out-of-line definition of `Scene::camera()` that returns
// a fresh `SceneCameraFacade` bound to `*this`.  Defined here (not in
// scene.hpp) to avoid pulling `scene_camera_facade.hpp` into every TU
// that includes `scene.hpp` — the facade is a public type but the
// return-by-value binding is an implementation detail of `Scene`.
#include <chronon3d/scene/camera/scene_camera_facade.hpp>

namespace chronon3d {

// Out-of-line `Scene::camera()` definition (declared in
// `include/chronon3d/scene/model/core/scene.hpp`).  Returns a fresh
// `SceneCameraFacade` bound to `*this` — the facade's back-reference
// ensures the setters route to this Scene's internal state.
SceneCameraFacade Scene::camera() noexcept {
    return SceneCameraFacade(*this);
}

SceneCameraFacade::SceneCameraFacade(Scene& scene) noexcept
    : scene_(scene) {}

SceneCameraFacade& SceneCameraFacade::descriptor(camera_v1::CameraDescriptor d) {
    scene_.set_default_camera_descriptor(std::move(d));
    return *this;
}

SceneCameraFacade& SceneCameraFacade::program(camera_v1::CameraProgram p) {
    scene_.set_default_camera_program(std::move(p));
    return *this;
}

SceneCameraFacade& SceneCameraFacade::timeline(
    std::shared_ptr<camera_v1::ShotTimeline> tl) {
    scene_.set_camera_timeline(std::move(tl));
    return *this;
}

SceneCameraFacade& SceneCameraFacade::preset(
    const std::string& preset_id,
    const CameraPresetCatalog& catalog) {
    // Resolve the preset by id and forward the resolved descriptor to
    // `descriptor(...)` so the storage path is single-source.  If the
    // catalog's resolve API uses a different name, the call site is
    // updated here (single point of maintenance).
    auto resolved = catalog.resolve(preset_id);
    if (resolved.has_value()) {
        return this->descriptor(std::move(resolved.value()));
    }
    // Resolution failure: leave the scene's descriptor untouched so the
    // call site can detect a no-op (rather than a default-constructed
    // descriptor masking the failure).  Future work (TICKET-CAMERA-FULL-LINUX
    // sub-ticket E) may surface a Result<> error here.
    return *this;
}

} // namespace chronon3d
