#include <doctest/doctest.h>

#include "commands.hpp"
#include "daemon/daemon_render_job_payload.hpp"
#include "daemon/daemon_service_job_admission.hpp"
#include "utils/job/output_lane_pixel_truth.hpp"
#include "utils/job/render_job.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <chronon3d/runtime/job/render_job_request.hpp>
#include <chronon3d/media/video/video_job_execution_context.hpp>

#include <cstdint>
#include <array>
#include <exception>
#include <limits>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace c3d = chronon3d;
namespace cli = chronon3d::cli;

namespace {

constexpr const char* kCompositionId = "RenderJobBuilderFixture";
constexpr std::int64_t kDuration = 120;

c3d::Composition make_fixture_composition() {
    return c3d::Composition{
        c3d::CompositionSpec{
            .name = kCompositionId,
            .width = 320,
            .height = 180,
            .frame_rate = c3d::FrameRate{30, 1},
            .duration = c3d::Frame{kDuration},
        },
        [](const c3d::FrameContext&) { return c3d::Scene{}; },
    };
}

c3d::CompositionRegistry make_registry() {
    c3d::CompositionRegistry registry;
    registry.add(c3d::make_composition_descriptor(c3d::CompositionDescriptor{
        .id = kCompositionId,
        .width = 320,
        .height = 180,
        .fps = c3d::FrameRate{30, 1},
        .duration = c3d::Frame{kDuration}}, [](const c3d::CompositionProps&) {
            return make_fixture_composition();
        }));
    return registry;
}

cli::RenderArgs args_for(std::string frames, std::string output) {
    cli::RenderArgs args;
    args.comp_id = kCompositionId;
    args.frames = std::move(frames);
    args.output = std::move(output);
    args.cpu_budget = c3d::CpuBudget{
        .total_threads = 1,
        .render_threads = 1,
        .decode_threads = 0,
        .encode_threads = 0,
    };
    return args;
}

} // namespace

TEST_CASE("RenderRequest resolves directly into the canonical RenderJob") {
    auto registry = make_registry();
    auto request = cli::make_render_request(
        registry, args_for("60", "hero.png"));

    REQUIRE(request.has_value());
    CHECK(request->mode == c3d::RenderMode::Still);
    CHECK(request->still_frame == c3d::Frame{60});

    auto resolved = cli::resolve_render_request(
        registry, std::move(*request));
    REQUIRE(resolved.has_value());
    CHECK(resolved->mode == c3d::RenderMode::Still);
    CHECK(resolved->still_frame == c3d::Frame{60});
    CHECK(resolved->output == "hero.png");
    CHECK(resolved->registry == &registry);
    REQUIRE(resolved->compiled != nullptr);
    CHECK(resolved->metadata.width == 320);
    CHECK(resolved->metadata.height == 180);
    CHECK(resolved->metadata.duration == c3d::Frame{kDuration});
}

TEST_CASE("RenderJob resolution invokes prepared construction exactly once") {
    auto prepare_calls = std::make_shared<int>(0);
    auto construct_calls = std::make_shared<int>(0);

    c3d::CompositionRegistry registry;
    c3d::CompositionDescriptor descriptor;
    descriptor.id = "PreparedOnce";
    descriptor.prepare_props = [prepare_calls, construct_calls](
        const c3d::CompositionProps&) -> c3d::PreparedCompositionResult {
        ++*prepare_calls;
        c3d::PreparedComposition prepared;
        prepared.metadata = c3d::CompositionMetadata{
            .width = 320,
            .height = 180,
            .fps = c3d::FrameRate{30, 1},
            .duration = c3d::Frame{20},
        };
        prepared.construct = [construct_calls] {
            ++*construct_calls;
            return make_fixture_composition();
        };
        return prepared;
    };
    registry.add(std::move(descriptor));

    auto args = args_for("0", "prepared.png");
    args.comp_id = "PreparedOnce";
    auto job = cli::make_render_job(registry, args);

    REQUIRE(job.has_value());
    CHECK(*prepare_calls == 1);
    CHECK(*construct_calls == 1);
}

TEST_CASE("RenderJob planner selects Sequence and preserves frame step") {
    auto registry = make_registry();
    auto job = cli::make_render_job(
        registry, args_for("0-90x5", "frame_####.png"));

    REQUIRE(job.has_value());
    CHECK(job->mode == c3d::RenderMode::Sequence);
    CHECK(job->first_frame == c3d::Frame{0});
    CHECK(job->last_frame == c3d::Frame{90});
    CHECK(job->frame_step == c3d::Frame{5});
}

TEST_CASE("RenderJob planner selects Video from a case-insensitive extension") {
    auto registry = make_registry();

    SUBCASE("default frame input expands to the full composition") {
        auto job = cli::make_render_job(registry, args_for("0", "hero.MP4"));
        REQUIRE(job.has_value());
        CHECK(job->mode == c3d::RenderMode::Video);
        CHECK(job->first_frame == c3d::Frame{0});
        CHECK(job->last_frame == c3d::Frame{kDuration - 1});
    }

    SUBCASE("an explicit range remains bounded") {
        auto job = cli::make_render_job(registry, args_for("10-30", "hero.webm"));
        REQUIRE(job.has_value());
        CHECK(job->mode == c3d::RenderMode::Video);
        CHECK(job->first_frame == c3d::Frame{10});
        CHECK(job->last_frame == c3d::Frame{30});
    }

    SUBCASE("an explicit non-zero frame creates a one-frame video") {
        auto job = cli::make_render_job(registry, args_for("12", "hero.mov"));
        REQUIRE(job.has_value());
        CHECK(job->mode == c3d::RenderMode::Video);
        CHECK(job->first_frame == c3d::Frame{12});
        CHECK(job->last_frame == c3d::Frame{12});
    }

    SUBCASE("an explicit single-frame chunk at frame 0 stays one frame") {
        // --frames 0 on a video output must mean "exactly frame 0", never
        // the whole plan: the (0,0) sentinel only expands when the range was
        // NOT explicitly requested.
        auto args = args_for("0", "hero.mp4");
        args.frames_explicit = true;
        auto job = cli::make_render_job(registry, args);
        REQUIRE(job.has_value());
        CHECK(job->mode == c3d::RenderMode::Video);
        CHECK(job->first_frame == c3d::Frame{0});
        CHECK(job->last_frame == c3d::Frame{0});
    }
}

