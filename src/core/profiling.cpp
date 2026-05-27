#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>

namespace chronon3d {

namespace cache {
    class FramebufferPool;
}

namespace profiling {
    thread_local RenderCounters* g_current_counters = nullptr;
    thread_local cache::FramebufferPool* g_current_framebuffer_pool = nullptr;
}

} // namespace chronon3d
