#include "native_av_encoder_internal.hpp"

namespace chronon3d::cli::detail {

double elapsed_ms(const NativeAvClock::time_point& start) noexcept {
    return std::chrono::duration<double, std::milli>(NativeAvClock::now() - start).count();
}

} // namespace chronon3d::cli::detail