TEST_CASE("RenderJob planner preserves canonical video settings") {
    auto registry = make_registry();
    auto args = args_for("0-30", "hero.mp4");
    args.video_settings.fps_num = 60;
    args.video_settings.fps_den = 1;
    args.video_settings.crf = 20;
    args.video_settings.codec = "custom_codec";
    args.video_settings.encode_preset = "medium";
    args.video_settings.tune = "film";
    args.video_settings.keep_frames = true;
    args.video_settings.frames_dir = "custom_frames";
    args.video_settings.chunks = 4;
    args.video_settings.hardware_encoder = "none";
    args.video_settings.ffmpeg_mode = "png";
    args.video_settings.ffmpeg_verbose = true;
    args.video_settings.pipe_pixfmt = "nv12";
    args.pipe_pixfmt_explicit = true;
    args.video_settings.color_output = "rec709";
    args.video_settings.pipe_writer = "classic";
    args.video_settings.encoder_backend = "pipe";
    args.video_settings.dry_run = true;

    auto job = cli::make_render_job(registry, args);

    REQUIRE(job.has_value());
    REQUIRE(job->mode == c3d::RenderMode::Video);
    CHECK(job->video_settings.fps_num == 60);
    CHECK(job->video_settings.fps_den == 1);
    CHECK(job->video_settings.crf == 20);
    CHECK(job->video_settings.codec == "custom_codec");
    CHECK(job->video_settings.encode_preset == "medium");
    CHECK(job->video_settings.tune == "film");
    CHECK(job->video_settings.keep_frames);
    CHECK(job->video_settings.frames_dir == "custom_frames");
    CHECK(job->video_settings.chunks == 4);
    CHECK(job->video_settings.ffmpeg_mode == "png");
    CHECK(job->video_settings.ffmpeg_verbose);
    CHECK(job->video_settings.pipe_pixfmt == "nv12");
    CHECK(job->video_settings.color_output == "rec709");
    CHECK(job->video_settings.pipe_writer == "classic");
    CHECK(job->video_settings.encoder_backend == "pipe");
    CHECK(job->video_settings.dry_run);
    CHECK(job->execution.warmup_renderer);
    CHECK(job->execution.warmup_dummy_frame);
}

TEST_CASE("RenderJob planner keeps non-video uppercase extensions on image modes") {
    auto registry = make_registry();
    auto job = cli::make_render_job(registry, args_for("8", "hero.PNG"));

    REQUIRE(job.has_value());
    CHECK(job->mode == c3d::RenderMode::Still);
    CHECK(job->still_frame == c3d::Frame{8});
}

// ═══════════════════════════════════════════════════════════════════════════
// The image lane declares itself non-canonical for pixel truth.
//
// TICKET-TEXT-QUALITY-V2 (P0-0b): `render -o x.png` is not a measurement of what
// the renderer composed, and the pre-write sanity scan cannot detect that (its
// contract is NaN/Inf only, pinned by test_render_job_write_frame_sanity.cpp).
// The CLI therefore has to SAY SO at the lane decision instead of reporting a
// silent success. These cases pin the rule (one lane is a truth surface) and
// the text (it names the replacement and the ticket) — not the free-form
// wording a log reader happens to see.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("the video lane is the only pixel-truth output lane") {
    CHECK(cli::pixel_truth::lane_is_pixel_truth_surface(c3d::RenderMode::Video));
    CHECK_FALSE(cli::pixel_truth::lane_is_pixel_truth_surface(c3d::RenderMode::Still));
    CHECK_FALSE(cli::pixel_truth::lane_is_pixel_truth_surface(c3d::RenderMode::Sequence));
}

TEST_CASE("the image lane notice names the replacement surface and the ticket") {
    CHECK(cli::pixel_truth::lane_notice(c3d::RenderMode::Video).empty());

    const auto still = cli::pixel_truth::lane_notice(c3d::RenderMode::Still);
    const auto sequence = cli::pixel_truth::lane_notice(c3d::RenderMode::Sequence);
    // One notice for the whole image lane: a still and a PNG sequence are the
    // same surface, so the two cannot drift apart.
    CHECK(still == sequence);
    CHECK_FALSE(still.empty());
    CHECK(still.find("--video-sink raw") != std::string_view::npos);
    CHECK(still.find("TICKET-TEXT-QUALITY-V2") != std::string_view::npos);
}

TEST_CASE("the planner's own lane choice is what the notice is keyed on") {
    auto registry = make_registry();

    auto still = cli::make_render_job(registry, args_for("8", "hero.PNG"));
    REQUIRE(still.has_value());
    CHECK_FALSE(
        cli::pixel_truth::lane_notice(still->mode).empty());

    auto sequence = cli::make_render_job(
        registry, args_for("0-90x5", "frame_####.png"));
    REQUIRE(sequence.has_value());
    CHECK_FALSE(
        cli::pixel_truth::lane_notice(sequence->mode).empty());

    auto video = cli::make_render_job(registry, args_for("0-90", "hero.mp4"));
    REQUIRE(video.has_value());
    CHECK(cli::pixel_truth::lane_notice(video->mode).empty());
}

