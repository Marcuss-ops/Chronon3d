// ─── visual_preset_registry.cpp — VisualPresetRegistry implementation ──────
//
// VISUAL-SSOT-01 — single canonical registry for overlay-level visual
// presets. Seeds the canonical cards plus 15 simple 2D showcase variants
// (five image, five name and five important-phrase presets) with their
// default style, anchor, animation, fallback anchors and capabilities.
//
// Anti-circular-dependency: this .cpp includes ONLY the registry descriptor
// header.  No `content/text/text_*.hpp`, no SceneBuilder/LayerBuilder, no
// backend headers — the edge-direction canon (content → core/registry,
// never vice versa) is preserved.
//
// Singleton pattern mirrors `text_preset_registry.cpp`: a namespace-scope
// const object (constant-initialized, thread-safe) exposed through a
// const-ref accessor.

#include <chronon3d/registry/visual_preset_registry.hpp>

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace chronon3d::registry {

namespace {

#include "visual_preset_registry_factories.inc"

// register_builtin_visual_presets seeds the canonical catalog.  Order is
// editorial: the caption/word treatments first, then the entity cards, then
// the image treatment.
void register_builtin_visual_presets(VisualPresetRegistry& r) {
#include "visual_preset_registry_catalog_core.inc"
#include "visual_preset_registry_catalog_showcase.inc"
}

} // namespace

#include "visual_preset_registry_api.inc"
