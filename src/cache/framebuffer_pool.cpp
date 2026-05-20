#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/trace.hpp>
#include <chronon3d/core/counters.hpp>

namespace chronon3d::cache {

FramebufferPool::FramebufferPool(size_t max_bytes)
    : m_max_bytes(max_bytes) {}

std::shared_ptr<Framebuffer> FramebufferPool::acquire(int width, int height, bool clear) {
    auto fb = acquire_unique(width, height, clear);
    auto weak_pool = weak_from_this();
    return std::shared_ptr<Framebuffer>(fb.release(), [weak_pool](Framebuffer* ptr) {
        if (auto pool = weak_pool.lock()) {
            pool->release(ptr);
        } else {
            delete ptr;
        }
    });
}

std::unique_ptr<Framebuffer> FramebufferPool::acquire_unique(int width, int height, bool clear) {
    std::lock_guard<std::mutex> lock(m_mutex);

    FramebufferPoolKey key{width, height};
    auto& bucket = m_free[key];

    if (!bucket.empty()) {
        auto fb = std::move(bucket.back());
        bucket.pop_back();
        m_current_bytes -= fb->size_bytes();
        if (clear) {
            fb->clear(Color::transparent());
        }

        if (profiling::g_current_counters) {
            profiling::g_current_counters->framebuffer_reuses.fetch_add(1, std::memory_order_relaxed);
        }

        return fb;
    }

    auto fb = std::make_unique<Framebuffer>(width, height);
    if (clear) {
        fb->clear(Color::transparent());
    }
    return fb;
}

std::shared_ptr<Framebuffer> FramebufferPool::acquire_pooled(int width, int height, std::shared_ptr<FramebufferPool> pool, bool clear) {
    if (!pool) {
        return acquire(width, height, clear);
    }

    auto fb = pool->acquire_unique(width, height, clear);
    std::weak_ptr<FramebufferPool> weak_pool = pool;
    return std::shared_ptr<Framebuffer>(fb.release(), [weak_pool](Framebuffer* ptr) {
        if (auto locked = weak_pool.lock()) {
            locked->release(ptr);
        } else {
            delete ptr;
        }
    });
}

void FramebufferPool::release(Framebuffer* fb) {
    if (!fb) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::unique_ptr<Framebuffer> owned(fb);
    const size_t weight = owned->size_bytes();
    if (m_current_bytes + weight > m_max_bytes) {
        // Pool is full; drop the framebuffer
        return;
    }

    FramebufferPoolKey key{owned->width(), owned->height()};
    m_current_bytes += weight;
    m_free[key].push_back(std::move(owned));
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
