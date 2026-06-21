#pragma once

// ===========================================================================
// assets/asset_resolver.hpp
//
// WP-8 PR 8.0 — typed, engine-local asset path resolver.  Every
// `chronon3d::runtime::RenderRuntime` owns exactly one
// `AssetResolver`.  Resolution is deterministic and per-engine: two
// runtimes with different mount roots resolve the same relative path
// to different absolute paths.  The resolver does NOT consult any
// process-wide global state (TICKET-011a) — only the instance's
// mount root.
//
// Behaviour (all return `std::optional<std::filesystem::path>`):
//
//   - empty input                       → std::nullopt
//   - absolute path                     → lexically_normal (mount ignored)
//   - relative with no mount            → std::nullopt  (missing-asset)
//   - relative under mount, exists      → mount/relative, normalized
//   - relative under mount, missing     → std::nullopt  (resolve() only;
//                                                  resolve_lexical() skips
//                                                  the on-disk check)
//   - relative that escapes the mount
//       via ".." segments               → std::nullopt  (clamp)
//   - relative under root whose exact
//       prefix matches a sibling mount
//       (e.g. mount="/X" + "Y/..." while
//        "/XY/..." exists on disk)      → std::nullopt  (sibling-prefix)
//
// The class is thread-safe: mount / resolve all go through a std::mutex
// guarding m_root_path.  Match the sister AssetRegistry's shape
// (non-copyable; default-constructed unmounted).
// ===========================================================================

#include <filesystem>
#include <mutex>
#include <optional>

namespace chronon3d::assets {

class AssetResolver {
public:
    AssetResolver() = default;
    ~AssetResolver() = default;

    AssetResolver(const AssetResolver&)            = delete;
    AssetResolver& operator=(const AssetResolver&) = delete;

    /// Set the engine-local mount root.  Subsequent resolve() calls
    /// use this root for relative paths.  Pass an empty path to
    /// unmount.
    ///
    /// Throws `std::invalid_argument` when `root_path` is non-empty
    /// but not absolute.  PR 8.0 contract: every Resolver is engine-
    /// owned, so mount roots must be absolute to preserve the
    /// "deterministic and independent per engine" guarantee.
    void mount(std::filesystem::path root_path);

    /// Clear the mount root.  resolve() returns std::nullopt for all
    /// relative paths until mount() is called again.
    void unmount();

    /// True when a non-empty mount root is currently configured.
    [[nodiscard]] bool has_mount() const noexcept;

    /// Read the current mount root (snapshot — copied out under lock).
    /// Returns an empty path when unmounted.
    [[nodiscard]] std::filesystem::path mount_root() const;

    /// Resolve `path` with on-disk existence check.  Returns
    /// std::nullopt for any of the missing-asset conditions listed at
    /// the class doc-comment.
    [[nodiscard]] std::optional<std::filesystem::path>
    resolve(const std::filesystem::path& path) const;

    /// Resolve `path` WITHOUT touching the filesystem.  Faster than
    /// resolve() for hot paths (preflight validation, font lookup).
    /// Same clamp + missing semantics as resolve() except
    /// "exists?" is replaced by "is the lexical combination within
    /// mount?".
    [[nodiscard]] std::optional<std::filesystem::path>
    resolve_lexical(const std::filesystem::path& path) const;

private:
    /// Common implementation used by resolve() and resolve_lexical().
    /// Caller holds m_mutex.  Returns the candidate path or
    /// std::nullopt for the missing / clamp / sibling-prefix cases.
    [[nodiscard]] std::optional<std::filesystem::path>
    resolve_locked_(const std::filesystem::path& path) const;

    mutable std::mutex    m_mutex;
    std::filesystem::path m_root_path;
};

} // namespace chronon3d::assets
