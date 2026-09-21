#pragma once

#include "gop_smart_copy.hpp"

#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/cpu_budget.hpp>

#include "../../commands/video/common/video_export_common.hpp"

#include <optional>

namespace chronon3d::cli {

/// Resolve the stream contract that a GOP schedule is allowed to compare
/// against. Remux owns the source contract; every re-encode owns an encoder
/// contract derived from the exact pipe options it will apply.
[[nodiscard]] std::optional<BitstreamTargetContract>
resolve_gop_target_contract(const FfmpegExportOptions& opts,
                            const CompiledComposition& compiled,
                            const chronon3d::CpuBudget& cpu_budget);

} // namespace chronon3d::cli
