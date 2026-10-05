#pragma once

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace chronon3d::backends::vulkan::pipeline_cache_io {

inline constexpr std::size_t kMaximumBytes = 64U * 1024U * 1024U;
inline constexpr std::size_t kHeaderBytes = 4U * sizeof(std::uint32_t) + VK_UUID_SIZE;

inline bool valid(std::span<const char> bytes, const VkPhysicalDeviceProperties& device) {
    if (bytes.size() < kHeaderBytes || bytes.size() > kMaximumBytes) return false;
    std::uint32_t header[4]{};
    std::memcpy(header, bytes.data(), sizeof(header));
    return header[0] >= kHeaderBytes && header[0] <= bytes.size() &&
           header[1] == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
           header[2] == device.vendorID && header[3] == device.deviceID &&
           std::memcmp(bytes.data() + sizeof(header), device.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}

inline std::string filename(const VkPhysicalDeviceProperties& device, std::uint64_t shader_hash) {
    constexpr char hex[] = "0123456789abcdef";
    std::string uuid;
    uuid.reserve(VK_UUID_SIZE * 2);
    for (const auto byte : device.pipelineCacheUUID) {
        uuid.push_back(hex[byte >> 4]);
        uuid.push_back(hex[byte & 15]);
    }
    return "vk_pipeline_cache_" + std::to_string(device.vendorID) + "_" +
           std::to_string(device.deviceID) + "_" + std::to_string(device.driverVersion) +
           "_" + uuid + "_" + std::to_string(shader_hash) + ".bin";
}

inline std::vector<char> read(const std::filesystem::path& file,
                             const VkPhysicalDeviceProperties& device) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto size = input.tellg();
    if (size < static_cast<std::streamoff>(kHeaderBytes) ||
        size > static_cast<std::streamoff>(kMaximumBytes)) return {};
    std::vector<char> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) ||
        !valid(bytes, device)) return {};
    return bytes;
}

// Unique sibling temporary files plus rename give readers a complete old or
// new blob, never a partially truncated file. Concurrent writers may replace
// one another's performance hints, but cannot corrupt the published bytes.
inline bool write_atomic(const std::filesystem::path& file, std::span<const char> bytes,
                         const VkPhysicalDeviceProperties& device) {
    if (!valid(bytes, device)) return false;
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if (error) return false;
    std::string temporary = file.string() + ".tmp.XXXXXX";
    const int descriptor = ::mkstemp(temporary.data());
    if (descriptor < 0) return false;
    std::size_t written = 0;
    bool success = true;
    while (written < bytes.size()) {
        const auto result = ::write(descriptor, bytes.data() + written, bytes.size() - written);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) { success = false; break; }
        written += static_cast<std::size_t>(result);
    }
    if (::close(descriptor) != 0) success = false;
    if (success && ::rename(temporary.c_str(), file.c_str()) == 0) return true;
    (void)::unlink(temporary.c_str());
    return false;
}

} // namespace chronon3d::backends::vulkan::pipeline_cache_io
