#pragma once

#include <chronon3d/runtime/frame_format.hpp>
#include <chronon3d/runtime/resource_residency.hpp>

#include <cstddef>
#include <cstdint>

namespace chronon3d::runtime {

enum class ResourceKind : std::uint8_t { Color, Depth, Yuv, Bytes };

enum class LifetimeClass : std::uint8_t {
    FrameTransient,
    PipelineSlot,
    JobPersistent,
    External,
};

enum class ResourceUsage : std::uint8_t {
    Generic,
    ColorAttachment,
    DepthAttachment,
    Storage,
};

enum class TextAtlasEncoding : std::uint8_t {
    PremultipliedRGBA,
    Coverage,
    MTSDF,
};

/// Canonical backend-neutral description shared by render surfaces and
/// resource planning. FrameFormat owns pixel/color/alpha semantics.
struct ResourceDesc {
    std::uint32_t width{0};
    std::uint32_t height{0};
    FrameFormat format{};
    ResourceUsage usage{ResourceUsage::Generic};

    // Retained during the staged request-mirror cleanup. Zero means a tight
    // allocation derived from FrameFormat and extent.
    std::size_t bytes{0};
    std::size_t alignment{alignof(std::max_align_t)};
    LifetimeClass lifetime{LifetimeClass::FrameTransient};
    ResourceResidency residency{};
    TextAtlasEncoding text_atlas_encoding{TextAtlasEncoding::PremultipliedRGBA};

    constexpr ResourceDesc() noexcept = default;

    constexpr ResourceDesc(
        std::uint32_t width_value,
        std::uint32_t height_value,
        FrameFormat format_value,
        ResourceUsage usage_value,
        LifetimeClass lifetime_value,
        std::size_t bytes_value) noexcept
        : width(width_value),
          height(height_value),
          format(format_value),
          usage(usage_value),
          bytes(bytes_value),
          lifetime(lifetime_value) {}

    constexpr ResourceDesc(
        std::uint32_t width_value,
        std::uint32_t height_value,
        FrameFormat format_value,
        ResourceUsage usage_value,
        std::size_t bytes_value,
        std::size_t alignment_value,
        LifetimeClass lifetime_value,
        ResourceResidency residency_value) noexcept
        : width(width_value),
          height(height_value),
          format(format_value),
          usage(usage_value),
          bytes(bytes_value),
          alignment(alignment_value),
          lifetime(lifetime_value),
          residency(residency_value) {}

    constexpr ResourceDesc(
        std::uint32_t width_value,
        std::uint32_t height_value,
        PixelFormat pixel,
        ResourceUsage usage_value,
        LifetimeClass lifetime_value,
        std::size_t bytes_value,
        ColorMetadata color) noexcept
        : ResourceDesc(
              width_value,
              height_value,
              make_frame_format(pixel, color, color.alpha),
              usage_value,
              lifetime_value,
              bytes_value) {}

    [[nodiscard]] constexpr std::size_t tight_bytes() const noexcept {
        return tight_surface_bytes(format, width, height);
    }

    [[nodiscard]] constexpr std::size_t allocation_bytes() const noexcept {
        return bytes != 0 ? bytes : tight_bytes();
    }

    [[nodiscard]] static constexpr ResourceDesc make(
        std::uint32_t width,
        std::uint32_t height,
        FrameFormat format,
        ResourceUsage usage = ResourceUsage::Generic,
        LifetimeClass lifetime = LifetimeClass::FrameTransient,
        std::size_t alignment = alignof(std::max_align_t),
        ResourceResidency residency = {}) noexcept {
        return ResourceDesc{
            width,
            height,
            format,
            usage,
            tight_surface_bytes(format, width, height),
            alignment,
            lifetime,
            residency};
    }

    [[nodiscard]] static constexpr ResourceDesc make(
        std::uint32_t width,
        std::uint32_t height,
        PixelFormat pixel,
        ResourceUsage usage,
        LifetimeClass lifetime,
        ColorMetadata color) noexcept {
        const auto format = make_frame_format(pixel, color, color.alpha);
        return make(width, height, format, usage, lifetime);
    }
};

} // namespace chronon3d::runtime
