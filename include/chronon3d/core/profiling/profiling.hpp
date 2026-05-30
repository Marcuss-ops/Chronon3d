#pragma once

#include <chronon3d/core/profiling/trace_categories.hpp>

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

