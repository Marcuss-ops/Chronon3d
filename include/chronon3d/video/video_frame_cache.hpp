#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace chronon3d::video {

struct VideoFrameKey {
    std::string path;
    Frame frame{0};
    i32 width{0};
    i32 height{0};

    bool operator==(const VideoFrameKey&) const = default;
};

struct VideoFrameKeyHash {
    size_t operator()(const VideoFrameKey& key) const noexcept;
};

class VideoFrameCache {
public:
    std::shared_ptr<Framebuffer> find(const VideoFrameKey& key) const;
    void store(VideoFrameKey key, std::shared_ptr<Framebuffer> fb);
    void clear();
    usize size() const;

private:
    std::unordered_map<VideoFrameKey, std::shared_ptr<Framebuffer>, VideoFrameKeyHash> m_entries;
};

} // namespace chronon3d::video
