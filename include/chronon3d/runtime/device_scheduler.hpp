#pragma once

#include <algorithm>
#include <array>
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
    std::uint32_t physical_device_index{0};
    // CUDA ordinal is an implementation detail of this process and is not
    // interchangeable with the scheduler's stable logical DeviceId.
    std::int32_t cuda_device_ordinal{-1};
    std::array<std::uint8_t, 16> uuid{};
    bool has_uuid{false};
    std::uint32_t pci_domain{0};
    std::uint32_t pci_bus{0};
    std::uint32_t pci_device{0};
    std::uint32_t pci_function{0};
    bool has_pci_identity{false};
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

/// Capability and resource requirements for a placement decision.
struct DeviceSelectionRequirements {
    DeviceResourceVector resources{};
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

    /// Discovery-only operation. The first reservation seals discovery so the
    /// capability table is immutable for the lifetime of active scheduling.
    void register_device(DeviceCapabilities caps, DeviceResourceVector capacity);

    [[nodiscard]] std::optional<DeviceReservation> reserve(const DeviceResourceVector& requirements);
    [[nodiscard]] std::optional<DeviceReservation> reserve(const DeviceSelectionRequirements& requirements);

    void release(DeviceId id, const DeviceResourceVector& resources) noexcept;

    [[nodiscard]] std::size_t device_count() const noexcept;
    /// Compatibility-only pointer API. The returned pointer refers to a
    /// thread-local snapshot, never to storage protected by m_mutex. New code
    /// must use capability_snapshot().
    [[deprecated("use capability_snapshot(); raw pointers must not escape scheduler storage")]]
    [[nodiscard]] const DeviceCapabilities* capabilities(DeviceId id) const noexcept;
    [[nodiscard]] std::optional<DeviceCapabilities> capability_snapshot(DeviceId id) const;
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
    bool m_discovery_sealed{false};

    void seal_discovery_locked() noexcept { m_discovery_sealed = true; }
    [[nodiscard]] std::optional<DeviceReservation> reserve_locked(
        const DeviceResourceVector& requirements,
        const DeviceSelectionRequirements* capabilities);
};

} // namespace chronon3d::runtime
