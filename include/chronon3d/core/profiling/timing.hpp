#pragma once

#include <chrono>
#include <cstdint>

namespace chronon3d {

namespace profiling {

    // ── Centralised timing helpers ────────────────────────────────────
    // Replace ad-hoc std::chrono::steady_clock::now() call sites with
    // these lightweight wrappers for consistency and readability.

    using Clock = std::chrono::steady_clock;

    /// Returns a steady_clock time_point.
    inline Clock::time_point now() { return Clock::now(); }

    /// Elapsed milliseconds since @p start (calls now() internally).
    inline double elapsed_ms(Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    /// Elapsed microseconds since @p start.
    inline double elapsed_us(Clock::time_point start) {
        return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    }

    /// Elapsed seconds since @p start.
    inline double elapsed_s(Clock::time_point start) {
        return std::chrono::duration<double>(Clock::now() - start).count();
    }

    /// Milliseconds between two recorded time points (no now() call).
    inline double duration_ms(Clock::time_point start, Clock::time_point end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    /// Microseconds between two recorded time points.
    inline double duration_us(Clock::time_point start, Clock::time_point end) {
        return std::chrono::duration<double, std::micro>(end - start).count();
    }

    /// Nanosecond-resolution timestamp since the clock's epoch.
    inline uint64_t timestamp_ns() {
        return static_cast<uint64_t>(Clock::now().time_since_epoch().count());
    }

} // namespace profiling

} // namespace chronon3d
