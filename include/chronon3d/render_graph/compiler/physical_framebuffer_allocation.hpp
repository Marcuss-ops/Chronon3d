#pragma once

#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace chronon3d::graph {

inline constexpr std::uint32_t kInvalidPhysicalFramebufferSlot =
    std::numeric_limits<std::uint32_t>::max();

/// A physical backing slot shared by non-overlapping transient resources.
/// Dimensions are filled by the executor for the current frame because the
/// compiled graph is intentionally independent of render resolution.
struct FramebufferSlot {
    std::uint32_t id{0};
    std::size_t max_width{0};
    std::size_t max_height{0};
};

/// Compiler assignment for one logical node output.
struct ResourceAllocation {
    GraphNodeId producer{k_invalid_node};
    std::uint32_t physical_slot{kInvalidPhysicalFramebufferSlot};
    bool aliasable{false};
    bool persistent{false};
    bool async_use{false};
};

/// Deterministic interval-coloring result.  A slot may be assigned to two
/// resources only when the first resource's last execution level is strictly
/// before the second resource's first level.  Persistent and non-releasable
/// resources deliberately receive no slot assignment.
struct PhysicalFramebufferAllocationPlan {
    std::vector<FramebufferSlot> slots;
    std::vector<ResourceAllocation> resources;
    std::uint32_t physical_slot_count{0};
    std::uint32_t logical_resource_count{0};
    std::uint32_t peak_live_resource_count{0};
    std::uint32_t aliasable_resource_count{0};
    std::uint32_t excluded_persistent_count{0};
    std::uint32_t excluded_async_count{0};

    [[nodiscard]] bool empty() const noexcept {
        return resources.empty() || physical_slot_count == 0;
    }

    [[nodiscard]] const ResourceAllocation* allocation_for(GraphNodeId id) const noexcept {
        if (id >= resources.size() || resources[id].producer != id) {
            return nullptr;
        }
        return &resources[id];
    }
};

} // namespace chronon3d::graph