TEST_CASE("RenderJob planner fails cleanly for an unknown composition") {
    auto registry = make_registry();
    auto args = args_for("0", "missing.png");
    args.comp_id = "MissingComposition";

    CHECK_FALSE(cli::make_render_job(registry, args).has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 9 — CLI/IPC parity gate.
//
// One job, two canonical entry points, ONE normalization. The CLI boundary
// builds a RenderJobRequest from flags; the IPC boundary decodes the daemon
// wire JSON with decode_render_job_payload() (the single canonical mapper) and
// builds a RenderJobRequest. Both must normalize to the SAME NormalizedRenderJob
// — proven by render_job_fingerprint() equality over the full semantic matrix,
// not by comparing JSON strings.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

using JobRequest = c3d::runtime::job::RenderJobRequest;
using JobCaps = c3d::runtime::job::RuntimeCapabilities;
using c3d::runtime::job::FrameRange;

// Mirror of the CLI entry mapping (register_render_commands_body.inc, the
// `render --plan` route): the renderer is requested through explicit backend /
// hardware / encoder / hot-path strings and requirements.gpu_required is
// derived from the hardware selection exactly as the CLI does.
JobRequest cli_request_for(
    const std::string& backend, const std::string& hardware,
    const std::string& encoder_backend, const std::string& hot_path,
    bool report = false) {
    JobRequest request;
    request.plan_path = "plan.json";
    request.output_path = "hero.mp4";
    request.assets_root = "assets";
    request.backend = backend;
    request.report = report;
    request.output.hardware_encoder = hardware;
    request.output.encoder_backend = encoder_backend;
    request.output.gpu_hot_path_mode = hot_path;
    // Mirror of the CLI entry mapping (register_render_commands.cpp): a
    // strict hot-path request is a GPU-native contract and forbids CPU
    // fallback (TICKET-CLI-STRICT-GPU-NATIVE-MAPPING), so the semantic
    // requirements derive from the hot-path selection as well as the
    // hardware spelling.
    const bool strict_hot_path = hot_path == "require_gpu_native" ||
        hot_path == "require_direct_yuv";
    request.requirements.gpu_required = (hardware == "nvenc") || strict_hot_path;
    request.requirements.cpu_fallback_allowed = !strict_hot_path;
    return request;
}

// Canonical wire-equality check between the decoded IPC request and the CLI
// request for the fields both transports spell identically on the wire. The
// hardware/encoder/hot-path strings are NOT compared here: the CLI spells
// them explicitly while the IPC payload carries only semantic requirements,
// so equality of the RESOLVED job is proven by the normalized fingerprint
// (normalize_render_job back-fills the resolved decision on both sides).
void check_same_semantics(const JobRequest& cli, const JobRequest& ipc) {
    CHECK(ipc.plan_path == cli.plan_path);
    CHECK(ipc.output_path == cli.output_path);
    CHECK(ipc.assets_root == cli.assets_root);
    // The IPC wire may omit `backend` (the daemon resolves it against its own
    // capability, so it decodes to "auto"), while the CLI boundary spells the
    // resolved value explicitly. Raw spelling is transport-specific and NOT
    // part of the parity contract: the resolved backend is asserted by the
    // normalized fingerprint below. Only a payload-authored backend must
    // agree verbatim.
    const bool backend_matches = ipc.backend == cli.backend;
    const bool backend_is_auto = ipc.backend == "auto";
    const bool backend_ok = backend_matches || backend_is_auto;
    CHECK(backend_ok);
    CHECK(ipc.report == cli.report);
    CHECK(ipc.frame_range.has_value() == cli.frame_range.has_value());
    if (ipc.frame_range && cli.frame_range) {
        CHECK(ipc.frame_range->first == cli.frame_range->first);
        CHECK(ipc.frame_range->last == cli.frame_range->last);
    }
    CHECK(ipc.requirements.gpu_required == cli.requirements.gpu_required);
    CHECK(ipc.output.codec == cli.output.codec);
    CHECK(ipc.output.encode_preset == cli.output.encode_preset);
    CHECK(ipc.output.rate_control_mode == cli.output.rate_control_mode);
    CHECK(ipc.output.crf == cli.output.crf);
    CHECK(ipc.output.qp == cli.output.qp);
    CHECK(ipc.output.bitrate == cli.output.bitrate);
    CHECK(ipc.output.audio_source_path == cli.output.audio_source_path);
    CHECK(ipc.output.gop_copy_only == cli.output.gop_copy_only);
    REQUIRE(ipc.output.changed_intervals.size() ==
            cli.output.changed_intervals.size());
    for (std::size_t index = 0; index < cli.output.changed_intervals.size(); ++index) {
        CHECK(ipc.output.changed_intervals[index].start_seconds ==
              cli.output.changed_intervals[index].start_seconds);
        CHECK(ipc.output.changed_intervals[index].end_seconds ==
              cli.output.changed_intervals[index].end_seconds);
        CHECK(ipc.output.changed_intervals[index].kind ==
              cli.output.changed_intervals[index].kind);
    }
    CHECK(ipc.output.fps_num == cli.output.fps_num);
    CHECK(ipc.output.fps_den == cli.output.fps_den);
    CHECK(ipc.trace.output.string() == cli.trace.output.string());
    CHECK(ipc.trace.level == cli.trace.level);
    CHECK(ipc.receipt_verification == cli.receipt_verification);
    CHECK(ipc.output.ffmpeg_mode == cli.output.ffmpeg_mode);
}

void check_normalized_parity(const JobRequest& cli, const std::string& payload,
                             const JobCaps& capabilities) {
    INFO("IPC payload: " << payload);
    const auto decoded = cli::decode_render_job_payload(payload);
    check_same_semantics(cli, decoded.request);

    const auto cli_resolved =
        c3d::runtime::job::normalize_render_job(cli, capabilities);
    const auto ipc_resolved =
        c3d::runtime::job::normalize_render_job(decoded.request, capabilities);
    REQUIRE(cli_resolved.has_value());
    REQUIRE(ipc_resolved.has_value());
    const auto& cli_job = cli_resolved.value();
    const auto& ipc_job = ipc_resolved.value();
    const auto cli_fp = c3d::runtime::job::render_job_fingerprint(cli_job);
    const auto ipc_fp = c3d::runtime::job::render_job_fingerprint(ipc_job);
    if (cli_fp != ipc_fp) {
        FAIL("CLI/IPC normalized fingerprints differ\n  CLI: " << cli_fp
             << "\n  IPC: " << ipc_fp);
    }
}

} // namespace

TEST_CASE("CLI and IPC boundary map identical semantics to one normalized job") {
    const JobCaps auto_host{"auto"};
    const JobCaps vulkan_host{"vulkan"};

    // Whole-plan default (no range): both transports render the whole plan.
    SUBCASE("whole plan, software host") {
        check_normalized_parity(cli_request_for("auto", "none", "pipe", "auto"),
            R"({"plan_path":"plan.json","assets_root":"assets","output":"hero.mp4","report":false,"range_enabled":false,"first_frame":0,"last_frame":0,"execution_requirements":{"gpu_required":false,"cpu_fallback_allowed":true,"composition_required":false,"video_source_required":true,"packet_copy_allowed":true},"output_spec":{}})",
            auto_host);
    }

    SUBCASE("explicit single-frame chunk at frame 0 stays one frame") {
        JobRequest cli = cli_request_for("auto", "none", "pipe", "auto");
        cli.frame_range = FrameRange{0, 0};
        check_normalized_parity(cli,
            R"({"plan_path":"plan.json","assets_root":"assets","output":"hero.mp4","range_enabled":true,"first_frame":0,"last_frame":0,"execution_requirements":{"gpu_required":false,"cpu_fallback_allowed":true,"composition_required":false,"video_source_required":true,"packet_copy_allowed":true},"output_spec":{}})",
            auto_host);
    }

    SUBCASE("chunk range + rational fps override") {
        JobRequest cli = cli_request_for("auto", "none", "pipe", "auto");
        cli.frame_range = FrameRange{40, 119};
        cli.output.fps_num = 30000;
        cli.output.fps_den = 1001;
        check_normalized_parity(cli,
            R"({"plan_path":"plan.json","assets_root":"assets","output":"hero.mp4","range_enabled":true,"first_frame":40,"last_frame":119,"execution_requirements":{"gpu_required":false,"cpu_fallback_allowed":true,"composition_required":false,"video_source_required":true,"packet_copy_allowed":true},"output_spec":{"fps_num":30000,"fps_den":1001}})",
            auto_host);
    }

    SUBCASE("source audio mux + gop copy intent") {
        JobRequest cli = cli_request_for("auto", "none", "pipe", "auto");
        cli.output.audio_source_path = "assets/master.wav";
        cli.output.gop_copy_only = true;
        check_normalized_parity(cli,
            R"({"plan_path":"plan.json","assets_root":"assets","output":"hero.mp4","audio_source_path":"assets/master.wav","gop_copy_only":true,"execution_requirements":{"gpu_required":false,"cpu_fallback_allowed":true,"composition_required":false,"video_source_required":true,"packet_copy_allowed":true},"output_spec":{}})",
            auto_host);
    }

    SUBCASE("software authored composition (pipe encoder)") {
        // CLI adapter: --backend vulkan --encoder-backend pipe
        // --gpu-hot-path-mode auto for a composition without source video.
        JobRequest cli = cli_request_for("vulkan", "none", "pipe", "auto");
        cli.requirements.composition_required = true;
        check_normalized_parity(cli,
            R"({"plan_path":"plan.json","assets_root":"assets","output":"hero.mp4","execution_requirements":{"gpu_required":false,"cpu_fallback_allowed":true,"composition_required":true,"video_source_required":false,"packet_copy_allowed":true},"output_spec":{"encoder_backend":"pipe"}})",
            vulkan_host);
    }

    // The GPU-semantic subcases below assert the VIDEO resolver's canonical
    // back-fill (nvenc/native/auto from gpu_required + codec).  The semantic
    // composition request leaves the DirectYUV/FullGraph choice to Chronon.
    // That decision exists only where CHRONON3D_ENABLE_VIDEO is compiled; in
    // video-off matrix builds normalize_render_job deliberately keeps the
    // non-video defaults (none/pipe/auto) and cannot reproduce an explicitly
    // authored nvenc request from semantic requirements alone. Transport/
    // range/output/trace parity (the subcases above and below the gate) stays
    // live in every build; the GPU-resolution axis is exercised where the
    // canonical resolver it pins is compiled.
    //
    // Wire fidelity: the resolved encoder selection is carried EXPLICITLY by
    // both clients, from one authority (RenderingGen's
    // resolveNativeEncodeSelection): the subprocess adapter as
    // --hardware/--encoder-backend/--gpu-hot-path-mode, the IPC adapter stamped
    // into output_spec by outputSpecForTransport. normalize_render_job only
    // back-fills these axes when the wire OMITS them, so a payload that omits
    // them models a client that does not exist and diverges from the CLI
    // whenever the resolution is not the default. The payloads below therefore
    // carry the same selection the CLI spells.
#if defined(CHRONON3D_ENABLE_VIDEO)
    SUBCASE("NVENC source-video composition on a Vulkan host") {
        // CLI spells nvenc/native/auto; the IPC adapter stamps the same
        // selection from the same authority, so the payload carries it.
        JobRequest cli = cli_request_for("vulkan", "nvenc", "native",
                                         "auto", /*report=*/true);
        cli.output.codec = "h264";
        cli.output.encode_preset = "p3";
        cli.receipt_verification = "normal";
        check_normalized_parity(cli,
            R"({"plan_path":"plan.json","assets_root":"assets","output":"hero.mp4","report":true,"encode_preset":"p3","receipt_verify":"normal","execution_requirements":{"gpu_required":true,"cpu_fallback_allowed":true,"composition_required":true,"video_source_required":true,"packet_copy_allowed":true},"output_spec":{"codec":"h264","hardware_encoder":"nvenc","encoder_backend":"native","gpu_hot_path_mode":"auto"}})",
            vulkan_host);
    }

    SUBCASE("pure-video GPU job carries its resolved hot path to DirectYUV") {
        // The exact hot-path spelling travels on both transports (the CLI as
        // --gpu-hot-path-mode, the IPC adapter inside output_spec), so an
        // explicit DirectYUV request reaches the resolver identically.
        // NOTE: no RenderingGen client can currently PRODUCE this value —
        // resolveNativeEncodeSelection only emits auto/require_gpu_native — so
        // this pins the transport contract, not the adapter's reachable set.
        JobRequest cli = cli_request_for("vulkan", "nvenc", "native",
                                         "require_direct_yuv");
        cli.output.codec = "h264";
        check_normalized_parity(cli,
            R"({"plan_path":"plan.json","assets_root":"assets","output":"hero.mp4","execution_requirements":{"gpu_required":true,"cpu_fallback_allowed":true,"composition_required":false,"video_source_required":true,"packet_copy_allowed":true},"output_spec":{"codec":"h264","hardware_encoder":"nvenc","encoder_backend":"native","gpu_hot_path_mode":"require_direct_yuv"}})",
            vulkan_host);
    }

    SUBCASE("GPU-authored composition without source video falls to pipe") {
        // CLI adapter mirror (render_args.go): composition + no source video
        // can never feed the NVENC native surface, so the encoder is the pipe
        // with automatic hot-path selection. Both transports carry that
        // selection explicitly.
        JobRequest cli = cli_request_for("vulkan", "nvenc", "pipe", "auto");
        cli.output.codec = "h264";
        check_normalized_parity(cli,
            R"({"plan_path":"plan.json","assets_root":"assets","output":"hero.mp4","execution_requirements":{"gpu_required":true,"cpu_fallback_allowed":true,"composition_required":true,"video_source_required":false,"packet_copy_allowed":true},"output_spec":{"codec":"h264","hardware_encoder":"nvenc","encoder_backend":"pipe","gpu_hot_path_mode":"auto"}})",
            vulkan_host);
    }

    SUBCASE("rate control + codec carried through the wire exactly once") {
        JobRequest cli = cli_request_for("vulkan", "nvenc", "native",
                                         "auto");
        cli.output.codec = "hevc";
        cli.output.rate_control_mode = "crf";
        cli.output.crf = 18;
        check_normalized_parity(cli,
            R"({"plan_path":"plan.json","assets_root":"assets","output":"hero.mp4","execution_requirements":{"gpu_required":true,"cpu_fallback_allowed":true,"composition_required":true,"video_source_required":true,"packet_copy_allowed":true},"output_spec":{"codec":"hevc","rate_control_mode":"crf","crf":18,"hardware_encoder":"nvenc","encoder_backend":"native","gpu_hot_path_mode":"auto"}})",
            vulkan_host);
    }
#endif  // CHRONON3D_ENABLE_VIDEO

    SUBCASE("trace + explicit verification policy survive the daemon") {
        JobRequest cli = cli_request_for("auto", "none", "pipe", "auto");
        cli.report = true;
        cli.trace.output = "render.pftrace";
        cli.trace.level = "nodes";
        cli.receipt_verification = "certify";
        check_normalized_parity(cli,
            R"({"plan_path":"plan.json","assets_root":"assets","output":"hero.mp4","report":true,"trace_output":"render.pftrace","trace_level":"nodes","receipt_verify":"certify","execution_requirements":{"gpu_required":false,"cpu_fallback_allowed":true,"composition_required":false,"video_source_required":true,"packet_copy_allowed":true},"output_spec":{}})",
            auto_host);
    }

    SUBCASE("top-level legacy keys keep decoding (pre-Phase-9 clients)") {
        JobRequest cli = cli_request_for("vulkan", "nvenc", "native",
                                         "require_gpu_native");
        cli.output.codec = "h264";
        cli.output.ffmpeg_mode = "native";
        cli.output.encode_preset = "p2";
        cli.receipt_verification = "fast";
        check_normalized_parity(cli,
            R"({"plan_path":"plan.json","assets_root":"assets","output":"hero.mp4","backend":"vulkan","codec":"h264","hardware_encoder":"nvenc","encoder_backend":"native","ffmpeg_mode":"native","gpu_hot_path_mode":"require_gpu_native","encode_preset":"p2","receipt_verify":"fast","execution_requirements":{"gpu_required":true,"cpu_fallback_allowed":true,"composition_required":true,"video_source_required":true,"packet_copy_allowed":true},"output_spec":{}})",
            vulkan_host);
    }
}

TEST_CASE("strict hot-path derives the canonical native lane unless explicit") {
    const JobCaps vulkan_host{"vulkan"};

    // Bare strict request (no --hardware/--encoder-backend): the semantic
    // requirements carry the strict contract and BOTH transports derive the
    // canonical native lane from resolve_canonical_execution_parameters
    // (TICKET-CLI-STRICT-GPU-NATIVE-MAPPING).
    JobRequest cli;
    cli.plan_path = "plan.json";
    cli.output_path = "hero.mp4";
    cli.assets_root = "assets";
    cli.backend = "vulkan";
    cli.output.gpu_hot_path_mode = "require_gpu_native";
    // Model the CLI request after it has elided implicit VideoSettings
    // defaults: normalizer defaults must derive from semantic requirements.
    cli.output.hardware_encoder.clear();
    cli.output.encoder_backend.clear();
    cli.requirements.gpu_required = true;
    cli.requirements.cpu_fallback_allowed = false;
    check_normalized_parity(cli,
        R"({"plan_path":"plan.json","assets_root":"assets","output":"hero.mp4","execution_requirements":{"gpu_required":true,"cpu_fallback_allowed":false,"composition_required":true,"video_source_required":true,"packet_copy_allowed":true},"output_spec":{"gpu_hot_path_mode":"require_gpu_native"}})",
        vulkan_host);

    const auto derived =
        c3d::runtime::job::normalize_render_job(cli, vulkan_host);
    REQUIRE(derived.has_value());
#if defined(CHRONON3D_ENABLE_VIDEO)
    CHECK(derived->output.hardware_encoder == "nvenc");
    CHECK(derived->output.encoder_backend == "native");
#else
    // Video-disabled matrix builds preserve semantic strictness but cannot
    // resolve a hardware encoder lane.
    CHECK(derived->output.hardware_encoder == "none");
    CHECK(derived->output.encoder_backend == "pipe");
#endif

    // An explicit transport selection travels and wins over the derivation.
    JobRequest explicit_pipe = cli;
    explicit_pipe.output.encoder_backend = "pipe";
    const auto pinned =
        c3d::runtime::job::normalize_render_job(explicit_pipe, vulkan_host);
    REQUIRE(pinned.has_value());
    CHECK(pinned->output.encoder_backend == "pipe");
}

TEST_CASE("Vulkan host accepts an explicit per-job software fallback") {
    const JobCaps vulkan_host{"vulkan"};
    auto request = cli_request_for("software", "none", "pipe", "auto");
    request.requirements.gpu_required = false;
    request.requirements.cpu_fallback_allowed = true;
    request.requirements.composition_required = true;
    request.requirements.video_source_required = true;

    const auto normalized =
        c3d::runtime::job::normalize_render_job(request, vulkan_host);
    REQUIRE(normalized.has_value());
    CHECK(normalized->backend == "software");
    CHECK(normalized->output.encoder_backend == "pipe");
}

TEST_CASE("render_job_fingerprint discriminates semantic axes") {
    const JobCaps vulkan_host{"vulkan"};
    JobRequest base = cli_request_for("vulkan", "nvenc", "native",
                                      "require_gpu_native");
    base.requirements.composition_required = true;
    base.requirements.video_source_required = true;
    const auto base_job =
        c3d::runtime::job::normalize_render_job(base, vulkan_host);
    REQUIRE(base_job.has_value());
    const auto base_fp =
        c3d::runtime::job::render_job_fingerprint(base_job.value());

    SUBCASE("frame range changes the fingerprint") {
        JobRequest ranged = base;
        ranged.frame_range = FrameRange{12, 34};
        const auto job =
            c3d::runtime::job::normalize_render_job(ranged, vulkan_host);
        REQUIRE(job.has_value());
        CHECK(c3d::runtime::job::render_job_fingerprint(job.value()) !=
              base_fp);
    }

    SUBCASE("rate control changes the fingerprint") {
        JobRequest rc = base;
        rc.output.rate_control_mode = "crf";
        rc.output.crf = 18;
        const auto job =
            c3d::runtime::job::normalize_render_job(rc, vulkan_host);
        REQUIRE(job.has_value());
        CHECK(c3d::runtime::job::render_job_fingerprint(job.value()) !=
              base_fp);
    }

    SUBCASE("receipt policy changes the fingerprint") {
        JobRequest with_receipt = base;
        with_receipt.receipt_verification = "certify";
        const auto job =
            c3d::runtime::job::normalize_render_job(with_receipt, vulkan_host);
        REQUIRE(job.has_value());
        CHECK(c3d::runtime::job::render_job_fingerprint(job.value()) !=
              base_fp);
    }

    SUBCASE("trace request changes the fingerprint") {
        JobRequest traced = base;
        traced.trace.output = "render.pftrace";
        traced.trace.level = "nodes";
        const auto job =
            c3d::runtime::job::normalize_render_job(traced, vulkan_host);
        REQUIRE(job.has_value());
        CHECK(c3d::runtime::job::render_job_fingerprint(job.value()) !=
              base_fp);
    }

    SUBCASE("source audio changes the fingerprint") {
        JobRequest with_audio = base;
        with_audio.output.audio_source_path = "assets/master.wav";
        const auto job =
            c3d::runtime::job::normalize_render_job(with_audio, vulkan_host);
        REQUIRE(job.has_value());
        CHECK(c3d::runtime::job::render_job_fingerprint(job.value()) !=
              base_fp);
    }

    SUBCASE("identical requests produce identical fingerprints") {
        const auto twin =
            c3d::runtime::job::normalize_render_job(base, vulkan_host);
        REQUIRE(twin.has_value());
        CHECK(c3d::runtime::job::render_job_fingerprint(twin.value()) ==
              base_fp);
    }

    SUBCASE("changed content interval changes the fingerprint") {
        JobRequest changed = base;
        changed.output.changed_intervals = {
            {.start_seconds = 1.0,
             .end_seconds = 1.25,
             .kind = c3d::media::ChangedIntervalKind::Content}};
        const auto job =
            c3d::runtime::job::normalize_render_job(changed, vulkan_host);
        REQUIRE(job.has_value());
        CHECK(c3d::runtime::job::render_job_fingerprint(job.value()) !=
              base_fp);
    }
}

TEST_CASE("decode_render_job_payload maps changed intervals") {
    const auto request = cli::decode_render_job_payload(R"({
        "plan_path":"plan.json",
        "output":"hero.mp4",
        "output_spec":{
            "changed_intervals":[
                {"start_seconds":1.0,"end_seconds":2.0,"kind":"content"},
                {"start_seconds":8.0,"end_seconds":9.0,"kind":"trim"}
            ]
        }
    })");
    REQUIRE(request.request.output.changed_intervals.size() == 2);
    CHECK(request.request.output.changed_intervals[0].kind ==
          c3d::media::ChangedIntervalKind::Content);
    CHECK(request.request.output.changed_intervals[1].kind ==
          c3d::media::ChangedIntervalKind::Trim);
}

