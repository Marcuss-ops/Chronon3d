// Contract test for the single v2 timing sidecar.
//
// A5 (TICKET-HOTSPOT-DECOMPOSITION): the summary and job sections are built by
// directly callable serializers (build_summary_section / serialize_job_section),
// so the schema can be frozen here instead of being an incidental property of a
// 600-line function. Both halves matter:
//
//   * the KEY SETS are the schema contract a future refactor must preserve;
//   * the VALUES prove each typed field is wired to its own key, and that a
//     measurement nobody took is reported as null rather than as a zero the
//     gate would read as a real (and terrible) measurement.

#include <doctest/doctest.h>

#include "commands/video/exporters/pipe_timing_sidecar.hpp"
#include "commands/video/exporters/pipe_timing_sidecar_detail.hpp"
#include "commands/video/exporters/pipe_timing_sidecar_serialize.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>

namespace {

using chronon3d::cli::pipe_timing::JobTimings;
using chronon3d::cli::pipe_timing_detail::SidecarContext;

std::set<std::string> keys_of(const nlohmann::json& object) {
    std::set<std::string> keys;
    for (const auto& item : object.items()) keys.insert(item.key());
    return keys;
}

/// The job section's frozen schema. Any addition, removal or rename must be a
/// deliberate change to this list — the zero-copy gates read these keys.
const std::set<std::string>& frozen_job_keys() {
    static const std::set<std::string> keys{
        "process_wall_ms", "measurement_kind", "execution_path",
        "execution_reason", "execution_downgrade_count",
        "surface_handoff_path", "job_wall_ms", "engine_init_ms",
        "backend_init_ms", "plan_read_ms", "plan_parse_ms", "plan_validate_ms",
        "plan_compile_ms", "graph_compile_ms", "prepare_ms",
        "render_loop_wall_ms", "encoder_finalize_ms", "mux_finalize_ms",
        "output_finalize_ms", "validation_ms", "ffprobe_ms", "sha256_ms",
        "full_file_reread_count", "hash", "pipeline", "encoder_settings",
        "gpu", "effect_usage", "prepare", "image", "text", "encoder",
    };
    return keys;
}

const std::set<std::string>& frozen_summary_keys() {
    static const std::set<std::string> keys{
        "first_frame_ms", "min_frame_ms", "max_frame_ms", "mean_frame_ms",
        "avg_frame_ms", "stddev_frame_ms", "p50_frame_ms", "p90_frame_ms",
        "p95_frame_ms", "p99_frame_ms", "warmup_frames", "warmup_avg_ms",
        "steady_avg_ms", "steady_p50_ms", "steady_p95_ms", "steady_p99_ms",
        "render_only_fps", "render_loop_fps", "end_to_end_fps", "measured_fps",
        "realtime_factor", "frame_budget_ms", "frames_over_budget",
        "over_budget_ratio", "graph_reused_frames", "graph_reused_ratio",
        "fast_path_reused_frames", "gpu_device_timing", "windows",
        "first_frame", "frame_to_nvenc", "target_fps", "target_fps_num",
        "target_fps_den",
    };
    return keys;
}

/// One measured frame, so the summary serializer has the `frames.front()` it
/// documents as a precondition.
SidecarContext context_with_one_frame(double wall_time_ms, double render_ms) {
    SidecarContext ctx;
    chronon3d::telemetry::FrameTelemetry frame;
    frame.frame_number = 0;
    frame.duration_ms = 4.0;
    ctx.frames.push_back(frame);
    ctx.wall_time_ms = wall_time_ms;
    ctx.render_ms = render_ms;
    ctx.is_native = false;
    return ctx;
}

} // namespace

