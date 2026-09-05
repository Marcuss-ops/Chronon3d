#pragma once

#include <chronon3d/cache/node_cache.hpp>

#include <string>
#include <utility>

namespace chronon3d::cache {

/// Canonical construction API for render-node cache identities.
///
/// The builder starts from NodeCacheKey's canonical zero/default state, so
/// omitted temporal and tile fields retain their legacy values. Callers may
/// provide only the identity dimensions relevant to their node; `build()`
/// returns an ordinary value type accepted by all existing cache APIs.
class NodeCacheIdentityBuilder {
public:
    explicit NodeCacheIdentityBuilder(std::string scope) {
        m_key.scope = std::move(scope);
    }

    NodeCacheIdentityBuilder& scope(std::string value) {
        m_key.scope = std::move(value);
        return *this;
    }

    NodeCacheIdentityBuilder& frame(Frame value) {
        m_key.frame = value;
        return *this;
    }

    NodeCacheIdentityBuilder& output(i32 width, i32 height) {
        m_key.width = width;
        m_key.height = height;
        return *this;
    }

    NodeCacheIdentityBuilder& params(u64 value) {
        m_key.params_hash = value;
        return *this;
    }

    NodeCacheIdentityBuilder& source(u64 value) {
        m_key.source_hash = value;
        return *this;
    }

    NodeCacheIdentityBuilder& input(u64 value) {
        m_key.input_hash = value;
        return *this;
    }

    NodeCacheIdentityBuilder& versions(
        ContentVersion params,
        ContentVersion source,
        ContentVersion input = 0) {
        m_key.params_version = params;
        m_key.source_version = source;
        m_key.input_version = input;
        return *this;
    }

    NodeCacheIdentityBuilder& hashes(u64 params_hash, u64 source_hash, u64 input_hash = 0) {
        m_key.params_hash = params_hash;
        m_key.source_hash = source_hash;
        m_key.input_hash = input_hash;
        return *this;
    }

    NodeCacheIdentityBuilder& temporal(TemporalSampleKey value) {
        m_key.temporal_key = value;
        return *this;
    }

    NodeCacheIdentityBuilder& tile(i32 x, i32 y, i32 size, u64 hash = 0) {
        m_key.tile_x = x;
        m_key.tile_y = y;
        m_key.tile_size = size;
        m_key.tile_hash = hash;
        return *this;
    }

    /// Fold evaluated camera state using the existing canonical mixer.
    NodeCacheIdentityBuilder& camera(const ::chronon3d::Camera2_5D& value) {
        fold_camera_into_params_hash(m_key, value);
        return *this;
    }

    NodeCacheIdentityBuilder& camera_if(
        bool enabled,
        const ::chronon3d::Camera2_5D& value) {
        if (enabled) {
            camera(value);
        }
        return *this;
    }

    [[nodiscard]] NodeCacheKey build() const {
        return m_key;
    }

private:
    NodeCacheKey m_key{};
};

/// Canonical conditional camera fold for nodes that evolve an existing key
/// (NodeCacheKey::cache_key() implementations that re-fold per evaluation).
/// This is the same contract NodeCacheIdentityBuilder::camera_if() enforces
/// at construction time; nodes that mutate `m_key` per frame must use this
/// helper so the camera-fingerprint invariant has exactly one implementation
/// and no recall-based discipline.
inline void fold_camera_if(
    NodeCacheKey& key,
    bool has_camera,
    const ::chronon3d::Camera2_5D& value) {
    if (has_camera) {
        fold_camera_into_params_hash(key, value);
    }
}

} // namespace chronon3d::cache
