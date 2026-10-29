#include <chronon3d/assets/hdri_environment.hpp>
#include <chronon3d/assets/asset_resolver.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace chronon3d::assets {
namespace {
constexpr std::size_t kHeaderBytes = 8 + 4 * 4 + 2 * 4 + 27 * 4;
constexpr std::uint32_t kMaxWidth = 8192;
constexpr std::uint32_t kMaxHeight = 4096;
constexpr std::uint64_t kMaxSourcePixels = 16ULL * 1024ULL * 1024ULL;

[[noreturn]] void invalid(const char* message) {
    throw std::invalid_argument(std::string("HDRIENV1: ") + message);
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t& cursor) {
    if (cursor > bytes.size() || bytes.size() - cursor < 4) invalid("truncated u32 field");
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data() + cursor);
    cursor += 4;
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}

float read_f32(std::span<const std::byte> bytes, std::size_t& cursor) {
    return std::bit_cast<float>(read_u32(bytes, cursor));
}
} // namespace

HdriEnvV1 decode_hdri_env_v1(std::span<const std::byte> bytes) {
    if (bytes.size() < kHeaderBytes) invalid("truncated header");
    if (bytes.size() > kHdriEnvV1MaxBytes) invalid("asset exceeds the 128 MiB byte limit");
    constexpr char kMagic[] = "HDRIENV1";
    if (std::memcmp(bytes.data(), kMagic, 8) != 0) invalid("invalid magic");

    std::size_t cursor = 8;
    HdriEnvV1 result;
    result.source_width = read_u32(bytes, cursor);
    result.source_height = read_u32(bytes, cursor);
    result.face_size = read_u32(bytes, cursor);
    const std::uint32_t mip_count = read_u32(bytes, cursor);
    result.rotation_radians = read_f32(bytes, cursor);
    result.intensity = read_f32(bytes, cursor);
    if (result.source_width == 0 || result.source_width > kMaxWidth ||
        result.source_height == 0 || result.source_height > kMaxHeight ||
        static_cast<std::uint64_t>(result.source_width) * result.source_height > kMaxSourcePixels)
        invalid("source dimensions exceed V1 limits");
    if (result.face_size == 0 || result.face_size > kHdriEnvV1MaxFaceSize ||
        !std::has_single_bit(result.face_size)) invalid("face size must be a power of two up to 1024");
    const auto expected_mips = static_cast<std::uint32_t>(std::bit_width(result.face_size));
    if (mip_count != expected_mips) invalid("mip count does not describe a complete chain to 1x1");
    if (!std::isfinite(result.rotation_radians) || !std::isfinite(result.intensity) ||
        result.intensity < 0.f || result.intensity > 16.f)
        invalid("rotation or intensity is outside supported finite limits");
    for (float& coefficient : result.irradiance_sh) {
        coefficient = read_f32(bytes, cursor);
        if (!std::isfinite(coefficient) || std::abs(coefficient) > 1.0e6f)
            invalid("SH coefficient is outside supported finite limits");
    }

    std::uint64_t total_floats = 0;
    result.mips.reserve(mip_count);
    std::uint32_t expected_size = result.face_size;
    for (std::uint32_t level = 0; level < mip_count; ++level) {
        const auto size = read_u32(bytes, cursor);
        if (size != expected_size) invalid("mip dimensions are not a complete halving chain");
        const std::uint64_t face_floats = static_cast<std::uint64_t>(size) * size * 3;
        const std::uint64_t level_floats = face_floats * 6;
        if (level_floats > kHdriEnvV1MaxFloats - total_floats)
            invalid("asset exceeds the 32M float output limit");
        total_floats += level_floats;
        if (face_floats > (bytes.size() - cursor) / sizeof(float) / 6)
            invalid("truncated mip face data");
        HdriEnvV1Mip mip;
        mip.size = size;
        for (auto& face : mip.faces) {
            face.resize(static_cast<std::size_t>(face_floats));
            for (float& component : face) {
                component = read_f32(bytes, cursor);
                if (!std::isfinite(component) || component < 0.f)
                    invalid("radiance texel must be finite and non-negative");
            }
        }
        result.mips.push_back(std::move(mip));
        expected_size = std::max(1u, expected_size / 2);
    }
    if (cursor != bytes.size()) invalid("trailing bytes are not permitted");
    std::uint64_t content_hash = 14695981039346656037ULL;
    for (const std::byte value : bytes) {
        content_hash ^= std::to_integer<std::uint8_t>(value);
        content_hash *= 1099511628211ULL;
    }
    result.content_hash = content_hash;
    return result;
}