TEST_CASE("video timing sidecar emits one v3 document with an inline summary") {
    const auto video = std::filesystem::temp_directory_path()
        / "chronon3d_timing_sidecar_schema_test.mp4";
    const auto sidecar = std::filesystem::path(video.string() + ".timing.json");
    const auto legacy_summary =
        std::filesystem::path(video.string() + ".telemetry-summary.json");
    std::filesystem::remove(sidecar);
    std::filesystem::remove(legacy_summary);

    chronon3d::telemetry::FrameTelemetry frame;
    frame.frame_number = 0;
    frame.duration_ms = 1.0;
    chronon3d::cli::pipe_timing::JobTimings timings;

    chronon3d::cli::write_frame_timing_sidecar(
        video.string(), {frame}, {}, 1.0, 1.0, 0.0, timings, false);

    REQUIRE(std::filesystem::exists(sidecar));
    std::ifstream input(sidecar);
    const auto document = nlohmann::json::parse(input);

    CHECK(chronon3d::cli::pipe_timing::kTimingSidecarSchema ==
          "chronon3d.frame-timing.v3");
    CHECK(chronon3d::cli::pipe_timing::kTimingSidecarVersion == 3);
    CHECK(document.at("schema") == chronon3d::cli::pipe_timing::kTimingSidecarSchema);
    CHECK(document.at("version") == chronon3d::cli::pipe_timing::kTimingSidecarVersion);
    const std::set<std::string> v3_additive_keys{
        "schema", "version", "video", "wall_time_ms", "render_ms",
        "encode_close_ms", "frames_total", "time_source", "frame_times_ms",
        "summary", "job", "cache", "memory", "exclusive_wall_timeline",
        "phase_attribution", "internal_profiling", "startup_breakdown",
        "prepare_breakdown",
    };
    std::set<std::string> emitted_keys;
    for (const auto& item : document.items()) emitted_keys.insert(item.key());
    CHECK(emitted_keys == v3_additive_keys);
    CHECK_FALSE(std::filesystem::exists(legacy_summary));

    std::filesystem::remove(sidecar);
}

// ── A5: the typed serializer ───────────────────────────────────────────────

TEST_CASE("A5 sidecar: the optional serializer spells an absent measurement as null") {
    using chronon3d::cli::pipe_timing_detail::put_optional;

    nlohmann::json object = nlohmann::json::object();
    put_optional(object, "absent_ms", std::optional<double>{});
    put_optional(object, "absent_count", std::optional<std::uint64_t>{});
    put_optional(object, "absent_name", std::optional<std::string>{});
    put_optional(object, "zero_ms", std::optional<double>{0.0});
    put_optional(object, "zero_count", std::optional<std::uint64_t>{0u});
    put_optional(object, "measured_ms", std::optional<double>{2.5});
    put_optional(object, "measured_count", std::optional<std::uint64_t>{7u});
    put_optional(object, "measured_name", std::optional<std::string>{"nvenc"});

    // Not measured ≠ measured zero. Both are reported as what they are.
    CHECK(object.at("absent_ms").is_null());
    CHECK(object.at("absent_count").is_null());
    CHECK(object.at("absent_name").is_null());
    CHECK(object.at("zero_ms") == 0.0);
    CHECK(object.at("zero_count") == 0u);
    CHECK(object.at("measured_ms") == 2.5);
    CHECK(object.at("measured_count") == 7u);
    CHECK(object.at("measured_name") == "nvenc");
}

