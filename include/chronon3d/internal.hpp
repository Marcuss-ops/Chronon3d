#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// Chronon3d — Internal Header
//
// Render graph, executor, effects, presets, compositor, text animator, and
// registry internals.  Only include this when you work with the internal
// rendering pipeline or need to extend the engine with custom nodes/effects.
//
// For scene-building API, prefer <chronon3d/chronon3d.hpp>.
// For runtime types, prefer <chronon3d/runtime.hpp>.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>

#include <chronon3d/effects/effect_category.hpp>
#include <chronon3d/effects/effect_descriptor.hpp>
#include <chronon3d/effects/effect_instance.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <chronon3d/effects/effect_registry.hpp>
#include <chronon3d/effects/effect_stage.hpp>

#include <chronon3d/registry/source_registry.hpp>
#include <chronon3d/registry/shape_ids.hpp>
#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/registry/sampler_registry.hpp>

#include <chronon3d/scene/model/layer/mask.hpp>
#include <chronon3d/scene/model/core/mask_utils.hpp>
#include <chronon3d/scene/model/layer/layer_effect.hpp>
#include <chronon3d/scene/model/core/depth_role.hpp>

#include <chronon3d/compositor/blend_mode.hpp>

#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_state.hpp>
#include <chronon3d/presets/motion_resolver.hpp>
#include <chronon3d/presets/motion_renderer.hpp>
#include <chronon3d/presets/motion_presets.hpp>
#include <chronon3d/presets/phrase/phrase_presets.hpp>
#include <chronon3d/presets/scene_presets.hpp>

#include <chronon3d/text/text_animator.hpp>

namespace chronon3d {
    // Internal umbrella
}
