#pragma once

#include <chrono>

namespace chronon3d::cli::detail {

using NativeAvClock = std::chrono::steady_clock;
[[nodiscard]] double elapsed_ms(const NativeAvClock::time_point& start) noexcept;

} // namespace chronon3d::cli::detail
