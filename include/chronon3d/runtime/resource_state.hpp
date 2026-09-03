#pragma once

#include <cstdint>

namespace chronon3d::runtime {

/// Backend-neutral pipeline stage mask used by the compiled resource-state
/// contract. Backends translate these bits into native synchronization
/// primitives; graph/runtime code never stores Vulkan types.
enum class PipelineStage : std::uint32_t {
    None = 0,
    ComputeShader = 1u << 0u,
    Transfer = 1u << 1u,
    Host = 1u << 2u,
    AllCommands = 1u << 3u,
    // Additive sync2-era stages (kept on the SAME underlying type and bit
    // positions so existing values never shift). Backends translate these
    // bits into native pipeline-stage masks.
    VertexShader = 1u << 4u,
    FragmentShader = 1u << 5u,
    ColorOutput = 1u << 6u,
    VideoDecode = 1u << 7u,
    VideoEncode = 1u << 8u,
    AllGraphics = 1u << 9u,
};

constexpr PipelineStage operator|(PipelineStage lhs, PipelineStage rhs) noexcept {
    return static_cast<PipelineStage>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

constexpr PipelineStage operator&(PipelineStage lhs, PipelineStage rhs) noexcept {
    return static_cast<PipelineStage>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

/// Backend-neutral memory-access mask. Read/write intent is derived from this
/// one mask; backends must not maintain a second access taxonomy.
enum class AccessMask : std::uint32_t {
    None = 0,
    ShaderRead = 1u << 0u,
    ShaderWrite = 1u << 1u,
    TransferRead = 1u << 2u,
    TransferWrite = 1u << 3u,
    HostRead = 1u << 4u,
    HostWrite = 1u << 5u,
    MemoryRead = 1u << 6u,
    MemoryWrite = 1u << 7u,
    // Additive sync2-era access bits (color attachment + video encode/decode).
    ColorRead = 1u << 8u,
    ColorWrite = 1u << 9u,
    VideoDecodeRead = 1u << 10u,
    VideoDecodeWrite = 1u << 11u,
    VideoEncodeRead = 1u << 12u,
    VideoEncodeWrite = 1u << 13u,
};

constexpr AccessMask operator|(AccessMask lhs, AccessMask rhs) noexcept {
    return static_cast<AccessMask>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

constexpr AccessMask operator&(AccessMask lhs, AccessMask rhs) noexcept {
    return static_cast<AccessMask>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

constexpr bool any(AccessMask value, AccessMask bits) noexcept {
    return static_cast<std::uint32_t>(value & bits) != 0u;
}

// Compatibility spelling for private stores that only need a state-map value
// type. This is deliberately an alias to the canonical AccessMask, not a
// second enum/authority.
using ResourceAccess = AccessMask;

enum class ResourceLayout : std::uint8_t {
    Undefined = 0,
    General,
    ShaderReadOnly,
    TransferSource,
    TransferDestination,
    // Additive layouts for color attachments, present/external interop and
    // the video encode/decode domains (NV12/P010 surfaces, encoder sinks).
    ColorAttachment,
    Present,
    External,
    VideoDecodeSrc,
    VideoDecodeDst,
    VideoEncodeSrc,
    VideoEncodeDst,
};

enum class QueueClass : std::uint8_t {
    GraphicsCompute = 0,
    Transfer,
    External,
    // Additive queue classes for compute-only and the video domains. A
    // queue-class change on a resource is modeled as an ownership transfer
    // (release/acquire) rather than a plain barrier.
    Compute,
    Decode,
    Encode,
};

enum class ResourceAspect : std::uint8_t {
    Color = 1u << 0u,
    Depth = 1u << 1u,
    Stencil = 1u << 2u,
    // Multi-planar media planes (NV12/P010): Plane0 = Y, Plane1 = UV,
    // Plane2 reserved for future 3-plane formats.
    Plane0 = 1u << 3u,
    Plane1 = 1u << 4u,
    Plane2 = 1u << 5u,
};

constexpr ResourceAspect operator|(ResourceAspect lhs, ResourceAspect rhs) noexcept {
    return static_cast<ResourceAspect>(
        static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

constexpr ResourceAspect operator&(ResourceAspect lhs, ResourceAspect rhs) noexcept {
    return static_cast<ResourceAspect>(
        static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
}

constexpr bool any(ResourceAspect value, ResourceAspect bits) noexcept {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(bits)) != 0u;
}

/// Explicit image subresource contract. Chronon's current color surfaces use
/// mip 0/layer 0, but synchronization no longer hard-codes that assumption.
/// This is the canonical image range type (the sync contract's
/// "ImageSubresourceRange"): it carries aspects (incl. Plane0/Plane1/Plane2),
/// mip span and layer span, and can answer overlap queries so a state
/// tracker never introduces a barrier for non-overlapping subresources.
struct SubresourceRange {
    ResourceAspect aspects{ResourceAspect::Color};
    std::uint32_t first_mip{0};
    std::uint32_t mip_count{1};
    std::uint32_t first_layer{0};
    std::uint32_t layer_count{1};

    [[nodiscard]] constexpr bool overlaps(const SubresourceRange& other) const noexcept {
        if ((static_cast<std::uint8_t>(aspects & other.aspects)) == 0u) return false;
        if (first_mip >= other.first_mip + other.mip_count) return false;
        if (other.first_mip >= first_mip + mip_count) return false;
        if (first_layer >= other.first_layer + other.layer_count) return false;
        if (other.first_layer >= first_layer + layer_count) return false;
        return true;
    }

    friend bool operator==(const SubresourceRange&, const SubresourceRange&) = default;
};

/// Canonical resource state produced by command/resource planning and consumed
/// by backends. Timeline scheduling is deliberately not represented here:
/// submission progression and memory/layout visibility remain separate.
struct ResourceState {
    PipelineStage stages{PipelineStage::None};
    AccessMask access{AccessMask::None};
    ResourceLayout layout{ResourceLayout::Undefined};
    QueueClass queue{QueueClass::GraphicsCompute};
    SubresourceRange range{};

    [[nodiscard]] bool reads() const noexcept {
        constexpr auto read_bits = AccessMask::ShaderRead |
                                   AccessMask::TransferRead |
                                   AccessMask::HostRead |
                                   AccessMask::MemoryRead |
                                   AccessMask::ColorRead |
                                   AccessMask::VideoDecodeRead |
                                   AccessMask::VideoEncodeRead;
        return any(access, read_bits);
    }

    [[nodiscard]] bool writes() const noexcept {
        constexpr auto write_bits = AccessMask::ShaderWrite |
                                    AccessMask::TransferWrite |
                                    AccessMask::HostWrite |
                                    AccessMask::MemoryWrite |
                                    AccessMask::ColorWrite |
                                    AccessMask::VideoDecodeWrite |
                                    AccessMask::VideoEncodeWrite;
        return any(access, write_bits);
    }

    [[nodiscard]] bool undefined() const noexcept {
        return layout == ResourceLayout::Undefined;
    }

    [[nodiscard]] static constexpr ResourceState undefined_state(
        SubresourceRange range = {}) noexcept {
        return ResourceState{
            PipelineStage::None,
            AccessMask::None,
            ResourceLayout::Undefined,
            QueueClass::GraphicsCompute,
            range};
    }

    [[nodiscard]] static constexpr ResourceState compute_read(
        SubresourceRange range = {}) noexcept {
        return ResourceState{
            PipelineStage::ComputeShader,
            AccessMask::ShaderRead,
            ResourceLayout::General,
            QueueClass::GraphicsCompute,
            range};
    }

    [[nodiscard]] static constexpr ResourceState compute_write(
        SubresourceRange range = {}) noexcept {
        return ResourceState{
            PipelineStage::ComputeShader,
            AccessMask::ShaderWrite,
            ResourceLayout::General,
            QueueClass::GraphicsCompute,
            range};
    }

    [[nodiscard]] static constexpr ResourceState compute_read_write(
        SubresourceRange range = {}) noexcept {
        return ResourceState{
            PipelineStage::ComputeShader,
            AccessMask::ShaderRead | AccessMask::ShaderWrite,
            ResourceLayout::General,
            QueueClass::GraphicsCompute,
            range};
    }

    friend bool operator==(const ResourceState&, const ResourceState&) = default;
};

} // namespace chronon3d::runtime