TEST_CASE("A5 sidecar: the job serializer publishes each typed field under its own key") {
    JobTimings timings;
    timings.process_wall_ms = 111.5;
    timings.measurement_kind = "clock";
    timings.execution_path = "direct_yuv";
    timings.execution_reason = "resolved";
    timings.execution_downgrade_count = 2u;
    timings.surface_handoff_path = "direct";
    timings.job_wall_ms = 222.25;
    timings.encoder_finalize_ms = 33.5;
    timings.full_file_reread_count = 7u;
    timings.hash.output_write_bytes = 4096u;
    timings.pipeline.queue_depth_peak = 5u;
    timings.encoder_settings.preset = "p4";
    timings.encoder_settings.async_depth = 3;
    timings.gpu.gpu_readback_bytes = 8192u;
    timings.gpu.nvenc_frames = 100u;
    timings.effect_usage["effect.color.curves"] = {3u, 1u, 4u, 5000000u, 1u, 2000000u};
    timings.gpu.cuda_composite_frames = 42u;
    timings.gpu.upload_breakdown["text_atlas"] = 999u;
    timings.prepare.asset_preflight_ms = 4.5;
    timings.image_draw_ms = 6.5;
    timings.image_draw_count = 11u;
    timings.text.shaping_ms = 1.5;
    timings.text.atlas_cache_hits = 13u;
    timings.encoder.submit_cpu_ms = 2.5;
    timings.encoder.cuda_pending_peak = 9u;

    nlohmann::json job = nlohmann::json::object();
    chronon3d::cli::pipe_timing_detail::serialize_job_section(job, timings);

    CHECK(job.at("process_wall_ms") == 111.5);
    CHECK(job.at("measurement_kind") == "clock");
    CHECK(job.at("execution_path") == "direct_yuv");
    CHECK(job.at("execution_reason") == "resolved");
    CHECK(job.at("execution_downgrade_count") == 2u);
    CHECK(job.at("surface_handoff_path") == "direct");
    CHECK(job.at("job_wall_ms") == 222.25);
    CHECK(job.at("encoder_finalize_ms") == 33.5);
    CHECK(job.at("full_file_reread_count") == 7u);
    CHECK(job.at("hash").at("output_write_bytes") == 4096u);
    CHECK(job.at("pipeline").at("queue_depth_peak") == 5u);
    CHECK(job.at("encoder_settings").at("preset") == "p4");
    CHECK(job.at("encoder_settings").at("async_depth") == 3);
    CHECK(job.at("gpu").at("gpu_readback_bytes") == 8192u);
    CHECK(job.at("effect_usage").at("effect.color.curves").at("native_frames") == 3u);
    CHECK(job.at("effect_usage").at("effect.color.curves").at("admission_rejected_frames") == 1u);
    CHECK(job.at("effect_usage").at("effect.color.curves").at("software_dispatches") == 4u);
    CHECK(job.at("effect_usage").at("effect.color.curves").at("effect_time_ns") == 5000000u);
    CHECK(job.at("effect_usage").at("effect.color.curves").at("fallback_count") == 1u);
    CHECK(job.at("effect_usage").at("effect.color.curves").at("fallback_effect_time_ns") == 2000000u);
    CHECK(job.at("gpu").at("nvenc_frames") == 100u);
    CHECK(job.at("gpu").at("cuda_composite_frames") == 42u);
    CHECK(job.at("gpu").at("upload_breakdown").at("text_atlas") == 999u);
    CHECK(job.at("prepare").at("asset_preflight_ms") == 4.5);
    CHECK(job.at("image").at("draw_ms") == 6.5);
    CHECK(job.at("image").at("draw_count") == 11u);
    CHECK(job.at("text").at("shaping_ms") == 1.5);
    CHECK(job.at("text").at("atlas_cache_hits") == 13u);
    CHECK(job.at("encoder").at("submit_cpu_ms") == 2.5);
    CHECK(job.at("encoder").at("cuda_pending_peak") == 9u);
    CHECK(job.at("effect_usage").is_object());
    CHECK(keys_of(job) == frozen_job_keys());

    // The derived projections the zero-copy gate reads follow from the typed
    // counters, not from a heuristic on the JSON.
    CHECK(job.at("gpu").at("encoder_backend") == "nvenc");
    CHECK(job.at("gpu").at("effective_backend") == "direct_yuv_cuda");
    CHECK(job.at("gpu").at("decoder_backend") == "software");
}

