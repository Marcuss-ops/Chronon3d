// ──────────────────────────────────────────────────────────────────────────────
// Unit test: populate_run_host_attribs (apps/chronon3d_cli/utils/telemetry/telemetry_record.hpp)
//
// Two scenarios are covered:
//   1. Default-constructed RenderTelemetryRecord: every host field is filled
//      after one call.  This is the canonical "no-drift" invariant the helper
//      was extracted to guarantee for JSONL-fallback writes.
//   2. Pre-populated RenderTelemetryRecord: caller-supplied string fields are
//      preserved (if-empty semantics); bytes_allocated_peak is re-snapshotted
//      every call because it is a process-wide gauge that is only meaningful
//      when taken at the point of run finalization.
//
// Skip the entire file when /proc/cpuinfo is missing — the helper probes the
// Linux kernel for several host fields, and on systems without /proc the
// results would not be informative.
// ──────────────────────────────────────────────────────────────────────────────

#include <doctest/doctest.h>

// apps/chronon3d_cli/utils/telemetry/telemetry_record.hpp transitively pulls
// in telemetry_capture.hpp, which references (a) the inline variable
// `chronon3d::profiling::g_peak_live_framebuffer_bytes` (declared in
// core/memory/framebuffer.hpp) and (b) methods on `chronon3d::cache::FramebufferPool`
// (forward-declared in many headers but the full definition lives only in
// cache/framebuffer_pool.hpp).  Other CLI tests happen to #include these
// before telemetry headers, masking the transitive incomplete-type errors.
// This test does not call any FramebufferPool APIs itself; the includes are
// present solely to keep the helper's header self-contained when this TU
// sees it.
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <apps/chronon3d_cli/utils/telemetry/telemetry_record.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>

#include <filesystem>
#include <string>

using chronon3d::telemetry::RenderTelemetryRecord;
// Compact alias for cli::telemetry::populate_run_host_attribs call sites.
// `using namespace chronon3d::cli;` does NOT bring the bare name `cli`
// into scope as a namespace alias; a using-declaration OR a namespace
// alias brings the short-form `cli` qualifier into scope.
namespace cli = chronon3d::cli;

namespace {

/// Skip the host-attribute tests on systems where /proc/cpuinfo is missing
/// (the helper queries Linux kernel-provided CPU feature fields; without them
/// the assertions below would be unreliable).
bool host_attrib_environment_available() {
    return std::filesystem::exists("/proc/cpuinfo");
}

} // namespace

// ── Test 1 — default record → every host field populated ─────────────────────
TEST_CASE("populate_run_host_attribs: default record yields non-empty host fields") {
    if (!host_attrib_environment_available()) {
        MESSAGE("Skipping: /proc/cpuinfo not available on this platform");
        return;
    }

    RenderTelemetryRecord run;

    // Pre-conditions: a default-constructed record is fully empty.
    CHECK(run.os.empty());
    CHECK(run.cpu_model.empty());
    CHECK(run.cores == 0);
    CHECK(run.compiler_info.empty());
    CHECK(run.build_type.empty());
    CHECK(run.git_commit_short.empty());
    CHECK(run.bytes_allocated_peak == 0);

    cli::telemetry::populate_run_host_attribs(run);

    // Post-conditions: every host field is populated.  The helper does not
    // discriminate between partially- and fully-filled records; it stamps the
    // canonical values once and the JSONL writer copies them verbatim.
    CHECK_FALSE(run.os.empty());
    CHECK_FALSE(run.cpu_model.empty());
    CHECK(run.cores > 0);
    CHECK_FALSE(run.compiler_info.empty());
    CHECK_FALSE(run.build_type.empty());
    CHECK_FALSE(run.git_commit_short.empty());
    CHECK(run.bytes_allocated_peak > 0);
}

// ── Test 2 — caller-set fields: preserve strings, re-snapshot gauge ──────────
TEST_CASE("populate_run_host_attribs: pre-populated strings preserved, bytes_allocated_peak re-snapshotted") {
    if (!host_attrib_environment_available()) {
        MESSAGE("Skipping: /proc/cpuinfo not available on this platform");
        return;
    }

    // The helper uses if-empty semantics for descriptive strings (so callers
    // can pre-set them from setup-time data) but bytes_allocated_peak is a
    // process-wide gauge that is only meaningful when sampled at the point of
    // run finalization — therefore it is overwritten unconditionally.
    RenderTelemetryRecord run;
    run.os                 = "preset-OS";
    run.cpu_model          = "preset-CPU";
    run.cores              = 12;
    run.compiler_info      = "preset-Compiler";
    run.build_type         = "preset-Build";
    run.git_commit_short   = "deadbeef";
    run.bytes_allocated_peak = 999999;

    cli::telemetry::populate_run_host_attribs(run);

    // Pre-populated descriptive fields are NOT clobbered.
    CHECK(run.os               == "preset-OS");
    CHECK(run.cpu_model        == "preset-CPU");
    CHECK(run.cores            == 12);
    CHECK(run.compiler_info    == "preset-Compiler");
    CHECK(run.build_type       == "preset-Build");
    CHECK(run.git_commit_short == "deadbeef");

    // bytes_allocated_peak is unconditionally re-snapshotted to the
    // current process high-water mark at the time of the call.
    CHECK(run.bytes_allocated_peak != 999999u);
    // Stronger assertion: the new snapshot must be a plausible memory
    // value (zero would mean nothing is loaded, which is unrealistic
    // for a typical test process that has linked stdlib + libfmt + spdlog).
    CHECK(run.bytes_allocated_peak > 0);
}
