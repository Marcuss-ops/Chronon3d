#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace chronon3d::cli {

// ── Timing lexicon ─────────────────────────────────────────────────────────
// Shared by every Chronon3d timing surface (the receipt's `timing_ms`,
// `startup_breakdown`, `prepare_breakdown`, `exclusive_wall_timeline`):
//   * every phase value is milliseconds as a double under a `*_ms` key;
//   * an absent value (JSON null) means "the phase did not run / was not
//     measured" — never a fabricated 0, never a negative sentinel;
//   * `0` always means "measured, took ~0";
//   * one name per phase: the same phase keeps the same `*_ms` key across
//     blocks (`ffprobe_ms`, `sha256_ms`, `encoder_drain_finalize_ms`, ...).
// Derived aggregates (total_*, accounted_ms, unaccounted_ms,
// accounted_percent) are always computed and therefore always present.

struct StartupBreakdown {
    std::optional<double> logger_init_ms;
    std::optional<double> cli_bootstrap_ms;
    std::optional<double> cli_parse_ms;
    std::optional<double> composition_registration_ms;
    std::optional<double> plan_read_ms;
    std::optional<double> plan_json_parse_ms;
    std::optional<double> plan_decode_validate_ms;
    std::optional<double> plan_asset_resolve_ms;
    std::optional<double> plan_compile_ms;
    std::optional<double> encoder_create_ms;
    std::optional<double> encoder_open_hw_ctx_ms;
    std::optional<double> cuda_compositor_warmup_ms;
    std::optional<double> encoder_open_nvenc_ms;
    std::optional<double> encoder_open_mux_header_ms;
    std::optional<double> vulkan_instance_ms;
    std::optional<double> vulkan_device_ms;
    std::optional<double> vulkan_pipelines_ms;
    std::optional<double> renderer_runtime_init_ms;
    // Derived: whole startup wall, the sum of the measured phases above and
    // the unaccounted remainder.
    double total_startup_ms{0.0};
    double accounted_ms{0.0};
    double unaccounted_ms{0.0};
    // Derived: how many phase fields carry a measurement. Unmeasured phases
    // do not count (the old `> 0.0` probe is gone with the fake zeros).
    std::uint32_t phases_observed{0};
};

struct PrepareBreakdown {
    // Whole setup-time runtime::prepare_render call. Historically named
    // `font_preflight_ms`, which misattributed ~245 ms/item of image decode +
    // GPU promotion to font work (TICKET-PERFORMANCE-AUDIT-2026-10).
    std::optional<double> prepare_render_ms;
    std::optional<double> pool_warmup_ms;
    std::optional<double> triple_arena_alloc_ms;
    std::optional<double> writer_thread_spawn_ms;
    // Derived: prepare wall minus the measured prepare phases above.
    double other_prepare_ms{0.0};
    double total_prepare_ms{0.0};
};

} // namespace chronon3d::cli
