#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::runtime {

enum class MediaCodecId : std::uint8_t {
    Unknown,
    H264,
    HEVC,
    AV1,
    ProRes
};

enum class MediaPixelFormat : std::uint8_t {
    Unknown,
    NV12,
    P010,
    RGBA,
    YUV420P
};

/// Lookup key for persistent hardware codec sessions.
struct MediaSessionKey {
    MediaCodecId codec{MediaCodecId::Unknown};
    std::uint32_t width{0};
    std::uint32_t height{0};
    MediaPixelFormat pixel_format{MediaPixelFormat::Unknown};
    std::uint32_t device_id{0};
    bool is_encoder{false};

    bool operator==(const MediaSessionKey& other) const noexcept {
        return codec == other.codec &&
               width == other.width &&
               height == other.height &&
               pixel_format == other.pixel_format &&
               device_id == other.device_id &&
               is_encoder == other.is_encoder;
    }
};

struct MediaSessionKeyHasher {
    std::size_t operator()(const MediaSessionKey& k) const noexcept {
        std::size_t h = 0x9e3779b9;
        h ^= static_cast<std::size_t>(k.codec) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(k.width) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(k.height) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(k.pixel_format) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(k.device_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= (k.is_encoder ? 1ULL : 0ULL) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

/// Opaque wrapper for a reusable decoder or encoder hardware instance.
struct ReusableMediaSession {
    std::uintptr_t codec_ctx_handle{0};
    std::uintptr_t hw_frames_ctx_handle{0};
    std::uint64_t last_used_timestamp{0};
    bool in_use{false};
};

/// Cache of hardware decoder/encoder instances to prevent repetitive surface
/// and driver allocation costs between subsequent render invocations.
class MediaSessionPool {
public:
    MediaSessionPool() = default;
    ~MediaSessionPool() { clear(); }

    MediaSessionPool(const MediaSessionPool&) = delete;
    MediaSessionPool& operator=(const MediaSessionPool&) = delete;

    void register_session(const MediaSessionKey& key, ReusableMediaSession session) {
        std::lock_guard lock(m_mutex);
        m_sessions[key].push_back(session);
    }

    [[nodiscard]] std::size_t cached_session_count() const noexcept {
        std::lock_guard lock(m_mutex);
        std::size_t count = 0;
        for (const auto& [k, v] : m_sessions) count += v.size();
        return count;
    }

    void clear() noexcept {
        std::lock_guard lock(m_mutex);
        m_sessions.clear();
    }

private:
    std::unordered_map<MediaSessionKey, std::vector<ReusableMediaSession>, MediaSessionKeyHasher> m_sessions;
    mutable std::mutex m_mutex;
};

} // namespace chronon3d::runtime
