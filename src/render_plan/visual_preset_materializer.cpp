// ─── visual_preset_materializer.cpp — VisualPresetMaterializer ─────────────
//
// VISUAL-SSOT-02 — the single materializer that consumes the existing
// VisualPresetRegistry and lowers a LayerPlan onto a fully-resolved text
// overlay.  It owns the preset→base-materializer dispatch, the style/font
// resolution, the animation intent resolution and the layout intent; final
// placement stays a separate scene-wide phase in the render-plan compiler.

#include <chronon3d/render_plan/visual_preset_materializer.hpp>

#include <chronon3d/presets/text/text_presets_v1.hpp>
#include <chronon3d/registry/style_resolver.hpp>
#include <chronon3d/registry/visual_preset_registry.hpp>
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/text/text_placement.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "visual_preset_materializer_support.inc"
#include "visual_preset_materializer_text_metrics.inc"
#include "visual_preset_materializer_image.inc"
#include "visual_preset_materializer_text.inc"
