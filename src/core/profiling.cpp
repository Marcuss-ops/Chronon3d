#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>

namespace chronon3d {

namespace cache {
    class FramebufferPool;
}

namespace profiling {
    thread_local RenderCounters* g_current_counters = nullptr;
    thread_local cache::FramebufferPool* g_current_framebuffer_pool = nullptr;
    thread_local FramebufferAllocationCategory g_framebuffer_allocation_category =
        FramebufferAllocationCategory::Unknown;
    thread_local GpuUploadProducer g_gpu_upload_producer = GpuUploadProducer::Unknown;
}

} // namespace chronon3d
