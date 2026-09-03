#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/resource_desc.hpp>

namespace chronon3d::runtime {

using ResourceId = std::uint32_t;

/// Graph-facing logical resource. The planner deliberately keeps this
/// description separate from the physical slot that backs it.
struct LogicalResource {
    ResourceId id{0};
    RenderSurfaceHandle surface{kInvalidRenderSurfaceHandle};
    ResourceDesc desc{};
    std::size_t first_use{0};
    std::size_t last_use{0};
    bool persistent{false};
};

/// Logical allocation request. ResourceDesc is the sole authority for kind,
/// allocation size, lifetime, alignment, format, usage and residency.
struct ResourceRequest {
    std::string id{};
    ResourceDesc desc{};
    std::size_t first{0};
    std::size_t last{0};
    RenderSurfaceHandle surface{kInvalidRenderSurfaceHandle};

    ResourceRequest() = default;

    ResourceRequest(
        std::string id_value,
        ResourceDesc desc_value,
        std::size_t first_value,
        std::size_t last_value,
        RenderSurfaceHandle surface_value = kInvalidRenderSurfaceHandle)
        : id(std::move(id_value)),
          desc(std::move(desc_value)),
          first(first_value),
          last(last_value),
          surface(surface_value) {}

    /// Source-compatibility constructor for staged migration of positional
    /// callers. The legacy values are immediately folded into ResourceDesc;
    /// no mirrored request-level state is retained.
    ResourceRequest(
        std::string id_value,
        ResourceKind kind_value,
        std::size_t bytes_value,
        LifetimeClass lifetime_value,
        std::size_t first_value,
        std::size_t last_value,
        std::size_t alignment_value,
        ResourceDesc desc_value,
        RenderSurfaceHandle surface_value = kInvalidRenderSurfaceHandle)
        : id(std::move(id_value)),
          desc(std::move(desc_value)),
          first(first_value),
          last(last_value),
          surface(surface_value) {
        desc.kind = kind_value;
        desc.lifetime = lifetime_value;
        if (bytes_value != 0) desc.bytes = bytes_value;
        if (alignment_value != 0) desc.alignment = alignment_value;
    }
};

struct ResourceAllocation {
    std::size_t request_index{0};
    std::size_t physical_slot{std::numeric_limits<std::size_t>::max()};
    RenderSurfaceHandle surface{kInvalidRenderSurfaceHandle};
};

using ResourceBinding = ResourceAllocation;

/// Physical placement metadata. ResourceDesc describes what the resource is;
/// this type only records where/how much storage was reserved for it.
struct PhysicalResourceSlot {
    ResourceDesc desc{};
    std::size_t capacity_bytes{0};
    std::size_t offset{0};
    std::size_t last_use{0};
    bool dedicated{false};
};

using PhysicalSlot = PhysicalResourceSlot;

struct ResourcePlanTelemetry {
    std::size_t logical_count{0};
    std::size_t physical_count{0};
    std::size_t logical_bytes{0};
    std::size_t physical_bytes{0};
    std::size_t alias_saved_bytes{0};
    std::size_t buffer_reuse_count{0};
    std::size_t buffer_new_allocations{0};
    std::size_t arena_peak_bytes{0};
};

struct ResourcePlan {
    std::vector<ResourceRequest> requests;
    std::vector<ResourceAllocation> allocations;
    std::vector<PhysicalResourceSlot> slots;
    std::size_t planned_physical_bytes{0};
    std::size_t peak_live_bytes{0};
    ResourcePlanTelemetry telemetry{};

    [[nodiscard]] const ResourceAllocation* allocation_for(
        std::size_t request_index) const noexcept {
        return request_index < allocations.size()
            ? &allocations[request_index] : nullptr;
    }

    [[nodiscard]] std::size_t total_bytes() const noexcept {
        return planned_physical_bytes;
    }
};

/// Deterministic first-fit interval planner for job/pipeline resources.
class ResourcePlanner {
public:
    void add(ResourceRequest request) {
        if (request.desc.kind == ResourceKind::Color &&
            is_rgb_surface_format(request.desc.format)) {
            request.desc.format = canonical_render_format();
        }
        m_requests.push_back(std::move(request));
    }

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