// ═══════════════════════════════════════════════════════════════════════════
// Closed payload vocabulary (RENDER_JOB ingress).
//
// The plan-file ingress is validated against a schema with
// `additionalProperties: false`; the IPC payload used to be the tolerant one,
// so a typo was refused in one boundary and silently defaulted in the other —
// the job then ran with a value the caller never asked for. These two cases pin
// both halves: the whole documented vocabulary still decodes, and a key outside
// it is refused by name at every scope.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("decode_render_job_payload accepts the whole documented vocabulary") {
    const auto request = cli::decode_render_job_payload(R"({
        "job_id":"full-video-job-42",
        "plan_path":"plan.json",
        "prepared_package_path":"package.json",
        "output":"hero.mp4",
        "assets_root":"/assets",
        "report":true,
        "trace_output":"hero.pftrace",
        "trace_level":"pipeline",
        "receipt_verify":"strict",
        "range_enabled":true,
        "first_frame":0,
        "last_frame":0,
        "codec":"h264",
        "hardware_encoder":"nvenc",
        "hardware":"nvenc",
        "encoder_backend":"native",
        "ffmpeg_mode":"pipe",
        "gpu_hot_path_mode":"auto",
        "encode_preset":"p4",
        "rate_control_mode":"crf",
        "crf":20,
        "qp":20,
        "bitrate":8000000,
        "audio_source_path":"hero.m4a",
        "audio_target_sample_rate":48000,
        "gop_copy_only":false,
        "changed_intervals":[
            {"start_seconds":1.0,"end_seconds":2.0,"kind":"content"}
        ],
        "pipe_pixfmt":"nv12",
        "fps_num":24,
        "fps_den":1,
        "backend":"vulkan",
        "execution_requirements":{
            "backend":"vulkan",
            "gpu_required":true,
            "cpu_fallback_allowed":false,
            "composition_required":true,
            "video_source_required":true,
            "packet_copy_allowed":true
        },
        "output_spec":{
            "codec":"h264",
            "width":1920,
            "height":1080,
            "fps_num":24,
            "fps_den":1,
            "pipe_pixfmt":"nv12",
            "hardware_encoder":"nvenc",
            "encoder_backend":"native",
            "gpu_hot_path_mode":"auto",
            "encode_preset":"p4",
            "rate_control_mode":"crf",
            "crf":20,
            "qp":0,
            "bitrate":8000000,
            "audio_target_sample_rate":48000,
            "ffmpeg_mode":"pipe",
            "changed_intervals":[
                {"start_seconds":1.0,"end_seconds":2.0,"kind":"content"}
            ]
        }
    })");

    CHECK(request.request.plan_path == "plan.json");
    CHECK(request.job_id == "full-video-job-42");
    CHECK(request.request.output_path == "hero.mp4");
    CHECK(request.request.output.hardware_encoder == "nvenc");
    CHECK(request.request.output.gop_copy_only == false);
    REQUIRE(request.request.output.changed_intervals.size() == 1);
    CHECK(request.request.requirements.gpu_required);
    CHECK_FALSE(request.request.requirements.cpu_fallback_allowed);
}

