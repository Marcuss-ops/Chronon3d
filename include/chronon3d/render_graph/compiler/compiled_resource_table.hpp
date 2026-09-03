#pragma once

#include <chronon3d/cache/cache_taxonomy.hpp>
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/runtime/resource_plan.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace chronon3d::graph {

static_assert(
    cache::cache_family_annotation<cache::CacheFamily::ResidencyCache>);

using PhysicalAllocationId = std::uint32_t;
inline constexpr PhysicalAllocationId kInvalidPhysicalAllocationId =
    std::numeric_limits<PhysicalAllocationId>::max();

// Transitional constant spelling retained until the compiled-operation ABI is
// renamed. It is a scalar sentinel only; no framebuffer allocation plan exists.
inline constexpr PhysicalAllocationId kInvalidPhysicalFramebufferSlot =
    kInvalidPhysicalAllocationId;

enum class ResourceSubresource : std::uint8_t {
    Whole,
    Plane0,
    Plane1,
};

/// Physical requirements lowered once from runtime::ResourceDesc. Backends
/// consume this value instead of rebuilding size/alignment/plane semantics.
struct PhysicalRequirements {
    std::size_t allocation_bytes{0};
    std::size_t alignment{alignof(std::max_align_t)};
    std::uint32_t plane_count{1};
    bool gpu_compatible{true};
    bool aliasable{false};
};

struct CompiledResourceSubresource {
    ResourceSubresource id{ResourceSubresource::Whole};
    std::uint32_t plane_index{0};
};

/// Synchronization/ownership edge carried by the resource that owns it. This
/// replaces graph-global ownership-transfer side tables and also provides the
/// common primitive used by media plane consumers.
struct CompiledResourceTransition {
    GraphNodeId consumer{k_invalid_node};
    ResourceSubresource subresource{ResourceSubresource::Whole};
    bool ownership_transfer{false};
};

/// Canonical compiled plan for one logical graph resource.
/// ResourceDesc -> PhysicalRequirements -> CompiledResourcePlan is the only
/// persisted lowering path for lifetime, release, subresource and allocation
/// metadata.
struct CompiledResourcePlan {
    GraphNodeId producer{k_invalid_node};
    runtime::ResourceDesc desc{};
    PhysicalRequirements physical{};

    std::size_t first_level{0};
    std::size_t last_level{0};
    std::size_t consumer_count{0};
    std::size_t release_after_level{0};
    bool release_scheduled{false};
    bool can_release_after_last_consumer{true};

    PhysicalAllocationId physical_slot{kInvalidPhysicalAllocationId};
    bool persistent{false};
    bool async_use{false};

    std::vector<CompiledResourceSubresource> subresources;
    std::vector<CompiledResourceTransition> transitions;

    [[nodiscard]] bool aliasable() const noexcept {
        return physical.aliasable;
    }

    [[nodiscard]] std::optional<GraphNodeId>
    ownership_transfer_consumer() const noexcept {
        for (const auto& transition : transitions) {
            if (transition.ownership_transfer) {
                return transition.consumer;
            }
        }
        return std::nullopt;
    }
};

// Temporary source spelling while downstream tests migrate to the plan name.
// It aliases the same object and carries no separate storage.
using CompiledResourceRecord = CompiledResourcePlan;
using ResourceLifetime = CompiledResourcePlan;

struct CompiledResourceTable {
    std::vector<CompiledResourcePlan> resources;
    std::vector<runtime::PhysicalResourceSlot> slots;

    std::uint32_t physical_slot_count{0};
    std::uint32_t logical_resource_count{0};
    std::uint32_t peak_live_resource_count{0};
    std::uint32_t aliasable_resource_count{0};
    std::uint32_t excluded_persistent_count{0};
    std::uint32_t excluded_async_count{0};

    std::size_t logical_bytes{0};
    std::size_t planned_physical_bytes{0};
    std::size_t peak_live_bytes{0};

    // Transitional zero-storage views retained only until every caller has
    // moved to resource_for(). They do not own a parallel authority.
    std::vector<CompiledResourcePlan>& lifetimes;
    CompiledResourceTable& physical_framebuffer_plan;

    CompiledResourceTable() noexcept
        : lifetimes(resources),
          physical_framebuffer_plan(*this) {}

    CompiledResourceTable(const CompiledResourceTable& other)
        : resources(other.resources),
          slots(other.slots),
          physical_slot_count(other.physical_slot_count),
          logical_resource_count(other.logical_resource_count),
          peak_live_resource_count(other.peak_live_resource_count),
          aliasable_resource_count(other.aliasable_resource_count),
          excluded_persistent_count(other.excluded_persistent_count),
          excluded_async_count(other.excluded_async_count),
          logical_bytes(other.logical_bytes),
          planned_physical_bytes(other.planned_physical_bytes),
          peak_live_bytes(other.peak_live_bytes),
          lifetimes(resources),
          physical_framebuffer_plan(*this) {}

    CompiledResourceTable(CompiledResourceTable&& other) noexcept
        : resources(std::move(other.resources)),
          slots(std::move(other.slots)),
          physical_slot_count(other.physical_slot_count),
          logical_resource_count(other.logical_resource_count),
          peak_live_resource_count(other.peak_live_resource_count),
          aliasable_resource_count(other.aliasable_resource_count),
          excluded_persistent_count(other.excluded_persistent_count),
          excluded_async_count(other.excluded_async_count),
          logical_bytes(other.logical_bytes),
          planned_physical_bytes(other.planned_physical_bytes),
          peak_live_bytes(other.peak_live_bytes),
          lifetimes(resources),
          physical_framebuffer_plan(*this) {}

    CompiledResourceTable& operator=(const CompiledResourceTable& other) {
        if (this == &other) return *this;
        resources = other.resources;
        slots = other.slots;
        physical_slot_count = other.physical_slot_count;
        logical_resource_count = other.logical_resource_count;
        peak_live_resource_count = other.peak_live_resource_count;
        aliasable_resource_count = other.aliasable_resource_count;
        excluded_persistent_count = other.excluded_persistent_count;
        excluded_async_count = other.excluded_async_count;
        logical_bytes = other.logical_bytes;
        planned_physical_bytes = other.planned_physical_bytes;
        peak_live_bytes = other.peak_live_bytes;
        return *this;
    }

    CompiledResourceTable& operator=(CompiledResourceTable&& other) noexcept {
        if (this == &other) return *this;
        resources = std::move(other.resources);
        slots = std::move(other.slots);
        physical_slot_count = other.physical_slot_count;
        logical_resource_count = other.logical_resource_count;
        peak_live_resource_count = other.peak_live_resource_count;
        aliasable_resource_count = other.aliasable_resource_count;
        excluded_persistent_count = other.excluded_persistent_count;
        excluded_async_count = other.excluded_async_count;
        logical_bytes = other.logical_bytes;
        planned_physical_bytes = other.planned_physical_bytes;
        peak_live_bytes = other.peak_live_bytes;
        return *this;
    }

    void clear() {
        resources.clear();
        slots.clear();
        physical_slot_count = 0;
        logical_resource_count = 0;
        peak_live_resource_count = 0;
        aliasable_resource_count = 0;
        excluded_persistent_count = 0;
        excluded_async_count = 0;
        logical_bytes = 0;
        planned_physical_bytes = 0;
        peak_live_bytes = 0;
    }

    [[nodiscard]] bool empty() const noexcept {
        return resources.empty();
    }

    [[nodiscard]] const CompiledResourcePlan* resource_for(
        GraphNodeId id) const noexcept {
        if (id >= resources.size() || resources[id].producer != id) {
            return nullptr;
        }
        return &resources[id];
    }

    [[nodiscard]] CompiledResourcePlan* resource_for(
        GraphNodeId id) noexcept {
        if (id >= resources.size() || resources[id].producer != id) {
            return nullptr;
        }
        return &resources[id];
    }

    [[nodiscard]] const CompiledResourcePlan* allocation_for(
        GraphNodeId id) const noexcept {
        return resource_for(id);
    }

    [[nodiscard]] std::vector<GraphNodeId> release_schedule(
        std::size_t level) const {
        std::vector<GraphNodeId> result;
        for (const auto& resource : resources) {
            if (resource.producer != k_invalid_node &&
                resource.release_scheduled &&
                resource.release_after_level == level) {
                result.push_back(resource.producer);
            }
        }
        return result;
    }
};

[[nodiscard]] inline PhysicalRequirements lower_physical_requirements(
    const runtime::ResourceDesc& desc,
    bool aliasable) noexcept {
    PhysicalRequirements result;
    result.allocation_bytes = desc.allocation_bytes();
    result.alignment = desc.alignment;
    result.plane_count =
        (desc.format.pixel == runtime::PixelFormat::Nv12 ||
         desc.format.pixel == runtime::PixelFormat::P010)
            ? 2u
            : 1u;
    result.gpu_compatible = desc.kind != runtime::ResourceKind::Bytes;
    result.aliasable = aliasable;
    return result;
}

inline void lower_subresources(CompiledResourcePlan& plan) {
    plan.subresources.clear();
    if (plan.physical.plane_count == 2u) {
        plan.subresources.push_back(
            CompiledResourceSubresource{ResourceSubresource::Plane0, 0});
        plan.subresources.push_back(
            CompiledResourceSubresource{ResourceSubresource::Plane1, 1});
    } else {
        plan.subresources.push_back(
            CompiledResourceSubresource{ResourceSubresource::Whole, 0});
    }
}

} // namespace chronon3d::graph
