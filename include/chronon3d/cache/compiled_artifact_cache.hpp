// =============================================================================
// include/chronon3d/cache/compiled_artifact_cache.hpp
//
// Persistent ProgramCache adapter for typed GPU-program artifacts.
// In-memory runtime caching and eviction remain owned by cache::LruCache.
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace chronon3d::cache {

/// Canonical identity for one compiled GPU-program artifact.
///
/// This is target/compiler-aware: a PTX artifact is not interchangeable with
/// SPIR-V, another CUDA architecture, or another compiler ABI. Residency
/// handles remain owned by the device session.
struct ProgramArtifactKey {
    std::uint64_t semantic_program_fingerprint{0};
    std::string lowering_kind;
    std::string target_backend;
    std::string gpu_architecture;
    std::string compiler_identity;
    std::string compiler_version;
    std::string compile_flags;
    std::uint32_t abi_version{1};

    /// Stable ProgramCache identity derived from every key field.
    [[nodiscard]] std::string stable_name() const;
};

/// Persistent storage for typed compiled GPU-program payloads.
class CompiledArtifactCache {
public:
    explicit CompiledArtifactCache(std::filesystem::path directory);

    /// Save a non-empty payload atomically under its canonical key.
    void save_program_artifact(const ProgramArtifactKey& key,
                               std::span<const std::uint8_t> payload) const;

    /// Load a payload, returning nullopt for a miss or invalid/corrupt data.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    load_program_artifact(const ProgramArtifactKey& key) const;

    /// Remove all persisted GPU-program artifacts. Returns files removed.
    std::size_t clear() const;

    [[nodiscard]] const std::filesystem::path& directory() const noexcept {
        return m_directory;
    }

private:
    std::filesystem::path m_directory;

    [[nodiscard]] std::filesystem::path program_artifact_path(
        const ProgramArtifactKey& key) const;
};

}  // namespace chronon3d::cache