TEST_CASE("A5 sidecar: an unmeasured job reports null, never a fabricated zero") {
    const JobTimings timings;
    nlohmann::json job = nlohmann::json::object();
    chronon3d::cli::pipe_timing_detail::serialize_job_section(job, timings);

    CHECK(job.at("process_wall_ms").is_null());
    CHECK(job.at("measurement_kind").is_null());
    CHECK(job.at("job_wall_ms").is_null());
    CHECK(job.at("gpu").at("gpu_readback_bytes").is_null());
    CHECK(job.at("gpu").at("gpu_execute_ms").is_null());
    CHECK(job.at("text").at("shaping_ms").is_null());
    CHECK(job.at("encoder").at("submit_cpu_ms").is_null());
    CHECK(job.at("hash").at("output_write_bytes").is_null());

    // Absent-where-it-must-be-a-value fields stay honest defaults, never null:
    // a reader must always be able to compare these against a gate string.
    CHECK(job.at("execution_path") == "unknown");
    CHECK(job.at("surface_handoff_path") == "unknown");
    CHECK(job.at("full_file_reread_count") == 0u);
    CHECK(job.at("gpu").at("decoder_backend") == "software");
    CHECK(job.at("gpu").at("encoder_backend") == "software");
    CHECK(job.at("gpu").at("effective_backend") == "unknown");
    // Zero-copy counters default to a real 0 rather than null: the gate asks
    // whether the number is zero, so it must always be a number. These keys are
    // written twice on purpose — the optional serializer leaves them null and
    // the compatibility projection then publishes the measured-or-zero value.
    CHECK(job.at("gpu").at("vulkan_frames") == 0u);
    CHECK(job.at("gpu").at("cpu_readback_frames") == 0u);
    CHECK(job.at("gpu").at("software_encode_frames") == 0u);
    CHECK(job.at("gpu").at("nvenc_frames") == 0u);
    CHECK(job.at("gpu").at("bitstream_copy_frames") == 0u);
}

TEST_CASE("A5 sidecar: the job section key set is frozen") {
    JobTimings timings;
    nlohmann::json job = nlohmann::json::object();
    chronon3d::cli::pipe_timing_detail::serialize_job_section(job, timings);

    CHECK(keys_of(job.at("hash")) ==
          std::set<std::string>{"output_write_bytes", "output_seek_count",
                                "output_overwrite_bytes",
                                "incremental_hash_invalidations",
                                "final_reread_bytes"});
    CHECK(keys_of(job.at("pipeline")) ==
          std::set<std::string>{"queue_depth_peak", "queue_depth_average",
                                "render_slots_in_flight_peak",
                                "encode_frames_in_flight_peak"});
    CHECK(keys_of(job.at("encoder_settings")) ==
          std::set<std::string>{"preset", "rate_control", "async_depth",
                                "requested_rate_control", "requested_preset"});
    // The gate-readable GPU keys must exist in every build, even one that has
    // never touched a GPU: a missing key would read as "no evidence" while an
    // absent measurement reads as null.
    for (const char* key : {"encoder_backend", "decoder_backend",
                            "effective_backend", "gpu_readback_bytes",
                            "nvenc_frames", "cuda_composite_frames",
                            "upload_breakdown"}) {
        CHECK(job.at("gpu").contains(key));
    }
    CHECK(job.at("gpu").contains("vma_allocation_count"));
    CHECK(job.at("gpu").contains("video_decode_native_surface_frames"));
    CHECK(keys_of(job) == frozen_job_keys());
}

TEST_CASE("timing sidecar preserves frozen job keys when temporal analysis is absent") {
    const JobTimings timings;
    nlohmann::json job = nlohmann::json::object();
    chronon3d::cli::pipe_timing_detail::serialize_job_section(job, timings);
    CHECK_FALSE(job.contains("temporal_rate_analysis"));
    CHECK(keys_of(job) == frozen_job_keys());
}

