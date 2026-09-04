#include <chronon3d/runtime/device_scheduler.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace chronon3d::runtime {

DeviceReservation::~DeviceReservation() {
    release();
}

void DeviceReservation::release() noexcept {
    if (m_active && m_scheduler) {
        m_scheduler->release(m_device_id, m_resources);
        m_active = false;
        m_scheduler = nullptr;
    }
}

void DeviceScheduler::register_device(DeviceCapabilities caps, DeviceResourceVector capacity) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_discovery_sealed) {
        throw std::logic_error("DeviceScheduler discovery is sealed after reservation begins");
    }
    caps.id = static_cast<DeviceId>(m_devices.size());
    DeviceResourceState state{};
    state.capacity = capacity;
    state.reserved = DeviceResourceVector{0.0f, 0, 0, 0, 0.0f};
    m_devices.push_back(DeviceEntry{
        .caps = std::move(caps),
        .state = state,
    });
}

float DeviceScheduler::calculate_pressure(const DeviceResourceState& state, const DeviceResourceVector& req) noexcept {
    float compute_p = (state.capacity.compute_units > 0.0f)
        ? (state.reserved.compute_units + req.compute_units) / state.capacity.compute_units
        : 0.0f;
    float vram_p = (state.capacity.vram_bytes > 0)
        ? static_cast<float>(state.reserved.vram_bytes + req.vram_bytes) / static_cast<float>(state.capacity.vram_bytes)
        : 0.0f;
    float nvdec_p = (state.capacity.nvdec_sessions > 0)
        ? static_cast<float>(state.reserved.nvdec_sessions + req.nvdec_sessions) / static_cast<float>(state.capacity.nvdec_sessions)
        : 0.0f;
    float nvenc_p = (state.capacity.nvenc_sessions > 0)
        ? static_cast<float>(state.reserved.nvenc_sessions + req.nvenc_sessions) / static_cast<float>(state.capacity.nvenc_sessions)
        : 0.0f;
    const float pcie_p = (state.capacity.pcie_bandwidth > 0.0f)
        ? (state.reserved.pcie_bandwidth + req.pcie_bandwidth) / state.capacity.pcie_bandwidth
        : 0.0f;

    return std::max({compute_p, vram_p, nvdec_p, nvenc_p, pcie_p});
}

std::optional<DeviceReservation> DeviceScheduler::reserve(const DeviceResourceVector& requirements) {
    std::lock_guard<std::mutex> lock(m_mutex);
    seal_discovery_locked();
    return reserve_locked(requirements, nullptr);
}

std::optional<DeviceReservation> DeviceScheduler::reserve(const DeviceSelectionRequirements& requirements) {
    std::lock_guard<std::mutex> lock(m_mutex);
    seal_discovery_locked();
    return reserve_locked(requirements.resources, &requirements);
}

