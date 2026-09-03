#pragma once

#include <chronon3d/render_graph/compiler/compiled_resource_table.hpp>
#include <chronon3d/runtime/frame_format.hpp>

#include <cstdint>
#include <optional>

namespace chronon3d::media {

/// Media color semantics are the canonical runtime FrameFormat; RenderToMedia
/// never carries a parallel color-description object.
using ColorDescription = runtime::FrameFormat;

struct MediaSurfaceDesc {
    std::uint32_t width{0};
    std::uint32_t height{0};
    ColorDescription format{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return width != 0 && height != 0 && format.valid() &&
               runtime::is_media_color_pixel_format(format.pixel);
    }
};

enum class ZeroCopyPolicy : std::uint8_t {
    Disabled = 0,
    Prefer,
    Require,
};

enum class MediaConversionKind : std::uint8_t {
    None = 0,
    RgbToNv12,
    RgbToP010,
};

enum class ZeroCopyProof : std::uint8_t {
    NotRequested = 0,
    BackendUnsupported,
    DestinationNotGraphBacked,
    DestinationNotGpuCompatible,
    DestinationPlanesMissing,
    Proven,
};

/// Derived media boundary plan. It deliberately stores no lifetime, release,
/// synchronization or allocation side tables: those remain authoritative in
/// CompiledResourceTable and are referenced by graph resource id/allocation id.
struct RenderToMediaPlan {
    graph::GraphNodeId source_resource{graph::k_invalid_node};
    graph::GraphNodeId destination_resource{graph::k_invalid_node};
    MediaSurfaceDesc destination{};
    ZeroCopyPolicy zero_copy_policy{ZeroCopyPolicy::Disabled};
    MediaConversionKind conversion{MediaConversionKind::None};
    ZeroCopyProof zero_copy_proof{ZeroCopyProof::NotRequested};
    graph::PhysicalAllocationId destination_allocation{
        graph::kInvalidPhysicalAllocationId};
    bool zero_copy_selected{false};

    [[nodiscard]] bool valid() const noexcept {
        return source_resource != graph::k_invalid_node &&
               destination_resource != graph::k_invalid_node &&
               destination.valid();
    }
};

class RenderToMediaResolver {
public:
    [[nodiscard]] static std::optional<RenderToMediaPlan> resolve(
        const graph::CompiledResourceTable& table,
        graph::GraphNodeId source_resource,
        graph::GraphNodeId destination_resource,
        const MediaSurfaceDesc& destination,
        ZeroCopyPolicy policy,
        bool backend_supports_native_video_surface) noexcept {
        if (!destination.valid()) return std::nullopt;

        const auto* source = table.resource_for(source_resource);
        const auto* target = table.resource_for(destination_resource);
        if (!source || !target) return std::nullopt;
        if (target->desc.width != destination.width ||
            target->desc.height != destination.height ||
            target->desc.format != destination.format) {
            return std::nullopt;
        }
        if (target->desc.format.pixel != runtime::PixelFormat::Nv12 &&
            target->desc.format.pixel != runtime::PixelFormat::P010) {
            return std::nullopt;
        }

        RenderToMediaPlan plan;
        plan.source_resource = source_resource;
        plan.destination_resource = destination_resource;
        plan.destination = destination;
        plan.zero_copy_policy = policy;
        plan.destination_allocation = target->physical_slot;

        if (source->desc.format == target->desc.format) {
            plan.conversion = MediaConversionKind::None;
        } else if (runtime::is_rgb_pixel_format(source->desc.format.pixel) &&
                   target->desc.format.pixel == runtime::PixelFormat::Nv12) {
            plan.conversion = MediaConversionKind::RgbToNv12;
        } else if (runtime::is_rgb_pixel_format(source->desc.format.pixel) &&
                   target->desc.format.pixel == runtime::PixelFormat::P010) {
            plan.conversion = MediaConversionKind::RgbToP010;
        } else {
            return std::nullopt;
        }

        if (policy == ZeroCopyPolicy::Disabled) {
            plan.zero_copy_proof = ZeroCopyProof::NotRequested;
            return plan;
        }

        if (!backend_supports_native_video_surface) {
            plan.zero_copy_proof = ZeroCopyProof::BackendUnsupported;
        } else if (target->physical_slot == graph::kInvalidPhysicalAllocationId &&
                   target->desc.lifetime != runtime::LifetimeClass::External) {
            plan.zero_copy_proof = ZeroCopyProof::DestinationNotGraphBacked;
        } else if (!target->physical.gpu_compatible) {
            plan.zero_copy_proof = ZeroCopyProof::DestinationNotGpuCompatible;
        } else if (!has_canonical_media_planes(*target)) {
            plan.zero_copy_proof = ZeroCopyProof::DestinationPlanesMissing;
        } else {
            plan.zero_copy_proof = ZeroCopyProof::Proven;
            plan.zero_copy_selected = true;
        }

        if (policy == ZeroCopyPolicy::Require && !plan.zero_copy_selected) {
            return std::nullopt;
        }
        return plan;
    }

private:
    [[nodiscard]] static bool has_canonical_media_planes(
        const graph::CompiledResourcePlan& resource) noexcept {
        if (resource.physical.plane_count != 2u ||
            resource.subresources.size() != 2u) {
            return false;
        }
        return resource.subresources[0].id == graph::ResourceSubresource::Plane0 &&
               resource.subresources[0].plane_index == 0u &&
               resource.subresources[1].id == graph::ResourceSubresource::Plane1 &&
               resource.subresources[1].plane_index == 1u;
    }
};

} // namespace chronon3d::media
