#pragma once

#include <chronon3d/internal/render_graph/render_graph.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace chronon3d::graph {

inline constexpr std::uint32_t kInvalidPhysicalResourceSlot =
    std::numeric_limits<std::uint32_t>::max();

enum class ResourceKind : std::uint8_t {
    Surface,
    Media,
};

enum class ResourceFormat : std::uint8_t {
    Unknown,
    RgbaLinear,
    Nv12,
    P010,
};

enum class ResourceSubresource : std::uint8_t {
    Whole,
    Plane0,
    Plane1,
};

struct ResourceSubresourceDesc {
    ResourceSubresource subresource{ResourceSubresource::Whole};
    std::uint32_t plane_index{0};
};

struct ResourceDesc {
    ResourceKind kind{ResourceKind::Surface};
    ResourceFormat format{ResourceFormat::RgbaLinear};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::vector<ResourceSubresourceDesc> subresources{
        ResourceSubresourceDesc{ResourceSubresource::Whole, 0}
    };

    [[nodiscard]] static ResourceDesc surface(
        std::uint32_t width,
        std::uint32_t height) {
        ResourceDesc desc;
        desc.kind = ResourceKind::Surface;
        desc.format = ResourceFormat::RgbaLinear;
        desc.width = width;
        desc.height = height;
        return desc;
    }

    [[nodiscard]] static ResourceDesc media(
        ResourceFormat format,
        std::uint32_t width,
        std::uint32_t height) {
        ResourceDesc desc;
        desc.kind = ResourceKind::Media;
        desc.format = format;
        desc.width = width;
        desc.height = height;
        desc.subresources = {
            ResourceSubresourceDesc{ResourceSubresource::Plane0, 0},
            ResourceSubresourceDesc{ResourceSubresource::Plane1, 1},
        };
        return desc;
    }
};

struct PhysicalRequirements {
    std::size_t size_bytes{0};
    std::size_t alignment_bytes{1};
    bool persistent{false};
    bool async_use{false};
    bool aliasable{false};
};

struct CompiledResourceTransition {
    GraphNodeId consumer{k_invalid_node};
    ResourceSubresource subresource{ResourceSubresource::Whole};
    bool ownership_transfer{false};
};

struct CompiledResourcePlan {
    GraphNodeId producer{k_invalid_node};
    ResourceDesc desc{};
    PhysicalRequirements physical{};
    std::size_t first_level{0};
    std::size_t last_level{0};
    std::size_t consumer_count{0};
    std::size_t release_after_level{0};
    bool release_scheduled{false};
    bool can_release_after_last_consumer{true};
    GraphNodeId ownership_transfer_consumer{k_invalid_node};
    std::uint32_t physical_slot{kInvalidPhysicalResourceSlot};
    std::vector<CompiledResourceTransition> transitions;

    [[nodiscard]] bool has_ownership_transfer() const noexcept {
        return ownership_transfer_consumer != k_invalid_node;
    }
};

struct PhysicalResourceSlot {
    std::uint32_t id{kInvalidPhysicalResourceSlot};
    std::size_t size_bytes{0};
    std::size_t alignment_bytes{1};
};

struct CompiledResourceTable {
    std::vector<CompiledResourcePlan> resources;
    std::vector<PhysicalResourceSlot> slots;
    std::uint32_t logical_resource_count{0};
    std::uint32_t aliasable_resource_count{0};
    std::uint32_t physical_slot_count{0};
    std::uint32_t peak_live_resource_count{0};
    std::uint32_t excluded_persistent_count{0};
    std::uint32_t excluded_async_count{0};

    [[nodiscard]] const CompiledResourcePlan* plan_for(GraphNodeId id) const noexcept {
        if (id >= resources.size()) {
            return nullptr;
        }
        const auto& plan = resources[id];
        return plan.producer == k_invalid_node ? nullptr : &plan;
    }

    [[nodiscard]] CompiledResourcePlan* plan_for(GraphNodeId id) noexcept {
        if (id >= resources.size()) {
            return nullptr;
        }
        auto& plan = resources[id];
        return plan.producer == k_invalid_node ? nullptr : &plan;
    }

    [[nodiscard]] std::vector<std::size_t> consumer_counts() const {
        std::vector<std::size_t> counts(resources.size(), 0);
        for (std::size_t i = 0; i < resources.size(); ++i) {
            counts[i] = resources[i].consumer_count;
        }
        return counts;
    }
};

} // namespace chronon3d::graph
