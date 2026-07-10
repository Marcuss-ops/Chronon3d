// ═══════════════════════════════════════════════════════════════════════════
// chronon3d/advanced.hpp — D2: canonical advanced/internals umbrella.
//
// One-stop include for advanced features: render graph internals,
// effects, registries, presets, text animation, assets, camera.
//
// Prefer <chronon3d/authoring.hpp> for composition building and
// <chronon3d/render.hpp> for rendering.  Use this header only when
// working with engine internals or extending Chronon3D.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

// ── Render graph internals ──────────────────────────────────────────
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>

// ── Effects ─────────────────────────────────────────────────────────
#include <chronon3d/effects/effect_category.hpp>
#include <chronon3d/effects/effect_descriptor.hpp>
#include <chronon3d/effects/effect_instance.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/effects/effect_stage.hpp>

// ── Registries ──────────────────────────────────────────────────────
#include <chronon3d/registry/source_registry.hpp>
#include <chronon3d/registry/shape_ids.hpp>
#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/registry/sampler_registry.hpp>

// ── Scene internals ─────────────────────────────────────────────────
#include <chronon3d/scene/model/layer/mask.hpp>
#include <chronon3d/scene/model/core/mask_utils.hpp>
#include <chronon3d/scene/model/layer/layer_effect.hpp>
#include <chronon3d/scene/model/core/depth_role.hpp>

// ── Compositor ──────────────────────────────────────────────────────
#include <chronon3d/compositor/blend_mode.hpp>

// ── Presets ─────────────────────────────────────────────────────────
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_state.hpp>
#include <chronon3d/presets/motion_resolver.hpp>
#include <chronon3d/presets/motion_renderer.hpp>
#include <chronon3d/presets/motion_presets.hpp>
#include <chronon3d/presets/phrase/phrase_presets.hpp>
#include <chronon3d/presets/scene_presets.hpp>

// ── Text animation ──────────────────────────────────────────────────
#include <chronon3d/text/text_animator.hpp>

// ── Assets ──────────────────────────────────────────────────────────
#include <chronon3d/assets/render_preflight.hpp>
#include <chronon3d/assets/asset_preflight_resolver.hpp>

// ── Camera ──────────────────────────────────────────────────────────
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>

// ── Backend internals ───────────────────────────────────────────────
#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/cache/node_cache.hpp>

// ── Deprecation notice ──────────────────────────────────────────────
// <chronon3d/internal.hpp> is superseded by this header.
