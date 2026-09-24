#pragma once

#include <cstdint>

namespace chronon3d {

struct RenderCounters;  // full definition lives in render_counter_types.hpp

namespace cache {
    class FramebufferPool;
}

namespace profiling {

    extern thread_local RenderCounters* g_current_counters;
    extern thread_local cache::FramebufferPool* g_current_framebuffer_pool;

    enum class FramebufferAllocationCategory : std::uint8_t {
        Unknown,
        Text,
        Effect,
        Glow,
        Video,
        Graph,
        Scratch,
    };

    extern thread_local FramebufferAllocationCategory g_framebuffer_allocation_category;

    // Node-memory events are attributed by a stack-scoped sink installed
    // around one graph-node execution. The callback keeps the framebuffer
    // layer independent of render-graph tracker types and does no work when
    // no node scope is active.
    enum class NodeMemoryEvent : std::uint8_t {
        Allocation,
        Copy,
        Clear,
    };

    struct NodeMemoryEventSink {
        void* context{nullptr};
        void (*record)(void*, NodeMemoryEvent, std::uint64_t) noexcept{nullptr};
    };

    extern thread_local NodeMemoryEventSink* g_node_memory_event_sink;

    inline void record_node_memory_event(
        NodeMemoryEvent event, std::uint64_t bytes = 0) noexcept {
        if (auto* sink = g_node_memory_event_sink; sink && sink->record) {
            sink->record(sink->context, event, bytes);
        }
    }

    enum class GpuUploadProducer : std::uint8_t {
        Unknown,
        Video,
        Projection,
        Composition,
        Text,
        Effects,
        Image,
        Other,
        Count,
    };

    extern thread_local GpuUploadProducer g_gpu_upload_producer;

    class GpuUploadProducerScope {
    public:
        explicit GpuUploadProducerScope(GpuUploadProducer producer)
            : m_previous(g_gpu_upload_producer) {
            g_gpu_upload_producer = producer;
        }

        ~GpuUploadProducerScope() {
            g_gpu_upload_producer = m_previous;
        }

        GpuUploadProducerScope(const GpuUploadProducerScope&) = delete;
        GpuUploadProducerScope& operator=(const GpuUploadProducerScope&) = delete;

    private:
        GpuUploadProducer m_previous;
    };

    class FramebufferAllocationScope {
    public:
        explicit FramebufferAllocationScope(FramebufferAllocationCategory category)
            : m_previous(g_framebuffer_allocation_category) {
            g_framebuffer_allocation_category = category;
        }

        ~FramebufferAllocationScope() {
            g_framebuffer_allocation_category = m_previous;
        }

        FramebufferAllocationScope(const FramebufferAllocationScope&) = delete;
        FramebufferAllocationScope& operator=(const FramebufferAllocationScope&) = delete;

    private:
        FramebufferAllocationCategory m_previous;
    };

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