TEST_CASE("decode_render_job_payload refuses a key outside the closed vocabulary") {
    // Root scope: the classic silent typo. Before this check the key was
    // ignored and the daemon resolved its own default for the field.
    CHECK_THROWS_AS(
        cli::decode_render_job_payload(
            R"({"plan_path":"plan.json","hardware_encder":"nvenc"})"),
        std::exception);
    CHECK_THROWS_AS(
        cli::decode_render_job_payload(
            R"({"job_id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","plan_path":"plan.json"})"),
        std::exception);
    CHECK_THROWS_AS(
        cli::decode_render_job_payload(
            R"({"plan_path":"plan.json","gop_copyonly":true})"),
        std::exception);

    // Nested scopes carry the same contract.
    CHECK_THROWS_AS(
        cli::decode_render_job_payload(
            R"({"plan_path":"plan.json","output_spec":{"codek":"h264"}})"),
        std::exception);
    CHECK_THROWS_AS(
        cli::decode_render_job_payload(
            R"({"plan_path":"plan.json","execution_requirements":{"gpu_requred":true}})"),
        std::exception);
    CHECK_THROWS_AS(
        cli::decode_render_job_payload(
            R"({"plan_path":"plan.json","output_spec":{"changed_intervals":[{"start_secnds":1.0}]}})"),
        std::exception);

    // A present-but-mistyped container is an error, not "no output spec":
    // `is_object()` used to swallow it and the job ran on defaults.
    CHECK_THROWS_AS(
        cli::decode_render_job_payload(
            R"({"plan_path":"plan.json","output_spec":"nope"})"),
        std::exception);
    CHECK_THROWS_AS(
        cli::decode_render_job_payload(
            R"({"plan_path":"plan.json","execution_requirements":"nope"})"),
        std::exception);
}

