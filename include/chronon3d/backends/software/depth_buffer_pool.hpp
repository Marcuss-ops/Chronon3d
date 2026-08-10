#pragma once

#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

namespace chronon3d {

/**
 * DepthBufferPool — reusable per-frame depth buffers for mesh rasterization.
 *
 * Eliminates the `std::vector<float> depth_buffer(w*h)` allocation that
 * happened on every `SoftwareMeshProcessor::draw()` and `draw_fake_box3d()`
 * call. The pool owns one reusable buffer per calling worker thread, so
 * parallel graph nodes never race on the same depth span.
 *
 * `acquire(width, height)` lazily creates/resizes the calling thread's
 * bucket, zeroes its used portion, and returns a span valid until the next
 * acquire on that same thread. `reset()` releases all buckets at a job
 * boundary; it must not run concurrently with rendering.
 */
class DepthBufferPool {
public:
    DepthBufferPool();
    ~DepthBufferPool();

    DepthBufferPool(const DepthBufferPool&) = delete;
    DepthBufferPool& operator=(const DepthBufferPool&) = delete;

    DepthBufferPool(DepthBufferPool&& other) noexcept;
    DepthBufferPool& operator=(DepthBufferPool&& other) noexcept;

    /// Return a zeroed, reusable depth span for the calling worker thread.
    [[nodiscard]] std::span<float> acquire(int width, int height);

    /// Release all per-worker storage. Call only between render jobs/frames.
    void reset();

private:
    struct WorkerBuffer {
        std::vector<float> values;
    };
    struct State {
        std::mutex mutex;
        std::unordered_map<std::thread::id, std::unique_ptr<WorkerBuffer>> workers;
    };

    std::shared_ptr<State> m_state;

    static std::size_t round_to_bucket(int dim) noexcept;
};

} // namespace chronon3d