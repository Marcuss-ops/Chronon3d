#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// Chronon3d — Runtime Shim (DEPRECATED) — Pass D compatibility layer
//
// D2 — [[deprecated]]: prefer <chronon3d/render.hpp> for rendering.
// ═════════════════════════════════════════════════════════════════════════════
//
// *** BUILD-BREAKING REFACTOR ***  This header is no longer an umbrella.
// Expect COMPILE ERRORS at every TU that previously relied on transitive
// `Camera`, `NodeCache`, `Framebuffer`, `SoftwareRenderer`, or any other
// dropped concrete type via `#include <chronon3d/runtime.hpp>`.  Pointer
// and reference declarations still work via the forward declarations at
// the bottom of this file; VALUE instantiations (`Camera c;`, etc.) MUST
// include the canonical header for the type directly.
//
// ───────────────────────────────────────────────────────────────────────────
// What the shim still exposes (the V0.1 SDK surface):
//   - `chronon3d::sdk::RenderEngine`        (PIMPL facade)
//   - `chronon3d::sdk::RenderOutput`        (framebuffer-agnostic result)
//   - `chronon3d::sdk::RenderSettings`      (neutral POD settings)
//
// What the shim does NOT expose anymore (each must be included directly
// from its canonical header):
//   - `chronon3d::backend::SoftwareRenderer` → `<chronon3d/backends/software/software_renderer.hpp>`
//   - `chronon3d::Camera`                    → `<chronon3d/scene/model/camera/camera.hpp>`
//   - `chronon3d::Camera2_5D`                → `<chronon3d/scene/model/camera/camera_2_5d.hpp>`
//   - `chronon3d::CameraRig`                 → `<chronon3d/scene/model/camera/camera_rig.hpp>`
//   - `chronon3d::AnimatedCamera2_5D`       → `<chronon3d/scene/camera/animated_camera_2_5d.hpp>`
//   - `chronon3d::NodeCache`                 → `<chronon3d/cache/node_cache.hpp>`
//   - `chronon3d::FrameCache`                → `<chronon3d/cache/frame_cache.hpp>`
//   - `chronon3d::VideoFrameCache`           → `<chronon3d/cache/video_frame_cache.hpp>`
//   - `chronon3d::Framebuffer`               → `<chronon3d/core/memory/framebuffer.hpp>`
//   - `chronon3d::AssetRegistry`             → `<chronon3d/assets/asset_registry.hpp>`
//   - `chronon3d::FrameContext`              → `<chronon3d/core/types/frame_context.hpp>`
//   - `chronon3d::LightContext`              → `<chronon3d/rendering/light_context.hpp>`
//   - `chronon3d::RenderPreflight`           → `<chronon3d/assets/render_preflight.hpp>`
//   - `chronon3d::runtime::RenderRuntime`    → use `chronon3d::advanced::RenderEngineAccess::runtime(engine)`
//
// Migration window:
//   - For application code: include the neutral SDK headers directly and
//     use `chronon3d::sdk::RenderEngine::render(...)`.
//   - For OPP-internal / migration code that needs the legacy renderer
//     or runtime by reference: include
//     `<chronon3d/advanced/render_engine_access.hpp>` (NOT part of
//     standard V0.1 SDK package; see its banner for the full contract).
//
// Removed transitive includes (was 22, now 0):
//   `<chronon3d/backends/software/{renderer,software_renderer}.hpp>`
//   `<chronon3d/core/composition/composition_registry.hpp>`
//   `<chronon3d/core/memory/{framebuffer,arena}.hpp>`
//   `<chronon3d/core/types/frame_context.hpp>`
//   `<chronon3d/core/profiling/{profiling,counters}.hpp>`
//   `<chronon3d/cache/{node_cache,frame_cache,video_frame_cache}.hpp>`
//   `<chronon3d/scene/model/camera/{camera,camera_2_5d,camera_rig,dof}.hpp>`
//   `<chronon3d/scene/camera/animated_camera_2_5d.hpp>`
//   `<chronon3d/rendering/{light_context,lighting_eval}.hpp>`
//   `<chronon3d/math/camera_2_5d_projection.hpp>`
//   `<chronon3d/assets/{asset_registry,render_preflight}.hpp>`
// ═════════════════════════════════════════════════════════════════════════════

// D2 — [[deprecated]]: prefer <chronon3d/render.hpp>.
// (Pragma warning deferred — see docs/FOLLOWUP_TICKETS.md D2-pragma ticket.)

#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_settings.hpp>

namespace chronon3d {

// ── Forward declarations of the most common legacy types ──────────────────
//
// These are sufficient for legacy code that only declared a pointer or
// reference (`Camera* cam;` or `Framebuffer& fb;`).  Any code that
// INSTANTIATES one of these types MUST include its canonical header
// (see the migration table at the top of this file).  Each tag
// (`class` vs `struct`) matches the canonical definition; mismatched
// tags break ODR in some downstream templates.

class  SoftwareRenderer;
class  Camera;
struct Camera2_5D;
struct CameraRig;
struct AnimatedCamera2_5D;

class  NodeCache;
class  FrameCache;
class  VideoFrameCache;

class  Framebuffer;
class  AssetRegistry;

struct FrameContext;
struct LightContext;
class  RenderPreflight;

} // namespace chronon3d
