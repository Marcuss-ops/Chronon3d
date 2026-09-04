// =============================================================================
// include/chronon3d/cache/compiled_artifact_cache.hpp
//
// CompiledArtifactCache — save/load compiled compositions to/from disk.
// Cache family: ProgramCache (persistent I/O adapter).
//
// This type does not implement an in-memory cache primitive or eviction policy;
// canonical runtime key/value caching remains delegated to cache::LruCache.
// The "Cache" name describes persisted compiled-program reuse only.
//
// Pipeline:
//   Save: CompiledComposition → FlatBuffers → zstd compress → atomic disk write
//   Load: disk read → zstd decompress → FlatBuffers verifier → version check
//         → hydrate runtime objects (re-resolve processors against live registry)
//
// Each artifact is a single compressed FlatBuffer with a version envelope.
// The cache directory is a flat namespace keyed by artifact name (typically
// the composition id + a content digest).
//
// Thread-safe: all methods are const and the underlying I/O is serialised
// per artifact name via internal locks.
// =============================================================================
#pragma once

#include <chronon3d/timeline/compiled_composition.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::cache {

/// Immutable handle returned by a successful load.  The caller owns the
/// reconstituted CompiledComposition and the provenance metadata.
struct LoadedArtifact {
    /// The reconstituted compiled composition, ready for rendering.
    /// May be empty (nullptr) if the artifact was incompatible with the
    /// current backend or processor registry.
    std::shared_ptr<CompiledComposition> composition;

    /// Git commit that produced this artifact (empty if unknown).
    std::string provenance_git_commit;

    /// True when the FlatBuffer passed the verifier and the version was
    /// recognised, but the processor snapshots are incompatible.
    /// The caller should recompile rather than silently accept a partial load.
    bool processor_incompatible{false};
};

/// Save/load compiled composition artifacts.
///
/// Usage:
///   CompiledArtifactCache cache("/var/cache/chronon3d/artifacts");
///
///   // Save
///   cache.save("intro-93_a1b2c3d4", compiled_composition);
///
///   // Load
///   auto loaded = cache.load("intro-93_a1b2c3d4");
///   if (loaded && loaded->composition) {
///       engine.render_compiled(*loaded->composition, frame);
///   }
class CompiledArtifactCache {
public:
    /// Construct a cache rooted at `directory`.  The directory is created
    /// on first save if it doesn't exist.
    explicit CompiledArtifactCache(std::filesystem::path directory);

    /// Save a compiled composition to disk under the given `name`.
    /// The compressed artifact is written atomically (temp file → rename).
    ///
    /// Throws std::runtime_error on I/O failure or serialisation error.
    ///
    /// `name` must be filesystem-safe (alphanumeric + underscores + hyphens).
    /// No path traversal — the name is appended directly to the cache directory.
    void save(std::string_view name, const CompiledComposition& compiled) const;

    /// Load a compiled composition from disk by `name`.  Returns nullopt
    /// if the artifact does not exist or is corrupt.
    ///
    /// The FlatBuffer is verified before any field access.  If the format
    /// version is unrecognised, the load is rejected (nullopt).
    ///
    /// If the artifact is valid but the processor snapshots are incompatible
    /// with the current runtime, LoadedArtifact::processor_incompatible is
    /// set to true and `composition` is nullopt.
    [[nodiscard]] std::optional<LoadedArtifact> load(
        std::string_view name) const;

    /// Check whether an artifact exists for the given name.
    [[nodiscard]] bool contains(std::string_view name) const noexcept;

    /// Remove one artifact.  No-op if it doesn't exist.
    void remove(std::string_view name) const;

    /// Remove all artifacts in this cache directory.
    /// Returns the number of files removed.
    std::size_t clear() const;

    /// Cache directory path.
    [[nodiscard]] const std::filesystem::path& directory() const noexcept {
        return m_directory;
    }

private:
    std::filesystem::path m_directory;

    // Internal helpers.
    [[nodiscard]] std::filesystem::path artifact_path(
        std::string_view name) const;

    static bool is_safe_name(std::string_view name) noexcept;
};

}  // namespace chronon3d::cache