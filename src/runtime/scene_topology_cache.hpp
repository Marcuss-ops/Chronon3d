#pragma once

// ===========================================================================
// runtime/scene_topology_cache.hpp
//
// The scene topology produced by SceneMaterializer for a CompiledProgram is
// immutable and depends only on the program fingerprint. It used to live in a
// 32-entry cache owned by RenderPipeline, and SoftwareRenderer::render_program
// creates one RenderPipeline per call. Probe, warmup and the export loop
// therefore started from a cold cache and rebuilt the same scene (72 misses /
// 0 hits in the P4 job, three passes per item).
//
// SoftwareExecutionContext now carries a shared cache: every pipeline built
// for the same renderer sees the same materializations. The cache stays
// bounded (32 entries) and uses the canonical LruCache primitive, so it is
// thread-safe and cannot grow with the number of jobs.
// ===========================================================================

#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/scene/model/core/scene.hpp>

#include <cstddef>
#include <memory>

namespace chronon3d::runtime {

class SceneTopologyCache final {
public:
    static constexpr std::size_t kDefaultCapacity = 32;

    using Cache = cache::LruCache<compiler::ProgramFingerprint,
                                  std::shared_ptr<const Scene>,
                                  compiler::ProgramFingerprintHash>;

    explicit SceneTopologyCache(
        std::size_t capacity = kDefaultCapacity)
        : m_cache(capacity, /*num_shards=*/2, cache::CapacityMode::Count) {}

    [[nodiscard]] Cache& cache() noexcept { return m_cache; }
    [[nodiscard]] const Cache& cache() const noexcept { return m_cache; }

private:
    Cache m_cache;
};

} // namespace chronon3d::runtime
