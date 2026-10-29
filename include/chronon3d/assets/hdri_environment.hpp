#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <span>
#include <vector>
#include <chronon3d/core/types/result.hpp>

namespace chronon3d::assets {
class AssetResolver;

inline constexpr std::uint64_t kHdriEnvV1MaxFloats = 32ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kHdriEnvV1MaxBytes = 128ULL * 1024ULL * 1024ULL;
inline constexpr std::uint32_t kHdriEnvV1MaxFaceSize = 1024;

struct HdriEnvV1Mip {
    std::uint32_t size{0};
    // +X, -X, +Y, -Y, +Z, -Z; row-major RGB float values.
    std::array<std::vector<float>, 6> faces{};
};

struct HdriEnvV1 {
    std::uint64_t content_hash{0};
    std::uint32_t source_width{0};
    std::uint32_t source_height{0};
    std::uint32_t face_size{0};
    float rotation_radians{0.f};
    float intensity{1.f};
    std::array<float, 27> irradiance_sh{};
    std::vector<HdriEnvV1Mip> mips{};
};

struct HdriEnvV1LoadError { std::string message; };

/// Decode the deterministic little-endian HDRIENV1 asset. Throws
/// std::invalid_argument for malformed payloads or resource-limit violations.
[[nodiscard]] HdriEnvV1 decode_hdri_env_v1(std::span<const std::byte> bytes);

/// Bilinear cubemap lookup at a fractional roughness mip level. Direction uses
/// the payload's +X/-X/+Y/-Y/+Z/-Z face convention.
[[nodiscard]] std::array<float, 3> sample_hdri_env_v1(
    const HdriEnvV1& probe, std::array<float, 3> direction, float mip_level);

/// Resolve and decode a logical asset under the caller's mounted asset root.
[[nodiscard]] Result<HdriEnvV1, HdriEnvV1LoadError> load_hdri_env_v1(
    const AssetResolver& resolver, std::string_view logical_path);

} // namespace chronon3d::assets
