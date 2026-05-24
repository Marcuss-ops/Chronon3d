#pragma once

#include <chronon3d/core/types/types.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::cache {

enum class VideoPixelFormat {
    RGBA8,
    YUV420P,
    NV12,
};

struct VideoFrameKey {
    std::string composition_id;
    u64 frame_index{0};
    i32 width{0};
    i32 height{0};
    VideoPixelFormat format{VideoPixelFormat::YUV420P};
    u64 scene_hash{0};
    u64 render_hash{0};

    [[nodiscard]] bool operator==(const VideoFrameKey&) const = default;
    [[nodiscard]] u64 digest() const;
};

struct VideoFrameKeyHash {
    [[nodiscard]] size_t operator()(const VideoFrameKey& key) const noexcept;
};

class VideoFrame {
public:
    VideoFrame() = default;
    VideoFrame(i32 width, i32 height, VideoPixelFormat format);

    [[nodiscard]] i32 width() const { return m_width; }
    [[nodiscard]] i32 height() const { return m_height; }
    [[nodiscard]] VideoPixelFormat format() const { return m_format; }
    [[nodiscard]] const std::vector<uint8_t>& bytes() const { return m_bytes; }
    [[nodiscard]] std::vector<uint8_t>& bytes() { return m_bytes; }
    [[nodiscard]] const uint8_t* data() const { return m_bytes.data(); }
    [[nodiscard]] uint8_t* data() { return m_bytes.data(); }
    [[nodiscard]] size_t size() const { return m_bytes.size(); }
    [[nodiscard]] bool empty() const { return m_bytes.empty(); }

    [[nodiscard]] size_t expected_size() const;
    void resize(i32 width, i32 height, VideoPixelFormat format);

private:
    i32 m_width{0};
    i32 m_height{0};
    VideoPixelFormat m_format{VideoPixelFormat::YUV420P};
    std::vector<uint8_t> m_bytes;
};

class VideoFrameCache {
public:
    using Value = std::shared_ptr<VideoFrame>;

    [[nodiscard]] bool contains(const VideoFrameKey& key) const;
    [[nodiscard]] const Value* find(const VideoFrameKey& key) const;
    void store(VideoFrameKey key, Value value);
    [[nodiscard]] bool erase(const VideoFrameKey& key);
    void clear();
    [[nodiscard]] size_t size() const { return m_entries.size(); }

private:
    std::unordered_map<VideoFrameKey, Value, VideoFrameKeyHash> m_entries;
};

} // namespace chronon3d::cache
