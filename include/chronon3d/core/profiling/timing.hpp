#pragma once

#include <atomic>
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

    /// Retain sub-millisecond samples while preserving the legacy integer-ms
    /// counter. The quotient delta telescopes across concurrent producers;
    /// rounding happens on the accumulated total, never on each sample.
    inline void accumulate_duration_ns(std::atomic<uint64_t>& total_ns,
                                       std::atomic<uint64_t>& total_ms,
                                       uint64_t sample_ns,
                                       std::atomic<uint64_t>* total_us = nullptr) {
        constexpr uint64_t ns_per_ms = 1'000'000;
        const auto previous = total_ns.fetch_add(sample_ns, std::memory_order_relaxed);
        const auto milliseconds = (previous + sample_ns) / ns_per_ms - previous / ns_per_ms;
        if (milliseconds != 0) total_ms.fetch_add(milliseconds, std::memory_order_relaxed);
        if (total_us) {
            const auto microseconds = (previous + sample_ns) / 1000 - previous / 1000;
            if (microseconds != 0) total_us->fetch_add(microseconds, std::memory_order_relaxed);
        }
    }

    inline void accumulate_duration(std::atomic<uint64_t>& total_ns,
                                    std::atomic<uint64_t>& total_ms,
                                    Clock::time_point start, Clock::time_point end,
                                    std::atomic<uint64_t>* total_us = nullptr) {
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        if (ns > 0) accumulate_duration_ns(total_ns, total_ms, static_cast<uint64_t>(ns), total_us);
    }

    /// Nanosecond-resolution timestamp since the clock's epoch.
    inline uint64_t timestamp_ns() {
        return static_cast<uint64_t>(Clock::now().time_since_epoch().count());
    }

} // namespace profiling

} // namespace chronon3d