std::array<float, 3> sample_hdri_env_v1(
    const HdriEnvV1& probe, std::array<float, 3> direction, float mip_level) {
    const float length = std::sqrt(direction[0]*direction[0] + direction[1]*direction[1] + direction[2]*direction[2]);
    if (!std::isfinite(length) || length <= 1e-8f || probe.mips.empty() || !std::isfinite(mip_level))
        return {};
    for (float& component : direction) component /= length;
    const float ax = std::abs(direction[0]), ay = std::abs(direction[1]), az = std::abs(direction[2]);
    unsigned face{};
    float u{}, v{}, major{};
    if (ax >= ay && ax >= az) {
        major = ax;
        if (direction[0] >= 0.f) { face = 0; u = -direction[2] / major; v = -direction[1] / major; }
        else { face = 1; u = direction[2] / major; v = -direction[1] / major; }
    } else if (ay >= az) {
        major = ay;
        if (direction[1] >= 0.f) { face = 2; u = direction[0] / major; v = direction[2] / major; }
        else { face = 3; u = direction[0] / major; v = -direction[2] / major; }
    } else {
        major = az;
        if (direction[2] >= 0.f) { face = 4; u = direction[0] / major; v = -direction[1] / major; }
        else { face = 5; u = -direction[0] / major; v = -direction[1] / major; }
    }
    const auto sample_face = [&](const HdriEnvV1Mip& mip) {
        const float px = std::clamp((u + 1.f) * .5f * static_cast<float>(mip.size - 1), 0.f,
                                    static_cast<float>(mip.size - 1));
        const float py = std::clamp((v + 1.f) * .5f * static_cast<float>(mip.size - 1), 0.f,
                                    static_cast<float>(mip.size - 1));
        const auto x0 = static_cast<std::uint32_t>(std::floor(px));
        const auto y0 = static_cast<std::uint32_t>(std::floor(py));
        const auto x1 = std::min(x0 + 1, mip.size - 1), y1 = std::min(y0 + 1, mip.size - 1);
        const float tx = px - static_cast<float>(x0), ty = py - static_cast<float>(y0);
        std::array<float, 3> color{};
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto at = [&](std::uint32_t x, std::uint32_t y) {
                return mip.faces[face][(static_cast<std::size_t>(y) * mip.size + x) * 3 + channel];
            };
            const float top = at(x0,y0) + tx * (at(x1,y0) - at(x0,y0));
            const float bottom = at(x0,y1) + tx * (at(x1,y1) - at(x0,y1));
            color[channel] = top + ty * (bottom - top);
        }
        return color;
    };
    const float level = std::clamp(mip_level, 0.f, static_cast<float>(probe.mips.size() - 1));
    const auto lo = static_cast<std::size_t>(std::floor(level));
    const auto hi = std::min(lo + 1, probe.mips.size() - 1);
    const float t = level - static_cast<float>(lo);
    auto a = sample_face(probe.mips[lo]);
    const auto b = sample_face(probe.mips[hi]);
    for (std::size_t c = 0; c < 3; ++c) a[c] += t * (b[c] - a[c]);
    return a;
}

Result<HdriEnvV1, HdriEnvV1LoadError> load_hdri_env_v1(
    const AssetResolver& resolver, std::string_view logical_path) {
    const auto path = resolver.resolve_logical(std::filesystem::path(logical_path));
    if (!path) return HdriEnvV1LoadError{"asset reference could not be resolved within the mounted root"};
    std::error_code ec;
    const auto size = std::filesystem::file_size(*path, ec);
    if (ec || size > kHdriEnvV1MaxBytes)
        return HdriEnvV1LoadError{"asset must be readable and no larger than 128 MiB"};
    try {
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        std::ifstream input(*path, std::ios::binary);
        if (!input || (size != 0 && !input.read(reinterpret_cast<char*>(bytes.data()),
                                                static_cast<std::streamsize>(size))))
            return HdriEnvV1LoadError{"asset could not be read completely"};
        return decode_hdri_env_v1(bytes);
    } catch (const std::exception& error) {
        return HdriEnvV1LoadError{error.what()};
    }
}
} // namespace chronon3d::assets
