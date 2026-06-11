#pragma once

#include <chronon3d/core/profiling/trace_categories.hpp>
#include <chrono>
#include <cstdint>

namespace chronon3d {

struct RenderCounters;

namespace cache {
    class FramebufferPool;
}

namespace profiling {
    extern thread_local RenderCounters* g_current_counters;
    extern thread_local cache::FramebufferPool* g_current_framebuffer_pool;

    /// RAII guard that sets profiling thread-locals for its lifetime and
    /// restores the previous values on destruction (exception-safe).
    class ProfilingGuard {
    public:
        ProfilingGuard(RenderCounters* counters,
                       cache::FramebufferPool* pool)
            : m_previous_counters(g_current_counters),
              m_previous_pool(g_current_framebuffer_pool) {
            g_current_counters = counters;
            g_current_framebuffer_pool = pool;
        }

        ~ProfilingGuard() {
            g_current_counters = m_previous_counters;
            g_current_framebuffer_pool = m_previous_pool;
        }

        ProfilingGuard(const ProfilingGuard&) = delete;
        ProfilingGuard& operator=(const ProfilingGuard&) = delete;
        ProfilingGuard(ProfilingGuard&&) = delete;
        ProfilingGuard& operator=(ProfilingGuard&&) = delete;

    private:
        RenderCounters*            m_previous_counters;
        cache::FramebufferPool*    m_previous_pool;
    };

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

#ifdef CHRONON_PROFILING
#include <tracy/Tracy.hpp>
#define CHRONON_ZONE(name) \
    ZoneScopedN(name)
#define CHRONON_ZONE_C(name, cat) \
    ZoneScopedN(name)
#else
#ifndef ZoneScoped
#define ZoneScoped
#endif
#ifndef ZoneScopedN
#define ZoneScopedN(name)
#endif
#define CHRONON_ZONE(name) \
    do { (void)sizeof(name); } while (false)
#define CHRONON_ZONE_C(name, cat) \
    do { (void)sizeof(name); (void)sizeof(cat); } while (false)
#endif

