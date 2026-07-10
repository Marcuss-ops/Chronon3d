// ═══════════════════════════════════════════════════════════════════════════
// chronon3d/authoring.hpp — D2: canonical authoring umbrella.
//
// One-stop include for composition/scene building.  Covers:
//   - Scene & layer construction (SceneBuilder, LayerBuilder)
//   - Authoring facade (authoring::Layer, authoring::Text, authoring::Scene)
//   - Composition registration & props
//   - Frame context & core math types
//   - Text definition & presets
//   - Motion presets & packs
//   - Asset references
//
// Prefer this over the deprecated <chronon3d/chronon3d.hpp> umbrella.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

// ── Core types ──────────────────────────────────────────────────────
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>

// ── Scene & layer building ──────────────────────────────────────────
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/sequence_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/model/core/scene.hpp>

// ── Authoring facade ────────────────────────────────────────────────
#include <chronon3d/authoring/authoring.hpp>
#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/authoring/text.hpp>
#include <chronon3d/authoring/scene.hpp>
#include <chronon3d/authoring/animator.hpp>
#include <chronon3d/authoring/selector.hpp>
#include <chronon3d/authoring/material.hpp>
#include <chronon3d/authoring/style_registry.hpp>
#include <chronon3d/authoring/motion_registry.hpp>

// ── Composition ─────────────────────────────────────────────────────
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>

// ── Text ────────────────────────────────────────────────────────────
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/registry/text_preset_registry.hpp>

// ── Motion presets ──────────────────────────────────────────────────
#include <chronon3d/animation/core/keyframe.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/motion/motion.hpp>
#include <chronon3d/presets/motion_preset_packs.hpp>

// ── Assets ──────────────────────────────────────────────────────────
#include <chronon3d/assets/asset_ref.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/asset_manifest.hpp>

// ── Deprecation notice ──────────────────────────────────────────────
// <chronon3d/chronon3d.hpp> is superseded by this header.
// <chronon3d/runtime.hpp>     is superseded by <chronon3d/render.hpp>.
