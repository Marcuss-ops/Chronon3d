// ═══════════════════════════════════════════════════════════════════════════
// pipe_timing_sidecar_summary_v1.cpp — phase 4 (observability ownership).
//
// Chronon owns its telemetry. This TU emits the BOUNDED, schema-typed
// telemetry summary (`<video>.telemetry-summary.json`,
// chronon3d.render-telemetry-summary.v1) beside the RAW deep-profile timing
// sidecar. The two documents have distinct contracts:
//
//   * `<video>.telemetry-summary.json` — bounded, stable, schema'd. Hosts and
//     collectors (RenderingGen) may ingest this typed summary; it never
//     contains per-frame arrays and its shape only changes under an explicit
//     schema version bump.
//   * `<video>.timing.json` — the unbounded Chronon-owned deep profile
//     (frame_times_ms, node diagnostics). Consumers MUST treat it as opaque
//     bytes (hash → object store → reference) and never parse, mutate or
//     re-transport its internals.
//
// A host may therefore evolve Chronon's deep profiler without breaking any
// external consumer, and no orchestrator (RenderingGen) ever has to know the
// raw profile schema.
// ═══════════════════════════════════════════════════════════════════════════

#include "pipe_timing_sidecar_detail.hpp"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace chronon3d::cli::pipe_timing_detail {

namespace {

constexpr const char* kSummarySchema = "chronon3d.render-telemetry-summary.v1";
constexpr int kSummaryVersion = 1;

// The bounded summary copies the `job` section wholesale (it carries only
// scalar/object telemetry — process walls, GPU counters, encoder/prepare
// breakdowns — never per-frame arrays) and a scalar-only projection of the
// `summary` statistics. `first_frame` is deliberately excluded: it embeds a
// full per-frame diagnostic object.
void copy_summary_scalars(nlohmann::json& dst, const nlohmann::json& src) {
    if (!src.is_object()) return;
    for (auto it = src.begin(); it != src.end(); ++it) {
        if (it.key() == "first_frame") continue;  // deep per-frame detail
        const auto& value = it.value();
        if (value.is_number() || value.is_boolean() || value.is_string() ||
            value.is_null()) {
            dst[it.key()] = value;
        }
    }
}

} // namespace

void write_bounded_telemetry_summary_sidecar(
    const std::string& video_path, const nlohmann::json& raw_sidecar) {
    nlohmann::json telemetry;
    telemetry["schema"] = kSummarySchema;
    telemetry["version"] = kSummaryVersion;

    // summary statistics (bounded, scalar-only projection).
    if (raw_sidecar.contains("summary")) {
        copy_summary_scalars(telemetry["summary"], raw_sidecar["summary"]);
    }
    // job section: scalar job/process walls + GPU/prepare/image/text/encoder
    // counters. Bounded by construction (no per-frame arrays live here).
    if (raw_sidecar.contains("job")) {
        telemetry["job"] = raw_sidecar["job"];
    }
    // Bounded execution envelope for the native/zero-copy gates.
    telemetry["outcome"] = {{"status", "ok"}};

    const auto summary_path =
        std::filesystem::path(video_path).string() + ".telemetry-summary.json";
    std::ofstream file(summary_path);
    if (!file) {
        spdlog::warn("[video] Could not write telemetry summary sidecar: {}",
                     summary_path);
        return;
    }
    file << telemetry.dump(2) << '\n';
    spdlog::info("[video] Wrote bounded telemetry summary sidecar: {} (schema {})",
                 summary_path, kSummarySchema);
}

} // namespace chronon3d::cli::pipe_timing_detail