TEST_CASE("timing sidecar emits optional temporal analysis when supplied") {
    JobTimings timings;
    chronon3d::render_plan::TemporalRateReport report;
    report.skippable_node_fraction = 0.5;
    report.layers.push_back({"opening", chronon3d::render_plan::TemporalRateClass::Cue,
                             {chronon3d::Frame{2}, chronon3d::Frame{3}}, 2, true});
    report.layers.back().properties.push_back({"animation.opacity",
        chronon3d::render_plan::TemporalRateClass::KeyframeInterval,
        {chronon3d::Frame{2}, chronon3d::Frame{3}}, 2, true});
    timings.temporal_rate_analysis = report;
    nlohmann::json job = nlohmann::json::object();
    chronon3d::cli::pipe_timing_detail::serialize_job_section(job, timings);
    CHECK(job.at("temporal_rate_analysis").at("skippable_node_fraction") == 0.5);
    REQUIRE(job.at("temporal_rate_analysis").at("layers").size() == 1);
    const auto& layer = job.at("temporal_rate_analysis").at("layers").at(0);
    CHECK(layer.at("layer_id") == "opening");
    CHECK(layer.at("class") == "cue");
    CHECK(layer.at("eval_frames") == nlohmann::json::array({2, 3}));
    CHECK(layer.at("eval_frame_count") == 2);
    CHECK(layer.at("eval_frames_complete") == true);
    REQUIRE(layer.at("properties").size() == 1);
    CHECK(layer.at("properties").at(0).at("path") == "animation.opacity");
    CHECK(layer.at("properties").at(0).at("eval_frames") == nlohmann::json::array({2, 3}));
    auto expected_job_keys = frozen_job_keys();
    expected_job_keys.insert("temporal_rate_analysis");
    CHECK(keys_of(job) == expected_job_keys);
}

TEST_CASE("timing sidecar v3 effect usage is additive and empty when unmeasured") {
    JobTimings timings;
    timings.effect_usage["effect.color.vignette"] = {7u, 0u, 0u, 0u, 0u, 0u};
    nlohmann::json job = nlohmann::json::object();
    chronon3d::cli::pipe_timing_detail::serialize_job_section(job, timings);
    CHECK(job.at("effect_usage").at("effect.color.vignette").at("native_frames") == 7u);
    CHECK(job.at("effect_usage").at("effect.color.vignette").at("admission_rejected_frames") == 0u);
    CHECK(keys_of(job) == frozen_job_keys());
}

TEST_CASE("A5 sidecar: the summary serializer freezes its key set and its derived rates") {
    const auto ctx = context_with_one_frame(/*wall_time_ms=*/40.0,
                                           /*render_ms=*/20.0);
    JobTimings timings;
    timings.target_fps = 24.0;
    timings.target_fps_num = 24;
    timings.target_fps_den = 1;

    const auto summary = chronon3d::cli::pipe_timing_detail::build_summary_section(
        ctx, {4.0}, timings);

    CHECK(keys_of(summary) == frozen_summary_keys());
    CHECK(keys_of(summary.at("first_frame")) ==
          std::set<std::string>{"frame", "wall_duration_ms", "queue_wait_ms",
                                "render_total_ms", "render", "conversion",
                                "encoder", "image", "text", "cache"});
    CHECK(summary.at("target_fps") == 24.0);
    CHECK(summary.at("target_fps_num") == 24);
    CHECK(summary.at("target_fps_den") == 1);

    // Frame budget is derived from the declared output rate, and the two FPS
    // views are measured over different denominators — reporting one twice
    // would hide exactly the overlap the sidecar exists to expose.
    CHECK(summary.at("frame_budget_ms") == doctest::Approx(1000.0 / 24.0));
    CHECK(summary.at("end_to_end_fps") == doctest::Approx(1000.0 * 1.0 / 40.0));
    CHECK(summary.at("render_loop_fps") == doctest::Approx(1000.0 * 1.0 / 20.0));
    CHECK(summary.at("measured_fps") == summary.at("end_to_end_fps"));
    CHECK(summary.at("realtime_factor") ==
          doctest::Approx((1000.0 / 40.0) / 24.0));

    // With no declared rate there is no budget to exceed — and no fabricated one.
    JobTimings unrated;
    const auto unrated_summary =
        chronon3d::cli::pipe_timing_detail::build_summary_section(
            ctx, {4.0}, unrated);
    CHECK(unrated_summary.at("frame_budget_ms") == 0.0);
    CHECK(unrated_summary.at("realtime_factor") == 0.0);
    CHECK(unrated_summary.at("target_fps").is_null());
    CHECK(keys_of(unrated_summary) == frozen_summary_keys());
}
