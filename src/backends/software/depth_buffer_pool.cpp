#include <chronon3d/backends/software/depth_buffer_pool.hpp>

#include <cstring>
#include <limits>
#include <stdexcept>

namespace chronon3d {

DepthBufferPool::DepthBufferPool()
    : m_state(std::make_shared<State>()) {}

DepthBufferPool::~DepthBufferPool() = default;

std::size_t DepthBufferPool::round_to_bucket(int dim) noexcept {
    constexpr std::size_t bucket = 64;
    const auto value = static_cast<std::size_t>(dim);
    return ((value + bucket - 1) / bucket) * bucket;
}

std::span<float> DepthBufferPool::acquire(int width, int height) {
    if (width <= 0 || height <= 0) return {};

    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h) {
        throw std::length_error("DepthBufferPool dimensions overflow");
    }
    const auto needed = w * h;
    const auto bucket_width = round_to_bucket(width);
    const auto bucket_height = round_to_bucket(height);
    if (bucket_width > std::numeric_limits<std::size_t>::max() / bucket_height) {
        throw std::length_error("DepthBufferPool bucket dimensions overflow");
    }
    const auto capacity = bucket_width * bucket_height;

    // The map is only locked while locating/creating the calling worker's
    // bucket. Rendering uses the worker-owned vector without holding this
    // mutex, so parallel graph nodes do not serialize rasterization.
    WorkerBuffer* worker = nullptr;
    {
        std::lock_guard lock(m_state->mutex);
        auto& worker_slot = m_state->workers[std::this_thread::get_id()];
        if (!worker_slot) worker_slot = std::make_unique<WorkerBuffer>();
        worker = worker_slot.get();
        if (capacity > worker->values.size()) {
            worker->values.resize(capacity, 0.0f);
        }
    }

    // Always clear the used portion, including after a grow: vector::resize
    // only zeroes appended elements, while prior rasterization wrote depth
    // values into the existing prefix.
    std::memset(worker->values.data(), 0, needed * sizeof(float));
    return {worker->values.data(), needed};
}

void DepthBufferPool::reset() {
    std::lock_guard lock(m_state->mutex);
    m_state->workers.clear();
}

DepthBufferPool::DepthBufferPool(DepthBufferPool&& other) noexcept
    : m_state(std::move(other.m_state))
{
    if (!m_state) m_state = std::make_shared<State>();
    // Keep the moved-from pool valid: callers may reuse a session-owned pool
    // after a container move, and acquire() is specified to remain usable.
    other.m_state = std::make_shared<State>();
}

DepthBufferPool& DepthBufferPool::operator=(DepthBufferPool&& other) noexcept {
    if (this != &other) {
        m_state = std::move(other.m_state);
        if (!m_state) m_state = std::make_shared<State>();
        other.m_state = std::make_shared<State>();
    }
    return *this;
}

} // namespace chronon3d
