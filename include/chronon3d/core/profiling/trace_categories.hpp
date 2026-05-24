#pragma once

#include <string_view>

namespace chronon3d::trace_category {

inline constexpr std::string_view kGraph{"graph"};
inline constexpr std::string_view kFrame{"frame"};
inline constexpr std::string_view kNode{"node"};
inline constexpr std::string_view kEffect{"effect"};
inline constexpr std::string_view kText{"text"};
inline constexpr std::string_view kImage{"image"};
inline constexpr std::string_view kRasterize{"rasterize"};
inline constexpr std::string_view kTimeline{"timeline"};
inline constexpr std::string_view kComposite{"composite"};
inline constexpr std::string_view kDownsample{"downsample"};
inline constexpr std::string_view kPipeline{"pipeline"};
inline constexpr std::string_view kOutput{"output"};

} // namespace chronon3d::trace_category
