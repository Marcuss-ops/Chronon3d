#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/core/counters.hpp>
#include <cstdlib>
#include <limits>
#include <string>

namespace chronon3d::cache {

namespace {

size_t resolve_default_max_bytes(size_t fallback) {
    const char* env = std::getenv("CHRONON_FB_POOL_MAX_MB");
    if (!env || !*env) {
        return fallback;
    }
    std::string env_str(env);
    if (env_str == "illimitato" || env_str == "unlimited") {
        return std::numeric_limits<size_t>::max();
    }
    try {
        const size_t mb = static_cast<size_t>(std::stoull(env));
        if (mb == 0) {
            return fallback;
        }
        return mb * 1024ULL * 1024ULL;
    } catch (...) {
        return fallback;
    }
}

int round_up_bucket(int val, int bucket = 64) {
    return ((val + bucket - 1) / bucket) * bucket;
}

} // namespace

FramebufferPool::FramebufferPool(size_t max_bytes)
    : m_max_bytes(resolve_default_max_bytes(max_bytes)) {}

void FramebufferPool::set_arena(std::shared_ptr<FramebufferArena> arena) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_arena = std::move(arena);
}

std::shared_ptr<Framebuffer> FramebufferPool::acquire(int width, int height, bool clear) {
    const auto t0 = std::chrono::high_resolution_clock::now();
    auto fb = acquire_unique(width, height);
    const auto t1 = std::chrono::high_resolution_clock::now();

    if (profiling::g_current_counters) {
        profiling::g_current_counters->framebuffer_acquire_ms.fetch_add(
            static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t1 - t0).count()),
            std::memory_order_relaxed);
    }

    if (clear) {
        const auto tc0 = std::chrono::high_resolution_clock::now();
        fb->clear(Color::transparent());
        const auto tc1 = std::chrono::high_resolution_clock::now();
        if (profiling::g_current_counters) {
            profiling::g_current_counters->framebuffer_clear_ms.fetch_add(
                static_cast<uint64_t>(std::chrono::duration<double, std::milli>(tc1 - tc0).count()),
                std::memory_order_relaxed);
        }
    }
    auto weak_pool = weak_from_this();
    return std::shared_ptr<Framebuffer>(fb.release(), [weak_pool](Framebuffer* ptr) {
        if (auto pool = weak_pool.lock()) {
            pool->release(ptr);
        } else {
            delete ptr;
        }
    });
}

std::unique_ptr<Framebuffer> FramebufferPool::acquire_unique(int width, int height) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const int rounded_w = round_up_bucket(width, 64); 
    const int rounded_h = round_up_bucket(height, 64);

    {
        FramebufferPoolKey key{rounded_w, rounded_h};
        auto it = m_free.find(key);
        if (it != m_free.end() && !it->second.empty()) {
            auto& bucket = it->second;
            auto fb = std::move(bucket.back());
            bucket.pop_back();
            m_current_bytes -= fb->size_bytes();
            if (profiling::g_current_counters) {
                profiling::g_current_counters->framebuffer_reuses.fetch_add(1, std::memory_order_relaxed);
            }
            fb->resize_logical(width, height);
            return fb;
        } else {
            if (profiling::g_current_counters) {
                if (it == m_free.end()) {
                    profiling::g_current_counters->framebuffer_pool_miss_count_size_mismatch.fetch_add(1, std::memory_order_relaxed);
                } else {
                    profiling::g_current_counters->framebuffer_pool_miss_count_empty.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    }

    if (m_arena) {
        void* ptr = m_arena->allocate(static_cast<size_t>(rounded_w) * rounded_h * sizeof(Color));
        if (ptr) {
            auto fb = std::make_unique<Framebuffer>(rounded_w, rounded_h, static_cast<Color*>(ptr));
            fb->resize_logical(width, height);
            return fb;
        }
    }

    auto fb = std::make_unique<Framebuffer>(rounded_w, rounded_h);
    fb->resize_logical(width, height);
    return fb;
}

std::shared_ptr<Framebuffer> FramebufferPool::acquire_pooled(int width, int height, std::shared_ptr<FramebufferPool> pool, bool clear) {
    CHRONON_ZONE_C("framebuffer_acquire", trace_category::kPipeline);
    if (!pool) {
        return acquire(width, height, clear);
    }

    auto fb = pool->acquire_unique(width, height);
    if (clear) {
        fb->clear(Color::transparent());
    }
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
    CHRONON_ZONE_C("framebuffer_release", trace_category::kPipeline);
    if (!fb) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::unique_ptr<Framebuffer> owned(fb);
    
    // Restore full allocated size so the Framebuffer is perfectly standardized in the pool
    const int alloc_w = owned->allocated_width();
    const int alloc_h = owned->allocated_height();
    owned->resize_logical(alloc_w, alloc_h);

    const size_t weight = owned->size_bytes();
    if (m_current_bytes + weight > m_max_bytes) {
        // Pool is full; drop the framebuffer
        return;
    }

    FramebufferPoolKey key{alloc_w, alloc_h};
    m_current_bytes += weight;
    m_free[key].push_back(std::move(owned));

    if (profiling::g_current_counters) {
        profiling::g_current_counters->framebuffer_buffer_returned_to_pool_count.fetch_add(1, std::memory_order_relaxed);
    }
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

size_t FramebufferPool::preallocate(const FramebufferPoolPreallocOptions& options) {
    if (options.width <= 0 || options.height <= 0 || options.count == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    const int rounded_w = round_up_bucket(options.width, 64);
    const int rounded_h = round_up_bucket(options.height, 64);

    FramebufferPoolKey key{rounded_w, rounded_h};
    auto& bucket = m_free[key];

    size_t created = 0;

    for (size_t i = 0; i < options.count; ++i) {
        auto fb = std::make_unique<Framebuffer>(rounded_w, rounded_h);

        if (options.clear) {
            fb->clear(Color::transparent());
        } else if (options.touch_memory) {
            // Touch the pixel data to commit physical pages.
            // We write-back the same values to avoid changing visual content.
            for (i32 y = 0; y < rounded_h; ++y) {
                Color* row = fb->pixels_row(y);
                for (i32 x = 0; x < rounded_w; ++x) {
                    // Read and write-back to force OS page commitment
                    Color c = row[x];
                    row[x] = c;
                }
            }
        }

        const size_t weight = fb->size_bytes();

        if (m_current_bytes + weight > m_max_bytes) {
            break;
        }

        m_current_bytes += weight;
        bucket.push_back(std::move(fb));
        ++created;
    }

    return created;
}

size_t FramebufferPool::max_bytes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_max_bytes;
}

FramebufferPoolStats FramebufferPool::stats() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t count = 0;
    for (const auto& [key, bucket] : m_free) {
        count += bucket.size();
    }

    return FramebufferPoolStats{
        .current_bytes = m_current_bytes,
        .available_count = count,
        .max_bytes = m_max_bytes
    };
}

} // namespace chronon3d::cache
