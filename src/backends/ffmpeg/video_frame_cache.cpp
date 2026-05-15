#include <chronon3d/backends/ffmpeg/video_frame_cache.hpp>
#include <xxhash.h>

namespace chronon3d::video {

size_t VideoFrameKeyHash::operator()(const VideoFrameKey& key) const noexcept {
    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);
    XXH64_update(state, key.path.data(), key.path.size());
    XXH64_update(state, &key.frame, sizeof(key.frame));
    XXH64_update(state, &key.width, sizeof(key.width));
    XXH64_update(state, &key.height, sizeof(key.height));
    auto hash = XXH64_digest(state);
    XXH64_freeState(state);
    return static_cast<size_t>(hash);
}

std::shared_ptr<Framebuffer> VideoFrameCache::find(const VideoFrameKey& key) const {
    auto it = m_entries.find(key);
    if (it == m_entries.end()) return nullptr;
    return it->second;
}

void VideoFrameCache::store(VideoFrameKey key, std::shared_ptr<Framebuffer> fb) {
    if (!fb) return;
    m_entries.insert_or_assign(std::move(key), std::move(fb));
}

void VideoFrameCache::clear() {
    m_entries.clear();
}

usize VideoFrameCache::size() const {
    return m_entries.size();
}

} // namespace chronon3d::video