TEST_CASE("RenderJobAdmissionContext releases lock and reservation on scope exit") {
    c3d::runtime::DeviceScheduler scheduler;
    scheduler.register_device(
        c3d::runtime::DeviceCapabilities{.name = "admission-fixture"},
        c3d::runtime::DeviceResourceVector{
            .compute_units = 1.0f,
            .vram_bytes = 4096,
            .pcie_bandwidth = 1.0f});
    const c3d::runtime::DeviceSelectionRequirements requirements{
        .resources = c3d::runtime::DeviceResourceVector{
            .compute_units = 0.25f,
            .vram_bytes = 512,
            .pcie_bandwidth = 0.25f}};
    std::shared_mutex render_mutex;
    std::mutex admission_mutex;
    std::condition_variable admission_cv;
    std::atomic<bool> running{true};

    bool dispatcher_entered = false;
    std::shared_ptr<c3d::media::VideoJobExecutionContext> video_execution;
    CHECK_THROWS_AS(([&]() {
        cli::RenderJobAdmissionContext context(
            false, 512, render_mutex, admission_mutex, admission_cv,
            running, scheduler, requirements);
        REQUIRE(context.admitted());
        dispatcher_entered = true;
        const auto state = scheduler.resource_state(0);
        REQUIRE(state.has_value());
        CHECK(state->reserved.vram_bytes == 512);
        video_execution =
            std::make_shared<c3d::media::VideoJobExecutionContext>();
        video_execution->device_scheduler = &scheduler;
        video_execution->reservation.emplace(context.take_reservation());
        CHECK_FALSE(context.take_reservation().valid());
        const auto test_dispatcher = [](
            std::shared_ptr<c3d::media::VideoJobExecutionContext> execution) -> void {
            REQUIRE(execution != nullptr);
            REQUIRE(execution->reservation.has_value());
            CHECK(execution->reservation->valid());
            throw std::runtime_error("dispatcher fixture failure");
        };
        test_dispatcher(std::move(video_execution));
    }()), std::runtime_error);
    CHECK(dispatcher_entered);
    CHECK(video_execution == nullptr);

    const auto released = scheduler.resource_state(0);
    REQUIRE(released.has_value());
    CHECK(released->reserved.vram_bytes == 0);
    CHECK(released->reserved.compute_units == doctest::Approx(0.0f));

    // The execution lock was also released; a second context can acquire it.
    {
        cli::RenderJobAdmissionContext second(
            false, 512, render_mutex, admission_mutex, admission_cv,
            running, scheduler, requirements);
        CHECK(second.admitted());
    }
}