std::optional<DeviceReservation> DeviceScheduler::reserve_locked(
    const DeviceResourceVector& requirements,
    const DeviceSelectionRequirements* capabilities) {
    if (m_devices.empty()) {
        return std::nullopt;
    }

    int best_index = -1;
    float min_peak_pressure = std::numeric_limits<float>::infinity();
    float min_total_pressure = std::numeric_limits<float>::infinity();

    for (std::size_t i = 0; i < m_devices.size(); ++i) {
        const auto& entry = m_devices[i];

        if (capabilities &&
            ((capabilities->cuda && !entry.caps.cuda) ||
             (capabilities->vulkan_interop && !entry.caps.vulkan_interop) ||
             (capabilities->nvdec && !entry.caps.nvdec) ||
             (capabilities->nvenc && !entry.caps.nvenc) ||
             (capabilities->nv12 && !entry.caps.nv12) ||
             (capabilities->p010 && !entry.caps.p010) ||
             (capabilities->h264 && !entry.caps.h264) ||
             (capabilities->hevc && !entry.caps.hevc) ||
             (capabilities->av1 && !entry.caps.av1))) {
            continue;
        }

        if (entry.state.capacity.compute_units > 0.0f &&
            (entry.state.reserved.compute_units + requirements.compute_units > entry.state.capacity.compute_units)) {
            continue;
        }
        if (entry.state.capacity.vram_bytes > 0 &&
            (entry.state.reserved.vram_bytes + requirements.vram_bytes > entry.state.capacity.vram_bytes)) {
            continue;
        }
        if (entry.state.capacity.nvdec_sessions > 0 &&
            (entry.state.reserved.nvdec_sessions + requirements.nvdec_sessions > entry.state.capacity.nvdec_sessions)) {
            continue;
        }
        if (entry.state.capacity.nvenc_sessions > 0 &&
            (entry.state.reserved.nvenc_sessions + requirements.nvenc_sessions > entry.state.capacity.nvenc_sessions)) {
            continue;
        }
        if (entry.state.capacity.pcie_bandwidth > 0.0f &&
            entry.state.reserved.pcie_bandwidth + requirements.pcie_bandwidth >
                entry.state.capacity.pcie_bandwidth) {
            continue;
        }

        const float peak_p = calculate_pressure(entry.state, requirements);
        float comp_p = (entry.state.capacity.compute_units > 0.0f)
            ? (entry.state.reserved.compute_units + requirements.compute_units) / entry.state.capacity.compute_units : 0.0f;
        float vram_p = (entry.state.capacity.vram_bytes > 0)
            ? static_cast<float>(entry.state.reserved.vram_bytes + requirements.vram_bytes) / static_cast<float>(entry.state.capacity.vram_bytes) : 0.0f;
        float dec_p = (entry.state.capacity.nvdec_sessions > 0)
            ? static_cast<float>(entry.state.reserved.nvdec_sessions + requirements.nvdec_sessions) / static_cast<float>(entry.state.capacity.nvdec_sessions) : 0.0f;
        float enc_p = (entry.state.capacity.nvenc_sessions > 0)
            ? static_cast<float>(entry.state.reserved.nvenc_sessions + requirements.nvenc_sessions) / static_cast<float>(entry.state.capacity.nvenc_sessions) : 0.0f;
        const float pcie_p = (entry.state.capacity.pcie_bandwidth > 0.0f)
            ? (entry.state.reserved.pcie_bandwidth + requirements.pcie_bandwidth) /
                entry.state.capacity.pcie_bandwidth
            : 0.0f;
        const float sum_p = comp_p + vram_p + dec_p + enc_p + pcie_p;

        if (peak_p < min_peak_pressure || (std::abs(peak_p - min_peak_pressure) < 1e-6f && sum_p < min_total_pressure)) {
            min_peak_pressure = peak_p;
            min_total_pressure = sum_p;
            best_index = static_cast<int>(i);
        }
    }

    if (best_index < 0) {
        return std::nullopt;
    }

    auto& chosen = m_devices[static_cast<std::size_t>(best_index)];
    chosen.state.reserved.compute_units += requirements.compute_units;
    chosen.state.reserved.vram_bytes += requirements.vram_bytes;
    chosen.state.reserved.nvdec_sessions += requirements.nvdec_sessions;
    chosen.state.reserved.nvenc_sessions += requirements.nvenc_sessions;
    chosen.state.reserved.pcie_bandwidth += requirements.pcie_bandwidth;

    return DeviceReservation(this, chosen.caps.id, requirements);
}

void DeviceScheduler::release(DeviceId id, const DeviceResourceVector& resources) noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id >= m_devices.size()) {
        return;
    }

    auto& entry = m_devices[id];
    entry.state.reserved.compute_units = std::max(0.0f, entry.state.reserved.compute_units - resources.compute_units);
    entry.state.reserved.vram_bytes = (entry.state.reserved.vram_bytes >= resources.vram_bytes)
        ? entry.state.reserved.vram_bytes - resources.vram_bytes : 0;
    entry.state.reserved.nvdec_sessions = (entry.state.reserved.nvdec_sessions >= resources.nvdec_sessions)
        ? entry.state.reserved.nvdec_sessions - resources.nvdec_sessions : 0;
    entry.state.reserved.nvenc_sessions = (entry.state.reserved.nvenc_sessions >= resources.nvenc_sessions)
        ? entry.state.reserved.nvenc_sessions - resources.nvenc_sessions : 0;
    entry.state.reserved.pcie_bandwidth = std::max(
        0.0f, entry.state.reserved.pcie_bandwidth - resources.pcie_bandwidth);
}

std::size_t DeviceScheduler::device_count() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_devices.size();
}

const DeviceCapabilities* DeviceScheduler::capabilities(DeviceId id) const noexcept {
    // Preserve the exported/source-compatible symbol without leaking an address
    // into m_devices after m_mutex is released. The compatibility pointer now
    // refers to a per-thread copy; capability_snapshot() is the canonical API.
    thread_local std::optional<DeviceCapabilities> snapshot;
    snapshot = capability_snapshot(id);
    return snapshot ? &*snapshot : nullptr;
}

std::optional<DeviceCapabilities> DeviceScheduler::capability_snapshot(DeviceId id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id >= m_devices.size()) return std::nullopt;
    return m_devices[id].caps;
}

std::optional<DeviceResourceState> DeviceScheduler::resource_state(DeviceId id) const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id >= m_devices.size()) return std::nullopt;
    return m_devices[id].state;
}

} // namespace chronon3d::runtime
