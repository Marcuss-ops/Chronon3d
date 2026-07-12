// ==============================================================================
// chronon3d/scene/camera/camera_v1/legacy_camera_adapters.hpp
//
// TICKET-CAMERA-FULL-LINUX sub-ticket D — 3 NEW stateless migration adapters.
//
// These are the canonical one-way bridges between the LEGACY authoring
// surface (`chronon3d::CameraRig` / `chronon3d::AnimatedCamera2_5D` /
// `chronon3d::camera_rig::*` preset functions) and the V1 camera pipeline
// (`chronon3d::camera_v1::CameraDescriptor`).
//
// Per the user spec verbatim (Italian → English):
//   1. CameraRig        → OrbitMotion       (orbit-yaw/pitch/radius/track/dolly/roll)
//   2. AnimatedCamera2_5D → PoseTracksSource (keyframes → PoseTracksSource direct)
//   3. preset legacy    → CameraDescriptor   (compose preset fn → AnimatedCamera2_5D adapter)
//
// Distinct from camera_descriptor_adapters.hpp which bakes CameraRig into
// PoseTracksSource via 60-sample interpolation; this header is the ORBIT
// CANONICAL form for CameraRig (the user's spec lists OrbitMotion
// explicitly — the baked-PoseTracksSource form is a transitional escape
// hatch and is documented as such in camera_descriptor_adapters.hpp).
//
// Cat-3 anti-duplication:
//   * Zero new singletons / registries / resolvers.  Each adapter is a
//     stateless free function returning a CameraDescriptor by value.
//   * Zero new dependencies on the legacy authoring surface beyond what
//     the existing V1 descriptor already pulls in.
//   * All three adapters follow the same contract: input (legacy
//     authoring instance) → output (chronon3d::camera_v1::CameraDescriptor,
//     fully pre-populated, no registry lookup, no scene mutation).
//
// Planned removal: this header is part of the V1 temporary layer. It will
// be DELETED after the bulk migration reaches count = 0 + the gate moves
// to hard-zero + the physical legacy types (`CameraRig` /
// `AnimatedCamera2_5D` / `camera_rig::CameraRig` /
// `camera_rig_animated_presets.hpp` / `camera_rig_presets.*` /
// `camera_shot_profile.hpp` / `camera_descriptor_adapters.*`) are deleted
// in TICKET-CAMERA-FULL-LINUX sub-ticket D +X. Until then, this header
// is the OFFICIAL migration bridge — content/ + examples/ + tests/ +
// SceneBuilder + TimelineBuilder + CameraApi must migrate through it.
// ==============================================================================
#pragma once

#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>

#include <functional>
#include <string>

