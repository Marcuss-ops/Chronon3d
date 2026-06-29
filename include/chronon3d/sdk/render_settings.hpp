#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// include/chronon3d/sdk/render_settings.hpp
//
// P1-A — Skeleton SDK settings type.
// ═════════════════════════════════════════════════════════════════════════════
//
// Neutral ABI-friendly POD that does NOT expose backend-specific knobs.
// Extensible: future versions may add new fields with defaults that
// preserve binary compatibility for older consumers.
//
// NOTE: this type is distinct from the internal
// `chronon3d::backends::software::RenderSettings` (which carries
// backend-specific knobs visible only via the OPP-side header in
// plan §5).  The SDK type is the public stable surface; the internal
// type remains where it is.
// ═════════════════════════════════════════════════════════════════════════════

namespace chronon3d::sdk {

/// SDK-facing render settings.  Minimal neutral POD.
/// Default-constructible to a sane 1920×1080 / 1×MSAA / no-motion-blur set.
struct RenderSettings {
    int  width{1920};                  ///< Output width  in pixels
    int  height{1080};                 ///< Output height in pixels
    int  antialiasing_samples{1};      ///< >=1; 1 disables MSAA
    bool motion_blur{false};           ///< Enable analogue motion-blur path
    bool dirty_rects{false};           ///< Use dirty-rect optimisation
    bool deterministic{false};         ///< Force deterministic scheduling
    int  max_threads{0};               ///< 0 = auto-detect
};

} // namespace chronon3d::sdk
