#include <chronon3d/render_plan/render_plan_compiler.hpp>

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/prepared_text.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/timeline/composition_definition.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace chronon3d::render_plan {

#include "render_plan_compiler_support.inc"

Result<PreparedRenderPlan, PlanDecodeError>
compile_render_plan(
    const RenderPlan& plan,
    chronon3d::assets::AssetResolver& resolver,
    const RenderPlanFingerprintOptions& fingerprint_options) {
    try {
#include "render_plan_compiler_prepare.inc"
#include "render_plan_compiler_layout.inc"
#include "render_plan_compiler_scene.inc"
    } catch (const std::exception& error) {
        return PlanDecodeError{"", error.what()};
    }
}

} // namespace chronon3d::render_plan
