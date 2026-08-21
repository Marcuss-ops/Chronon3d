#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::runtime {

/// Backend execution context types supported by GpuRuntime.
enum class GpuApiKind : std::uint8_t {
    None,
    Cuda,
    Vulkan
};

/// Hardware device descriptor.
struct GpuDeviceDesc {
    std::uint32_t device_id{0};
    GpuApiKind api{GpuApiKind::None};
    std::string name;
    std::size_t total_memory_bytes{0};
};

/// Persistent engine-lifetime GPU runtime environment.
///
/// Owns the long-lived driver contexts, hardware command queues/streams,
/// compiled shader/module caches, and allocator pools across render jobs.
/// Prevents repetitive ~360ms driver initialization overhead per render session.
class GpuRuntime {
public:
    GpuRuntime() = default;
    ~GpuRuntime() { shutdown(); }

    GpuRuntime(const GpuRuntime&) = delete;
    GpuRuntime& operator=(const GpuRuntime&) = delete;
    GpuRuntime(GpuRuntime&&) = delete;
    GpuRuntime& operator=(GpuRuntime&&) = delete;

    /// Initialize hardware GPU subsystem (CUDA primary context / Vulkan device).
    /// Safe to call multiple times (idempotent).
    bool initialize(std::uint32_t device_id = 0);

    /// Query if the GPU runtime has been initialized and is ready for execution.
    [[nodiscard]] bool is_initialized() const noexcept {
        return m_initialized;
    }

    [[nodiscard]] GpuApiKind active_api() const noexcept {
        return m_api;
    }

    [[nodiscard]] std::uintptr_t native_context_handle() const noexcept {
        return m_native_context;
    }

    [[nodiscard]] std::uintptr_t default_stream_handle() const noexcept {
        return m_default_stream;
    }

    /// Shutdown and release hardware contexts.
    void shutdown() noexcept;

private:
    bool m_initialized{false};
    GpuApiKind m_api{GpuApiKind::None};
    std::uint32_t m_device_id{0};
    std::uintptr_t m_native_context{0};
    std::uintptr_t m_default_stream{0};
    mutable std::mutex m_mutex;
};

} // namespace chronon3d::runtime
