#pragma once

namespace chronon3d::graph::text_run::detail {

enum class CachedTextExecutionPath {
    RejectUnsupportedAnimation,
    RejectForcedSoftware,
    TryMtsdf,
    PackedBitmap,
};

[[nodiscard]] const char* cached_text_execution_path_name(
    CachedTextExecutionPath path) noexcept;

/// Select the first cached-text authority without inspecting glyph data or
/// allocating atlas resources. The caller still owns runtime failure handling
/// and may fall back from MTSDF to the packed compatibility route.
[[nodiscard]] CachedTextExecutionPath resolve_cached_text_execution_path(
    bool unsupported_animation,
    bool force_software,
    bool prepared_cache_available,
    bool backend_available,
    bool surface_registry_available) noexcept;

} // namespace chronon3d::graph::text_run::detail
