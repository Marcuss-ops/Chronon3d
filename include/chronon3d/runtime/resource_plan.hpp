#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/resource_residency.hpp>

namespace chronon3d::runtime {

using ResourceId = std::uint32_t;

/// Transitional planner-local descriptor. It is intentionally isolated under
/// a distinct name so ResourceDesc can become the single runtime authority in
/// the next commit without a type-name collision.
struct PlannerResourceDesc {
    std::uint32_t width{0};
    std::uint32_t height{0};
    FrameFormat format{};
    ResourceUsage usage{ResourceUsage::Generic};
    std::size_t bytes{0};
    std::size_t alignment{alignof(std::max_align_t)};
    LifetimeClass lifetime{LifetimeClass::FrameTransient};
    ResourceResidency residency{};

    [[nodiscard]] constexpr std::size_t tight_bytes() const noexcept {
        return tight_surface_bytes(format, width, height);
    }

    [[nodiscard]] static constexpr PlannerResourceDesc make(
        std::uint32_t width,
        std::uint32_t height,
        FrameFormat format,
        ResourceUsage usage = ResourceUsage::Generic,
        LifetimeClass lifetime = LifetimeClass::FrameTransient,
        std::size_t alignment = alignof(std::max_align_t),
        ResourceResidency residency = {}) noexcept {
        return PlannerResourceDesc{
            width, height, format, usage,
            tight_surface_bytes(format, width, height), alignment, lifetime,
            residency};
    }
};

/// Graph-facing logical resource. The planner deliberately keeps this
/// description separate from the physical slot that backs it.
struct LogicalResource {
    ResourceId id{0};
    RenderSurfaceHandle surface{kInvalidRenderSurfaceHandle};
    PlannerResourceDesc desc{};
    std::size_t first_use{0};
    std::size_t last_use{0};
    bool persistent{false};
};

struct ResourceRequest {
    std::string id{};
    ResourceKind kind{ResourceKind::Bytes};
    /// Explicit allocation-size override for padded/external layouts. Zero
    /// means derive tightly from the descriptor; desc.bytes is never authoritative.
    std::size_t bytes{0};
    /// Legacy request-level mirror. ResourcePlanner::add folds this into
    /// desc.lifetime and thereafter the descriptor is authoritative.
    LifetimeClass lifetime{LifetimeClass::FrameTransient};
    std::size_t first{0};
    std::size_t last{0};
    std::size_t alignment{alignof(std::max_align_t)};
    PlannerResourceDesc desc{};
    RenderSurfaceHandle surface{kInvalidRenderSurfaceHandle};
};

struct ResourceAllocation {
    std::size_t request_index{0};
    std::size_t physical_slot{std::numeric_limits<std::size_t>::max()};
    RenderSurfaceHandle surface{kInvalidRenderSurfaceHandle};
};

using ResourceBinding = ResourceAllocation;

struct PhysicalResourceSlot {
    ResourceKind kind{ResourceKind::Bytes};
    LifetimeClass lifetime{LifetimeClass::FrameTransient};
    std::size_t bytes{0};
    std::size_t alignment{alignof(std::max_align_t)};
    std::size_t last{0};
    FrameFormat format{};
    ResourceUsage usage{ResourceUsage::Generic};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::size_t offset{0};
    ResourceResidency residency{};
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
        if (request.desc.lifetime == LifetimeClass::FrameTransient &&
            request.lifetime != LifetimeClass::FrameTransient) {
            request.desc.lifetime = request.lifetime;
        }
        request.lifetime = request.desc.lifetime;

        if (request.kind == ResourceKind::Color &&
            is_rgb_surface_format(request.desc.format)) {
            request.desc.format = canonical_render_format();
        }

        request.desc.bytes = request.desc.tight_bytes();
        request.desc.alignment = request.alignment != 0
            ? request.alignment : request.desc.alignment;
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
                        physical.last < request.first) {
                        selected = slot;
                        break;
                    }
                }
            }
            if (selected == std::numeric_limits<std::size_t>::max()) {
                selected = plan.slots.size();
                plan.slots.push_back(PhysicalResourceSlot{
                    request.kind, request.desc.lifetime, request_bytes,
                    request.desc.alignment, request.last,
                    request.desc.format, request.desc.usage,
                    request.desc.width, request.desc.height, 0,
                    request.desc.residency,
                    request.desc.residency.requires_dedicated_allocation()});
                ++plan.telemetry.buffer_new_allocations;
            } else {
                auto& physical = plan.slots[selected];
                physical.bytes = physical.bytes < request_bytes ? request_bytes : physical.bytes;
                physical.last = request.last;
                physical.alignment = physical.alignment < request.desc.alignment
                    ? request.desc.alignment : physical.alignment;
                physical.width = std::max(physical.width, request.desc.width);
                physical.height = std::max(physical.height, request.desc.height);
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
                plan.planned_physical_bytes, slot.alignment);
            plan.planned_physical_bytes += slot.bytes;
        }
        std::size_t offset = 0;
        for (auto& slot : plan.slots) {
            offset = align_up(offset, slot.alignment);
            slot.offset = offset;
            offset += slot.bytes;
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
        return request.bytes != 0 ? request.bytes : request.desc.tight_bytes();
    }

    static bool less(const ResourceRequest& lhs, const ResourceRequest& rhs) noexcept {
        if (lhs.first != rhs.first) return lhs.first < rhs.first;
        return lhs.id < rhs.id;
    }

    static bool compatible(const PhysicalResourceSlot& slot,
                           const ResourceRequest& request) noexcept {
        const auto& desc = request.desc;
        const bool format_compatible =
            desc.format.pixel == PixelFormat::Unknown ||
            slot.format.pixel == PixelFormat::Unknown || slot.format == desc.format;
        const bool usage_compatible =
            desc.usage == ResourceUsage::Generic ||
            slot.usage == ResourceUsage::Generic || slot.usage == desc.usage;
        const bool alignment_compatible = desc.alignment != 0;
        const bool dimensions_compatible =
            desc.width == 0 || desc.height == 0 || slot.width == 0 ||
            slot.height == 0 ||
            (slot.width == desc.width && slot.height == desc.height);
        const bool residency_compatible = slot.residency == desc.residency;
        return slot.kind == request.kind &&
               slot.lifetime == desc.lifetime &&
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