namespace chronon3d {

// Forward declarations: the legacy authoring types live in their own
// headers. We forward-declare here so this header (which is part of the
// V1 temporary layer, slated for removal after the bulk migration) does
// not drag the heavy `camera_rig.hpp` / `animated_camera_2_5d.hpp`
// includes into every TU that just wants to call the adapter.
//
// The .cpp implementation file includes the full types; consumers that
// only need `CameraDescriptor` / `OrbitMotion` / `PoseTracksSource` stay
// light.
struct AnimatedCamera2_5D;
struct CameraRig;

namespace camera_v1 {

// =========================================================================
// Adapter 1 — CameraRig (modern) → CameraDescriptor with OrbitMotion source
// =========================================================================
//
// Direct orbit mapping; unlike `camera_descriptor_from(CameraRig, ...)`
// (in camera_descriptor_adapters.hpp) which BAKES the orbit into a
// dense PoseTracksSource, this adapter assigns the `OrbitMotion` variant
// of the descriptor's `CameraSourceSpec` directly.  This is the
// canonical V1 form for orbit-driven cameras and lets the V1 runtime
// use its own orbit math (no bake interpolation loss).
//
// Mapping:
//   rig.target          → orb.target          (AnimatedValue<Vec3>)
//   rig.orbit_yaw       → orb.yaw             (AnimatedValue<float>)
//   rig.orbit_pitch     → orb.pitch           (AnimatedValue<float>)
//   rig.orbit_radius    → orb.radius          (AnimatedValue<float>)
//   rig.track           → orb.track           (AnimatedValue<Vec3>)
//   rig.dolly           → orb.dolly           (AnimatedValue<float>)
//   rig.roll            → orb.roll            (AnimatedValue<float>)
//
// Static fields (`zoom`, `fov_deg`, `lens`, `dof`, `motion_blur`) are
// copied into `CameraBaseSpec` so the projection pipeline picks them up
// after the orbit evaluation.
[[nodiscard]] CameraDescriptor
camera_descriptor_from_orbit_rig(const CameraRig& rig);

// =========================================================================
// Adapter 2 — AnimatedCamera2_5D → CameraDescriptor with PoseTracksSource
// =========================================================================
//
// Direct keyframe extraction.  `AnimatedCamera2_5D`'s `AnimatedValue<T>`
// fields are field-compatible with `PoseTracksSource`'s `AnimatedValue<T>`
// fields, so the adapter is a field-by-field copy (no baking / no
// sample-time dance): the source's existing keyframes ARE the
// PoseTracksSource keyframes.  This is simpler AND preserves the
// specification exactly — the legacy `evaluate(time)` was just doing
// `keyframe.interpolate(time)`, so the V1 runtime's `PoseTracksSource`
// evaluation reproduces the same frame values bit-exactly.
//
// Mapping (AnimatedCamera2_5D → PoseTracksSource):
//   cam.position          → pts.position
//   cam.rotation          → pts.rotation
//   cam.point_of_interest + cam.point_of_interest_enabled → pts.target + pts.use_target
//   cam.zoom              → pts.zoom
//   cam.fov_deg           → pts.fov_deg
//   cam.focus_distance    → pts.focus_distance
//   cam.aperture          → pts.aperture
//   cam.max_blur          → pts.max_blur
//
// Plus side-channel field copies:
//   cam.lens.* ▷ base.lens.*
//   cam.use_physical_model + cam.dof.* ▷ base.dof.*
//   cam.motion_blur (none on AnimatedCamera2_5D, the legacy struct
//     does not surface it) ▷ base.motion_blur stays default.
[[nodiscard]] CameraDescriptor
camera_descriptor_from_animated(const AnimatedCamera2_5D& cam);

// =========================================================================
// Adapter 3 — legacy preset function → CameraDescriptor
// =========================================================================
//
// The legacy preset namespace (`chronon3d::camera_rig::hero_push_in`,
// `orbit_yaw`, `parallax_pan`, `dolly_zoom`, `focus_pull`,
// `low_angle_reveal`, `subtle_float`) houses inline functions that
// EACH return a fully populated `AnimatedCamera2_5D` from a typed
// `*Params` struct.  This adapter takes the function as a callable
// (`std::function<AnimatedCamera2_5D()>`) plus a diagnostic name, invokes
// it, and forwards the result to Adapter 2.
//
// The `name` argument tags the resulting descriptor's `id` field so the
// OPP can log a deterministic per-preset identifier; it does NOT alter
// the produced camera state.
//
// Usage example (content/ migration):
//
//   auto dsc = camera_descriptor_from_legacy_preset(
//       "camera_rig::hero_push_in",
//       []() { return chronon3d::camera_rig::hero_push_in(my_params); });
//   auto program = compile_camera(dsc).value();
//
// This is the canonical content-side migration shape — a single inline
// adapter call replaces the legacy `SceneBuilder::animated_camera(...)`
// or `animated_camera(cam)` pattern at the call site without changing
// the call site's surrounding composition tree.
[[nodiscard]] CameraDescriptor
camera_descriptor_from_legacy_preset(
    const std::string& preset_name,
    const std::function<AnimatedCamera2_5D()>& preset_invocation);

} // namespace chronon3d::camera_v1
} // namespace chronon3d