TEST_CASE("decode_render_job_payload rejects malformed wire JSON") {
    CHECK_THROWS_AS(cli::decode_render_job_payload("{not json"),
                    std::exception);
    CHECK_THROWS_AS(
        cli::decode_render_job_payload(
            R"({"plan_path":"plan.json","execution_requirements":"nope","output_spec":{}})"),
        std::exception);
}

TEST_CASE("normalize_render_job validates the pipe pixel format against the canonical vocabulary") {
    const JobCaps auto_host{"auto"};

    // Unknown name: rejected at the boundary. Before this validation the value
    // travelled to the runtime parser, which mapped anything unrecognised onto
    // RGBA — a formally successful export in the wrong pixel format.
    for (const char* bad : {"nv21", "nv12 ", "yuv420", "banana"}) {
        JobRequest request = cli_request_for("auto", "none", "pipe", "auto");
        request.output.pipe_pixfmt = bad;
        const auto normalized =
            c3d::runtime::job::normalize_render_job(request, auto_host);
        INFO("rejected value: '" << bad << "'");
        REQUIRE_FALSE(normalized.has_value());
        CHECK(normalized.error().code ==
              c3d::runtime::job::RenderJobRequestErrorCode::InvalidPipePixelFormat);
    }

    // Every accepted spelling, including the empty "no explicit request" case.
    for (const char* good : {"", "rgba", "yuv420p", "nv12", "p010", "p010le"}) {
        JobRequest request = cli_request_for("auto", "none", "pipe", "auto");
        request.output.pipe_pixfmt = good;
        INFO("accepted value: '" << good << "'");
        const auto normalized =
            c3d::runtime::job::normalize_render_job(request, auto_host);
        REQUIRE(normalized.has_value());
        CHECK(normalized.value().output.pipe_pixfmt == good);
    }
}

