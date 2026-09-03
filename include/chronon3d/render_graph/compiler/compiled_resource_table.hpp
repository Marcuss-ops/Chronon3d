#pragma once

#include <chronon3d/cache/cache_taxonomy.hpp>
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/runtime/resource_plan.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace chronon3d::graph {

// The compiled resource table is the ResidencyCache compiler boundary.
// ResourceDesc owns logical requirements; runtime::PhysicalResourceSlot owns
// physical placement metadata. No backend-specific framebuffer allocation
// descriptor is persisted beside them.
static_assert(
    cache::cache_family_annotation<cache::CacheFamily::ResidencyCache>);

using PhysicalAllocationId = std::uint32_t;
inline constexpr PhysicalAllocationId kInvalidPhysicalAllocationId =
    std::numeric_limits<PhysicalAllocationId>::max();

// Temporary source-compatibility spelling for compiled operation payloads.
// This is a constant alias only; the deleted PhysicalFramebufferAllocationPlan
// type and its independent storage/interval-coloring authority are gone.
inline constexpr PhysicalAllocationId kInvalidPhysicalFramebufferSlot =
    kInvalidPhysicalAllocationId;

/// Canonical compiled record for one logical graph resource.
///
/// The descriptor, lifetime interval, release policy and physical allocation
/// id live together so execution never has to reconcile parallel authorities.
struct CompiledResourceRecord {
    GraphNodeId producer{k_invalid_node};
    runtime::ResourceDesc desc{};

    std::size_t first_level{0};
    std::size_t last_level{0};
    std::size_t consumer_count{0};
    bool can_release_after_last_consumer{true};

    PhysicalAllocationId physical_slot{kInvalidPhysicalAllocationId};
    bool aliasable{false};
    bool persistent{false};
    bool async_use{false};
};

// Source-compatible type spelling for callers that reason about the lifetime
// portion of a compiled resource. It names the canonical record itself and
// therefore does not create a second representation.
using ResourceLifetime = CompiledResourceRecord;

/// Sole persisted resource/lifetime/allocation authority of CompiledFrameGraph.
///
/// `resources` contains every reachable logical graph output. `slots` is the
/// canonical physical placement produced by runtime::ResourcePlanner for the
/// transient aliasable subset. Persistent/async resources remain in the table
/// with an invalid physical id and retain normal shared/pool ownership.
struct CompiledResourceTable {
    std::vector<CompiledResourceRecord> resources;
    std::vector<runtime::PhysicalResourceSlot> slots;

    // Derived release index owned by the same table. This is an execution
    // acceleration index over `resources`, not an independently-computed
    // lifetime authority.
    std::vector<std::vector<GraphNodeId>> release_after_level;

    std::uint32_t physical_slot_count{0};
    std::uint32_t logical_resource_count{0};
    std::uint32_t peak_live_resource_count{0};
    std::uint32_t aliasable_resource_count{0};
    std::uint32_t excluded_persistent_count{0};
    std::uint32_t excluded_async_count{0};

    // Canonical byte totals produced by the compiler/planner. Execution and
    // backends consume these directly instead of reconstructing allocation
    // requirements from frame dimensions or framebuffer implementation details.
    std::size_t logical_bytes{0};
    std::size_t planned_physical_bytes{0};
    std::size_t peak_live_bytes{0};

    // Zero-storage compatibility views. They deliberately alias canonical
    // table storage so old call sites cannot become a second authority.
    std::vector<CompiledResourceRecord>& lifetimes;
    CompiledResourceTable& physical_framebuffer_plan;

    CompiledResourceTable() noexcept
        : lifetimes(resources),
          physical_framebuffer_plan(*this) {}

    CompiledResourceTable(const CompiledResourceTable& other)
        : resources(other.resources),
          slots(other.slots),
          release_after_level(other.release_after_level),
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
          release_after_level(std::move(other.release_after_level)),
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
        release_after_level = other.release_after_level;
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
        release_after_level = std::move(other.release_after_level);
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
        release_after_level.clear();
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

    [[nodiscard]] const CompiledResourceRecord* resource_for(
        GraphNodeId id) const noexcept {
        if (id >= resources.size() || resources[id].producer != id) {
            return nullptr;
        }
        return &resources[id];
    }

    [[nodiscard]] CompiledResourceRecord* resource_for(
        GraphNodeId id) noexcept {
        if (id >= resources.size() || resources[id].producer != id) {
            return nullptr;
        }
        return &resources[id];
    }

    // Compatibility spelling used by existing compiled-program/executor code.
    // It returns the canonical record, not a mirrored allocation object.
    [[nodiscard]] const CompiledResourceRecord* allocation_for(
        GraphNodeId id) const noexcept {
        return resource_for(id);
    }

    [[nodiscard]] const std::vector<GraphNodeId>& release_schedule(
        std::size_t level) const noexcept {
        static const std::vector<GraphNodeId> kEmpty;
        return level < release_after_level.size()
            ? release_after_level[level]
            : kEmpty;
    }
};

} // namespace chronon3d::graph
