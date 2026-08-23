#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace chronon3d::runtime {

using DeviceId = std::uint32_t;

/// Resource capacity and demand dimensions for a single accelerator.
struct DeviceResourceVector {
    float compute_units{1.0f};
    std::uint64_t vram_bytes{0};
    std::uint32_t nvdec_sessions{0};
    std::uint32_t nvenc_sessions{0};
    float pcie_bandwidth{1.0f};

    friend bool operator==(const DeviceResourceVector&, const DeviceResourceVector&) = default;
};

struct DeviceResourceState {
    DeviceResourceVector capacity{};
    DeviceResourceVector reserved{};
};

struct DeviceCapabilities {
    DeviceId id{0};
    std::string name{};
    bool cuda{false};
    bool vulkan_interop{false};
    bool nvdec{false};
    bool nvenc{false};
    bool nv12{false};
    bool p010{false};
    bool h264{false};
    bool hevc{false};
    bool av1{false};
};

class DeviceScheduler;

/// RAII token representing a dynamic reservation on a device.
class DeviceReservation {
public:
    DeviceReservation() noexcept = default;
    DeviceReservation(DeviceScheduler* scheduler, DeviceId device_id, DeviceResourceVector resources) noexcept
        : m_scheduler(scheduler), m_device_id(device_id), m_resources(resources), m_active(true) {}

    ~DeviceReservation();

    DeviceReservation(const DeviceReservation&) = delete;
    DeviceReservation& operator=(const DeviceReservation&) = delete;

    DeviceReservation(DeviceReservation&& other) noexcept
        : m_scheduler(other.m_scheduler),
          m_device_id(other.m_device_id),
          m_resources(other.m_resources),
          m_active(other.m_active) {
        other.m_active = false;
        other.m_scheduler = nullptr;
    }

    DeviceReservation& operator=(DeviceReservation&& other) noexcept {
        if (this != &other) {
            release();
            m_scheduler = other.m_scheduler;
            m_device_id = other.m_device_id;
            m_resources = other.m_resources;
            m_active = other.m_active;
            other.m_active = false;
            other.m_scheduler = nullptr;
        }
        return *this;
    }

    void release() noexcept;

    [[nodiscard]] bool valid() const noexcept { return m_active; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_active; }
    [[nodiscard]] DeviceId device() const noexcept { return m_device_id; }
    [[nodiscard]] const DeviceResourceVector& resources() const noexcept { return m_resources; }

private:
    DeviceScheduler* m_scheduler{nullptr};
    DeviceId m_device_id{0};
    DeviceResourceVector m_resources{};
    bool m_active{false};
};

/// Deterministic multi-GPU/device scheduler.
class DeviceScheduler {
public:
    DeviceScheduler() = default;

    void register_device(DeviceCapabilities caps, DeviceResourceVector capacity);

    [[nodiscard]] std::optional<DeviceReservation> reserve(const DeviceResourceVector& requirements);

    void release(DeviceId id, const DeviceResourceVector& resources) noexcept;

    [[nodiscard]] std::size_t device_count() const noexcept;
    [[nodiscard]] const DeviceCapabilities* capabilities(DeviceId id) const noexcept;
    [[nodiscard]] std::optional<DeviceResourceState> resource_state(DeviceId id) const noexcept;

    /// Calculate deterministic pressure score for candidate evaluation [0.0 .. 1.0+]
    static float calculate_pressure(const DeviceResourceState& state, const DeviceResourceVector& req) noexcept;

private:
    struct DeviceEntry {
        DeviceCapabilities caps;
        DeviceResourceState state;
    };

    mutable std::mutex m_mutex;
    std::vector<DeviceEntry> m_devices;
};

} // namespace chronon3d::runtime
