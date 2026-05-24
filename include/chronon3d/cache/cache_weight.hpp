#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <memory>
#include <vector>

namespace chronon3d::cache {

/// Returns the memory footprint in bytes of a Framebuffer.
inline size_t framebuffer_weight_bytes(const Framebuffer& fb) {
    return fb.size_bytes();
}

/// Overload for shared_ptr<Framebuffer>; returns 0 for null.
inline size_t framebuffer_weight_bytes(const std::shared_ptr<Framebuffer>& fb) {
    return fb ? fb->size_bytes() : 0;
}

/// Returns the total byte weight of a vector (capacity * sizeof(T)).
template <typename T>
inline size_t vector_weight_bytes(const std::vector<T>& v) {
    return v.size() * sizeof(T);
}

} // namespace chronon3d::cache
