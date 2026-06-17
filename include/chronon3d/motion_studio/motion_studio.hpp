// ═════════════════════════════════════════════════════════════════════════════
//  Chronon3d::motion_studio
//
//  A high-level, declarative layer on top of the Chronon3d rendering core
//  for SaaS-flavoured motion design.  Included all-at-once users should
//  only need:
//
//      #include <chronon3d/motion_studio/motion_studio.hpp>
//
//  Subsystems:
//    • layout::LayoutResolver     — flex/grid tree solver
//    • ui::UiBuilder              — button / card / input components + emit
//    • director::SequenceDirector — beat-based timing reference
//    • svg::SvgSceneImporter      — full SVG document → SceneBuilder
//    • chart::ChartRegistry       — line/bar area charts
//    • interaction::InteractionSimulator — cursor + click + typing
//
//  None of these subsystems mutate existing engine headers — they compose
//  SceneBuilder / LayerBuilder / existing motion primitives.  This keeps
//  the core engine ABI stable while allowing fast iteration on the
//  designer-facing API.
// ═════════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/motion_studio/layout/ui_node.hpp>
#include <chronon3d/motion_studio/layout/layout_resolver.hpp>
#include <chronon3d/motion_studio/director/sequence_director.hpp>
#include <chronon3d/motion_studio/ui/ui_builder.hpp>
#include <chronon3d/motion_studio/svg/svg_scene_importer.hpp>
#include <chronon3d/motion_studio/chart/chart_registry.hpp>
#include <chronon3d/motion_studio/interaction/interaction_simulator.hpp>

namespace chronon3d::motion_studio {
    // Module namespace — concrete types live in their respective sub-namespaces.
}
