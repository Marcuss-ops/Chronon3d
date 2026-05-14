#include <chronon3d/render_graph/types.hpp>

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace chronon3d {
namespace render_graph {

u64 RenderCacheKey::digest() const {
    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);
    XXH64_update(state, scope.data(), scope.size());
    XXH64_update(state, &frame, sizeof(frame));
    XXH64_update(state, &width, sizeof(width));
    XXH64_update(state, &height, sizeof(height));
    XXH64_update(state, &params_hash, sizeof(params_hash));
    XXH64_update(state, &source_hash, sizeof(source_hash));
    XXH64_update(state, &input_hash, sizeof(input_hash));
    u64 hash = XXH64_digest(state);
    XXH64_freeState(state);
    return hash;
}

size_t RenderCacheKeyHash::operator()(const RenderCacheKey& key) const noexcept {
    return static_cast<size_t>(key.digest());
}

u64 hash_bytes(const void* data, usize size) {
    return XXH64(data, size, 0);
}

u64 hash_string(std::string_view value) {
    return XXH64(value.data(), value.size(), 0);
}

u64 hash_combine(u64 seed, u64 value) {
    seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}

u64 hash_vec2(const Vec2& value) {
    return hash_bytes(&value, sizeof(value));
}

u64 hash_vec3(const Vec3& value) {
    return hash_bytes(&value, sizeof(value));
}

u64 hash_color(const Color& color) {
    return hash_bytes(&color, sizeof(color));
}

u64 hash_transform(const Transform& transform) {
    u64 h = hash_vec3(transform.position);
    h = hash_combine(h, hash_vec3(transform.anchor));
    h = hash_combine(h, hash_vec3(transform.scale));
    h = hash_combine(h, hash_bytes(&transform.rotation, sizeof(transform.rotation)));
    h = hash_combine(h, hash_bytes(&transform.opacity, sizeof(transform.opacity)));
    return h;
}

} // namespace render_graph
} // namespace chronon3d
