#include "video_job_time_rescale.hpp"

#include <limits>

namespace chronon3d::cli::video_detail {
namespace {

std::int64_t rescale_ticks_exact(std::int64_t ticks, std::int64_t from_num,
                                 std::int64_t from_den, std::int64_t to_num,
                                 std::int64_t to_den,
                                 FrameRounding rounding) noexcept {
    if (from_num <= 0 || from_den <= 0 || to_num <= 0 || to_den <= 0) return 0;
    const __int128 numerator = static_cast<__int128>(ticks) * from_num * to_num;
    const __int128 denominator = static_cast<__int128>(from_den) * to_den;
    if (denominator == 0) return 0;
    __int128 result = 0;
    switch (rounding) {
        case FrameRounding::Floor:
            result = numerator >= 0
                ? numerator / denominator
                : -((-numerator + denominator - 1) / denominator);
            break;
        case FrameRounding::Ceiling:
            result = numerator >= 0
                ? (numerator + denominator - 1) / denominator
                : -((-numerator) / denominator);
            break;
        case FrameRounding::Nearest:
        default:
            result = numerator >= 0
                ? (numerator + denominator / 2) / denominator
                : -((-numerator + denominator / 2) / denominator);
            break;
    }
    if (result > std::numeric_limits<std::int64_t>::max())
        return std::numeric_limits<std::int64_t>::max();
    if (result < std::numeric_limits<std::int64_t>::min())
        return std::numeric_limits<std::int64_t>::min();
    return static_cast<std::int64_t>(result);
}

} // namespace

Frame tick_to_frame(std::int64_t tick, std::int64_t time_base_num,
                    std::int64_t time_base_den, FrameRate rate,
                    FrameRounding rounding) noexcept {
    return Frame{rescale_ticks_exact(tick, time_base_num, time_base_den,
                                     rate.numerator, rate.denominator, rounding)};
}

std::int64_t frame_to_tick(Frame frame, std::int64_t time_base_num,
                           std::int64_t time_base_den, FrameRate rate,
                           FrameRounding rounding) noexcept {
    return rescale_ticks_exact(frame.integral(), rate.numerator,
                               rate.denominator, time_base_num, time_base_den, rounding);
}

} // namespace chronon3d::cli::video_detail
