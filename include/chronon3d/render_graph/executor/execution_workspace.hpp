#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/raster_utils.hpp>

namespace chronon3d::graph {

class ExecutionWorkspace {
public:
    ExecutionWorkspace() = default;

    ExecutionWorkspace(const ExecutionWorkspace&) = delete;
    ExecutionWorkspace& operator=(const ExecutionWorkspace&) = delete;

    void begin_frame() {
        temp.clear();
        resolved_key_digest.clear();
        resolved_frame_dependent.clear();
        resolved_cache_hit.clear();
        resolved_bboxes.clear();
        physical_slots.clear();
    }
    std::vector<CachedFB> temp;
    std::vector<u64> resolved_key_digest;
    std::vector<char> resolved_frame_dependent;
    std::vector<char> resolved_cache_hit;
    std::vector<std::optional<raster::BBox>> resolved_bboxes;
    std::vector<OwnedFB> physical_slots;

};

class ExecutionWorkspaceRing {
public:
    static constexpr std::size_t kSlotCount = 8;

    class Lease {
    public:
        Lease() = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept
            : m_ring(std::exchange(other.m_ring, nullptr)),
              m_slot(other.m_slot), m_workspace(other.m_workspace) {}
        Lease& operator=(Lease&& other) noexcept {
            if (this == &other) return *this;
            release();
            m_ring = std::exchange(other.m_ring, nullptr);
            m_slot = other.m_slot;
            m_workspace = other.m_workspace;
            return *this;
        }
        ~Lease() { release(); }
        [[nodiscard]] ExecutionWorkspace& workspace() const noexcept { return *m_workspace; }

    private:
        friend class ExecutionWorkspaceRing;
        Lease(ExecutionWorkspaceRing* ring, std::size_t slot, ExecutionWorkspace* workspace)
            : m_ring(ring), m_slot(slot), m_workspace(workspace) {}
        void release() noexcept {
            if (!m_ring) return;
            std::lock_guard lock(m_ring->m_mutex);
            m_ring->m_leased[m_slot] = false;
            m_ring = nullptr;
            m_workspace = nullptr;
        }
        ExecutionWorkspaceRing* m_ring{nullptr};
        std::size_t m_slot{0};
        ExecutionWorkspace* m_workspace{nullptr};
    };

    [[nodiscard]] Lease acquire(std::uint64_t frame_key) {
        std::lock_guard lock(m_mutex);
        const auto preferred = static_cast<std::size_t>(frame_key % kSlotCount);
        for (std::size_t offset = 0; offset < kSlotCount; ++offset) {
            const auto slot = (preferred + offset) % kSlotCount;
            if (m_leased[slot]) continue;
            m_leased[slot] = true;
            m_workspaces[slot].begin_frame();
            return Lease(this, slot, &m_workspaces[slot]);
        }
        throw std::runtime_error("ExecutionWorkspaceRing: all slots are in use");
    }

private:
    std::array<ExecutionWorkspace, kSlotCount> m_workspaces{};
    std::array<bool, kSlotCount> m_leased{};
    std::mutex m_mutex;
};

} // namespace chronon3d::graph