TEST_CASE("normalize_render_job rejects invalid changed intervals") {
    const JobCaps auto_host{"auto"};
    for (const auto interval : std::array<c3d::media::ChangedInterval, 3>{
             c3d::media::ChangedInterval{.start_seconds = -1.0,
                                          .end_seconds = 1.0},
             c3d::media::ChangedInterval{.start_seconds = 2.0,
                                          .end_seconds = 2.0},
             c3d::media::ChangedInterval{.start_seconds = 0.0,
                                          .end_seconds = std::numeric_limits<double>::infinity()}}) {
        JobRequest request = cli_request_for("auto", "none", "pipe", "auto");
        request.output.changed_intervals = {interval};
        const auto normalized =
            c3d::runtime::job::normalize_render_job(request, auto_host);
        REQUIRE_FALSE(normalized.has_value());
        CHECK(normalized.error().code ==
              c3d::runtime::job::RenderJobRequestErrorCode::InvalidChangedInterval);
    }
}

#ifdef CHRONON3D_TEST_VIDEO_EXPORT_DISABLED
TEST_CASE("Video RenderJob returns UnsupportedMode when exporter target is disabled") {
    auto registry = make_registry();
    auto job = cli::make_render_job(registry, args_for("0", "hero.mp4"));
    REQUIRE(job.has_value());

    const auto result = cli::execute_render_job(*job);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == c3d::RenderJobErrorCode::UnsupportedMode);
}
#endif
