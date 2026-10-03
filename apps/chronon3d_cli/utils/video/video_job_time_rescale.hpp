#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/time.hpp>

#include <cstdint>

namespace chronon3d::cli::video_detail {

[[nodiscard]] Frame tick_to_frame(std::int64_t tick, std::int64_t time_base_num,
                                  std::int64_t time_base_den, FrameRate rate,
                                  FrameRounding rounding) noexcept;
[[nodiscard]] std::int64_t frame_to_tick(Frame frame, std::int64_t time_base_num,
                                         std::int64_t time_base_den, FrameRate rate,
                                         FrameRounding rounding) noexcept;

} // namespace chronon3d::cli::video_detail
