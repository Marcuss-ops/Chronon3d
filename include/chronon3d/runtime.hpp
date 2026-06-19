#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// Chronon3d — Runtime Header
//
// Renderer/export/cache runtime types: renderer backends, frame cache,
// camera types, framebuffer, asset infrastructure, profiling counters.
//
// Include this when you need to drive the rendering pipeline, export
// video frames, or manage cached framebuffers.
//
// For scene-building API only, prefer <chronon3d/chronon3d.hpp>.
// For graph/executor internals, prefer <chronon3d/internal.hpp>.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/backends/software/renderer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>

#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/cache/frame_cache.hpp>
#include <chronon3d/cache/video_frame_cache.hpp>

#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>

#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>

#include <chronon3d/math/camera_2_5d_projection.hpp>

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/assets/render_preflight.hpp>

namespace chronon3d {
    // Runtime umbrella
}