        for (const auto index : order) {
            const auto& request = m_requests[index];
            const auto request_bytes = allocation_bytes(request);
            if (request.desc.lifetime == LifetimeClass::External) {
                plan.allocations[index] = ResourceAllocation{
                    index, std::numeric_limits<std::size_t>::max(), request.surface};
                continue;
            }
            std::size_t selected = std::numeric_limits<std::size_t>::max();
            if (request.desc.residency.allows_transient_aliasing()) {
                for (std::size_t slot = 0; slot < plan.slots.size(); ++slot) {
                    const auto& physical = plan.slots[slot];
                    if (!physical.dedicated &&
                        request.desc.lifetime == LifetimeClass::FrameTransient &&
                        compatible(physical, request) &&
                        physical.last_use < request.first) {
                        selected = slot;
                        break;
                    }
                }
            }
            if (selected == std::numeric_limits<std::size_t>::max()) {
                selected = plan.slots.size();
                plan.slots.push_back(PhysicalResourceSlot{
                    request.desc,
                    request_bytes,
                    0,
                    request.last,
                    request.desc.residency.requires_dedicated_allocation()});
                ++plan.telemetry.buffer_new_allocations;
            } else {
                auto& physical = plan.slots[selected];
                physical.capacity_bytes = physical.capacity_bytes < request_bytes
                    ? request_bytes : physical.capacity_bytes;
                physical.last_use = request.last;
                physical.desc.alignment = physical.desc.alignment < request.desc.alignment
                    ? request.desc.alignment : physical.desc.alignment;
                physical.desc.width = std::max(physical.desc.width, request.desc.width);
                physical.desc.height = std::max(physical.desc.height, request.desc.height);
                ++plan.telemetry.buffer_reuse_count;
            }
            plan.allocations[index] = ResourceAllocation{index, selected, request.surface};
        }

        std::size_t max_point = 0;
        for (const auto& request : m_requests) {
            max_point = max_point < request.last ? request.last : max_point;
        }
        for (std::size_t point = 0; point <= max_point; ++point) {
            std::size_t live = 0;
            for (const auto& request : m_requests) {
                if (request.first <= point && point <= request.last) {
                    live += allocation_bytes(request);
                }
            }
            if (live > plan.peak_live_bytes) plan.peak_live_bytes = live;
        }
        for (const auto& slot : plan.slots) {
            plan.planned_physical_bytes = align_up(
                plan.planned_physical_bytes, slot.desc.alignment);
            plan.planned_physical_bytes += slot.capacity_bytes;
        }
        std::size_t offset = 0;
        for (auto& slot : plan.slots) {
            offset = align_up(offset, slot.desc.alignment);
            slot.offset = offset;
            offset += slot.capacity_bytes;
        }
        plan.telemetry.logical_count = plan.requests.size();
        plan.telemetry.physical_count = plan.slots.size();
        for (const auto& request : plan.requests) {
            plan.telemetry.logical_bytes += allocation_bytes(request);
        }
        plan.telemetry.physical_bytes = plan.planned_physical_bytes;
        plan.telemetry.arena_peak_bytes = plan.planned_physical_bytes;
        plan.telemetry.alias_saved_bytes =
            plan.telemetry.logical_bytes > plan.telemetry.physical_bytes
                ? plan.telemetry.logical_bytes - plan.telemetry.physical_bytes
                : 0;
        return plan;
    }

private:
    [[nodiscard]] static std::size_t allocation_bytes(
        const ResourceRequest& request) noexcept {
        return request.desc.allocation_bytes();
    }

    static bool less(const ResourceRequest& lhs, const ResourceRequest& rhs) noexcept {
        if (lhs.first != rhs.first) return lhs.first < rhs.first;
        return lhs.id < rhs.id;
    }

    static bool compatible(const PhysicalResourceSlot& slot,
                           const ResourceRequest& request) noexcept {
        const auto& slot_desc = slot.desc;
        const auto& desc = request.desc;
        const bool format_compatible =
            desc.format.pixel == PixelFormat::Unknown ||
            slot_desc.format.pixel == PixelFormat::Unknown ||
            slot_desc.format == desc.format;
        const bool usage_compatible =
            desc.usage == ResourceUsage::Generic ||
            slot_desc.usage == ResourceUsage::Generic ||
            slot_desc.usage == desc.usage;
        const bool alignment_compatible = desc.alignment != 0;
        const bool dimensions_compatible =
            desc.width == 0 || desc.height == 0 || slot_desc.width == 0 ||
            slot_desc.height == 0 ||
            (slot_desc.width == desc.width && slot_desc.height == desc.height);
        const bool residency_compatible = slot_desc.residency == desc.residency;
        return slot_desc.kind == desc.kind &&
               slot_desc.lifetime == desc.lifetime &&
               format_compatible && usage_compatible && alignment_compatible &&
               dimensions_compatible && residency_compatible;
    }

    static std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
        if (alignment <= 1) return value;
        const auto remainder = value % alignment;
        return remainder == 0 ? value : value + (alignment - remainder);
    }

    std::vector<ResourceRequest> m_requests;
};

using ResourceLifetimePlanner = ResourcePlanner;

} // namespace chronon3d::runtime
