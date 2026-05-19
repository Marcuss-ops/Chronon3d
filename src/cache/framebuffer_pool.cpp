#include <chronon3d/cache/framebuffer_pool.hpp>

namespace chronon3d::cache {

FramebufferPool::FramebufferPool(size_t max_bytes)
    : m_max_bytes(max_bytes) {}

std::shared_ptr<Framebuffer> FramebufferPool::acquire(int width, int height) {
    std::lock_guard<std::mutex> lock(m_mutex);

    FramebufferPoolKey key{width, height};
    auto& bucket = m_free[key];

    if (!bucket.empty()) {
        auto fb = std::move(bucket.back());
        bucket.pop_back();
        m_current_bytes -= fb->size_bytes();
        fb->clear(Color::transparent());
        return fb;
    }

    auto fb = std::make_shared<Framebuffer>(width, height);
    fb->clear(Color::transparent());
    return fb;
}

void FramebufferPool::release(std::shared_ptr<Framebuffer> fb) {
    if (!fb) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    const size_t weight = fb->size_bytes();
    if (m_current_bytes + weight > m_max_bytes) {
        // Pool is full; drop the framebuffer
        return;
    }

    FramebufferPoolKey key{fb->width(), fb->height()};
    m_current_bytes += weight;
    m_free[key].push_back(std::move(fb));
}

void FramebufferPool::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_free.clear();
    m_current_bytes = 0;
}

size_t FramebufferPool::current_bytes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_current_bytes;
}

size_t FramebufferPool::available_count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    for (const auto& [key, bucket] : m_free) {
        count += bucket.size();
    }
    return count;
}

} // namespace chronon3d::cache
