#pragma once

// Text is a move-only, non-owning authoring handle over the PendingTextRun
// stored by LayerBuilder. All mutators write directly into that canonical
// pending specification; destruction has no commit side effect.

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/text_direction.hpp>
#include <chronon3d/text/resolve_text_placement.hpp>

#include <chronon3d/authoring/animator.hpp>
#include <chronon3d/authoring/material.hpp>
#include <chronon3d/authoring/style_registry.hpp>
#include <chronon3d/authoring/motion_registry.hpp>
#include <chronon3d/authoring/resolution_outcome.hpp>
#include <chronon3d/assets/asset_ref.hpp>

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::authoring {

class Layer;
namespace testing { class TextRunBuilderInspector; }

#ifdef CHRONON3D_BUILD_TESTS
// Temporary compatibility for the remaining authoring test fixtures. Production
// builds expose CanvasInfo as the sole placement context.
using FrameContext [[deprecated("Use chronon3d::CanvasInfo")]] = chronon3d::CanvasInfo;
#endif

class Text {
public:
#include <chronon3d/authoring/detail/text_content_font.hpp>
#include <chronon3d/authoring/detail/text_placement_layout.hpp>
#include <chronon3d/authoring/detail/text_appearance_animation.hpp>
#include <chronon3d/authoring/detail/text_registry_access.hpp>

private:
#include <chronon3d/authoring/detail/text_private.hpp>
};

} // namespace chronon3d::authoring
