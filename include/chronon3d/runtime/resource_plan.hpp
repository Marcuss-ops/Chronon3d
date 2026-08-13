#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace chronon3d::runtime {

using ResourceId = std::uint32_t;

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

/// Compatibility domain for a planned allocation. External resources are
/// described for dependency analysis but never receive an arena slot.
enum class ResourceLifetime : std::uint8_t {
    Transient,
    Persistent,
    External,
};

enum class ResourceUsage : std::uint8_t {
    Generic,
    ColorAttachment,
    DepthAttachment,
    Storage,
};

enum class PixelFormat : std::uint8_t {
    Unknown,
    Rgba32Float,
    Rgba8Unorm,
    Depth32Float,
    Bytes,
};

struct ResourceDesc {
    std::uint32_t width{0};
    std::uint32_t height{0};
    PixelFormat format{PixelFormat::Unknown};
    ResourceUsage usage{ResourceUsage::Generic};
    std::size_t bytes{0};
    std::size_t alignment{alignof(std::max_align_t)};
    ResourceLifetime lifetime{ResourceLifetime::Transient};
};

/// Graph-facing logical resource.  The planner deliberately keeps this
/// description separate from the physical slot that backs it.
struct LogicalResource {
    ResourceId id{0};
    ResourceDesc desc{};
    std::size_t first_use{0};
    std::size_t last_use{0};
    bool persistent{false};
};

struct ResourceRequest {
    std::string id{};
    ResourceKind kind{ResourceKind::Bytes};
    std::size_t bytes{0};
    LifetimeClass lifetime{LifetimeClass::FrameTransient};
    std::size_t first{0};
    std::size_t last{0};
    std::size_t alignment{alignof(std::max_align_t)};
    ResourceDesc desc{};
};

struct ResourceAllocation {
    std::size_t request_index{0};
    std::size_t physical_slot{std::numeric_limits<std::size_t>::max()};
};

using ResourceBinding = ResourceAllocation;

struct PhysicalResourceSlot {
    ResourceKind kind{ResourceKind::Bytes};
    LifetimeClass lifetime{LifetimeClass::FrameTransient};
    std::size_t bytes{0};
    std::size_t alignment{alignof(std::max_align_t)};
    std::size_t last{0};
    PixelFormat format{PixelFormat::Unknown};
    ResourceUsage usage{ResourceUsage::Generic};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::size_t offset{0};
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
    // Sum of the physical slots that must be backed after liveness aliasing.
    // This is the capacity commitment; peak_live_bytes remains the logical
    // overlap peak useful for comparing graph pressure before reuse.
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

        for (const auto index : order) {
            const auto& request = m_requests[index];
            const auto request_bytes = request.bytes != 0
                ? request.bytes : request.desc.bytes;
            if (request.desc.lifetime == ResourceLifetime::External) {
                plan.allocations[index] = ResourceAllocation{
                    index, std::numeric_limits<std::size_t>::max()};
                continue;
            }
            std::size_t selected = std::numeric_limits<std::size_t>::max();
            for (std::size_t slot = 0; slot < plan.slots.size(); ++slot) {
                const auto& physical = plan.slots[slot];
                if (request.desc.lifetime == ResourceLifetime::Transient &&
                    compatible(physical, request) &&
                    physical.last < request.first) {
                    selected = slot;
                    break;
                }
            }
            if (selected == std::numeric_limits<std::size_t>::max()) {
                selected = plan.slots.size();
                plan.slots.push_back(PhysicalResourceSlot{
                    request.kind, request.lifetime, request_bytes,
                    request.alignment, request.last,
                    request.desc.format, request.desc.usage,
                    request.desc.width, request.desc.height, 0});
                ++plan.telemetry.buffer_new_allocations;
            } else {
                auto& physical = plan.slots[selected];
                physical.bytes = physical.bytes < request_bytes ? request_bytes : physical.bytes;
                physical.last = request.last;
                physical.alignment = physical.alignment < request.alignment
                    ? request.alignment : physical.alignment;
                physical.width = std::max(physical.width, request.desc.width);
                physical.height = std::max(physical.height, request.desc.height);
                ++plan.telemetry.buffer_reuse_count;
            }
            plan.allocations[index] = ResourceAllocation{index, selected};
        }

        std::size_t max_point = 0;
        for (const auto& request : m_requests) max_point = max_point < request.last ? request.last : max_point;
        for (std::size_t point = 0; point <= max_point; ++point) {
            std::size_t live = 0;
            for (const auto& request : m_requests) {
                const auto request_bytes = request.bytes != 0
                    ? request.bytes : request.desc.bytes;
                if (request.first <= point && point <= request.last) live += request_bytes;
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
            plan.telemetry.logical_bytes += request.bytes != 0
                ? request.bytes : request.desc.bytes;
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
    static bool less(const ResourceRequest& lhs, const ResourceRequest& rhs) noexcept {
        if (lhs.first != rhs.first) return lhs.first < rhs.first;
        return lhs.id < rhs.id;
    }

    static bool compatible(const PhysicalResourceSlot& slot,
                           const ResourceRequest& request) noexcept {
        const auto& desc = request.desc;
        const bool format_compatible =
            desc.format == PixelFormat::Unknown ||
            slot.format == PixelFormat::Unknown || slot.format == desc.format;
        const bool usage_compatible =
            desc.usage == ResourceUsage::Generic ||
            slot.usage == ResourceUsage::Generic || slot.usage == desc.usage;
        // A slot may grow in bytes/alignment, but dimensions are part of the
        // row-pitch contract.  Do not alias distinct concrete extents; a
        // zero extent is the legacy/wildcard form used by byte resources.
        // A slot can be promoted to the stricter alignment when it is
        // reused; the final arena offset is recomputed after all bindings.
        // This is safe for ordinary power-of-two alignments and preserves
        // the planner's first-fit behavior.
        const bool alignment_compatible = desc.alignment != 0;
        const bool dimensions_compatible =
            desc.width == 0 || desc.height == 0 || slot.width == 0 ||
            slot.height == 0 ||
            (slot.width == desc.width && slot.height == desc.height);
        return slot.kind == request.kind &&
               slot.lifetime == request.lifetime &&
               format_compatible && usage_compatible && alignment_compatible &&
               dimensions_compatible;
    }

    static std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
        if (alignment <= 1) return value;
        const auto remainder = value % alignment;
        return remainder == 0 ? value : value + (alignment - remainder);
    }

    std::vector<ResourceRequest> m_requests;
};

/// Canonical graph terminology; kept as an alias so existing preparation
/// code and the new compiled-graph planner share one implementation.
using ResourceLifetimePlanner = ResourcePlanner;

} // namespace chronon3d::runtime
