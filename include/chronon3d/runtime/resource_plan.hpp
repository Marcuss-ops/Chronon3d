#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace chronon3d::runtime {

enum class ResourceKind : std::uint8_t {
    Color,
    Depth,
    Yuv,
    Bytes,
};

enum class LifetimeClass : std::uint8_t {
    FrameTransient,
    PipelineSlot,
    JobPersistent,
};

struct ResourceRequest {
    std::string id{};
    ResourceKind kind{ResourceKind::Bytes};
    std::size_t bytes{0};
    LifetimeClass lifetime{LifetimeClass::FrameTransient};
    std::size_t first{0};
    std::size_t last{0};
    std::size_t alignment{alignof(std::max_align_t)};
};

struct ResourceAllocation {
    std::size_t request_index{0};
    std::size_t physical_slot{std::numeric_limits<std::size_t>::max()};
};

struct PhysicalResourceSlot {
    ResourceKind kind{ResourceKind::Bytes};
    LifetimeClass lifetime{LifetimeClass::FrameTransient};
    std::size_t bytes{0};
    std::size_t alignment{alignof(std::max_align_t)};
    std::size_t last{0};
};

struct ResourcePlan {
    std::vector<ResourceRequest> requests;
    std::vector<ResourceAllocation> allocations;
    std::vector<PhysicalResourceSlot> slots;
    // Sum of the physical slots that must be backed after liveness aliasing.
    // This is the capacity commitment; peak_live_bytes remains the logical
    // overlap peak useful for comparing graph pressure before reuse.
    std::size_t planned_physical_bytes{0};
    std::size_t peak_live_bytes{0};

    [[nodiscard]] const ResourceAllocation* allocation_for(
        std::size_t request_index) const noexcept {
        return request_index < allocations.size()
            ? &allocations[request_index] : nullptr;
    }
};

/// Deterministic first-fit interval planner for job/pipeline resources.
/// Requests alias only when kind/alignment are compatible and lifetimes do not
/// overlap. The planner allocates no backing memory; pools own that lifecycle.
class ResourcePlanner {
public:
    void add(ResourceRequest request) { m_requests.push_back(request); }

    [[nodiscard]] ResourcePlan build() const {
        ResourcePlan plan;
        plan.requests = m_requests;
        plan.allocations.resize(m_requests.size());

        std::vector<std::size_t> order(m_requests.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        for (std::size_t i = 1; i < order.size(); ++i) {
            const auto value = order[i];
            std::size_t j = i;
            while (j > 0 && less(m_requests[value], m_requests[order[j - 1]])) {
                order[j] = order[j - 1];
                --j;
            }
            order[j] = value;
        }

        std::vector<std::size_t> live_bytes;
        for (const auto index : order) {
            const auto& request = m_requests[index];
            std::size_t selected = std::numeric_limits<std::size_t>::max();
            for (std::size_t slot = 0; slot < plan.slots.size(); ++slot) {
                const auto& physical = plan.slots[slot];
                if (physical.kind == request.kind &&
                    physical.lifetime == request.lifetime &&
                    physical.last < request.first) {
                    selected = slot;
                    break;
                }
            }
            if (selected == std::numeric_limits<std::size_t>::max()) {
                selected = plan.slots.size();
                plan.slots.push_back(PhysicalResourceSlot{
                    request.kind, request.lifetime, request.bytes,
                    request.alignment, request.last});
            } else {
                auto& physical = plan.slots[selected];
                physical.bytes = physical.bytes < request.bytes ? request.bytes : physical.bytes;
                physical.last = request.last;
                physical.alignment = physical.alignment < request.alignment
                    ? request.alignment : physical.alignment;
            }
            plan.allocations[index] = ResourceAllocation{index, selected};
        }

        std::size_t max_point = 0;
        for (const auto& request : m_requests) max_point = max_point < request.last ? request.last : max_point;
        for (std::size_t point = 0; point <= max_point; ++point) {
            std::size_t live = 0;
            for (const auto& request : m_requests) {
                if (request.first <= point && point <= request.last) live += request.bytes;
            }
            if (live > plan.peak_live_bytes) plan.peak_live_bytes = live;
        }
        for (const auto& slot : plan.slots) {
            plan.planned_physical_bytes += slot.bytes;
        }
        return plan;
    }

private:
    static bool less(const ResourceRequest& lhs, const ResourceRequest& rhs) noexcept {
        if (lhs.first != rhs.first) return lhs.first < rhs.first;
        return lhs.id < rhs.id;
    }

    std::vector<ResourceRequest> m_requests;
};

} // namespace chronon3d::runtime
