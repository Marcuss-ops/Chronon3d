#pragma once

#include "text_layout_types.hpp"
#include "text_layout_helpers.hpp"

namespace chronon3d::detail::text_layout {

[[nodiscard]] TextLayoutResult layout_inline_runs(const TextLayoutInput& input);

} // namespace chronon3d::detail::text_layout
