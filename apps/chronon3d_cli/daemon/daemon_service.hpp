#pragma once

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/runtime/device_scheduler.hpp>
#include <chronon3d/runtime/job/render_job_request.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>
#include <chronon3d/media/video/video_job_execution_context.hpp>
#include <chronon3d/media/video/packet_assembler.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#include "chronon_ipc.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <string>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <unordered_map>
#include <map>
#include <deque>
#include <string_view>

namespace chronon3d {
    class RenderEngine;
    class PreparedRenderJob;
    class Config;
    class SoftwareRenderer;
}

namespace chronon3d::cli {

/// RENDER_JOB dispatcher, registered by the render command group when it is
/// linked into the executable. Unset by default so a render-less build
/// answers RENDER_JOB with NotFound. Keeps chronon3d_cli_core free of any
/// render-group link dependency.
///
/// Phase 9 (CLI/IPC parity): the dispatcher carries the ALREADY-NORMALIZED
/// job, never a JSON string. The daemon parses the wire payload once,
/// normalizes once, and hands the immutable `NormalizedRenderJob` to the
/// render group; no transport layer may re-parse or re-normalize (the
/// `NormalizedRenderJob.execution` fields are final by contract).
/// `warm_renderer` is runtime state (null for the Direct-YUV path), not a
/// second execution authority.
using RenderJobDispatcher = std::function<ipc::Reply(
    const runtime::job::NormalizedRenderJob&,
    std::shared_ptr<SoftwareRenderer>,
    std::shared_ptr<media::VideoJobExecutionContext>)>;
RenderJobDispatcher& render_job_dispatcher();

struct DaemonOptions {
    /// Root directory for asset resolution (fonts, images, etc.).
    std::string assets_root;

    /// Backend selected for the persistent runtime (software | vulkan | auto).
    /// The default is the resolver-owned constant so the daemon and the
    /// resolver fallback chain cannot drift (audit N8).
    std::string backend{std::string{media::kDefaultBackend}};
    std::uint32_t gpu_device_id{chronon3d::Config::kAutoGpuDevice};

    /// Shell command to rebuild the project (e.g. "bash build-fast.sh cli").
    /// Empty = no rebuild support.
    std::string build_command;

    /// Directories to watch for file changes (inotify-style poll).
    std::vector<std::string> watch_dirs;
};

/**
 * DaemonService — persistent rendering daemon (warm render shell).
 *
 * Keeps a RenderEngine alive across renders so that framebuffer pools,
 * font engines, glyph atlases, image caches, and node caches stay warm.
 *
 * NOTE: the daemon does NOT perform hot-reload of its own binary (it
 * cannot reload its own code while running).  For genuine hot-reload
 * use `chronon watch <comp>` (TICKET-V3-CLI-UNIFICATION-WATCH-SUPERVISOR,
 * Blocco 4.1) which re-execs the freshly-built CLI as a subprocess and
 * renders the selected frame on each source change.
 *
 * Accepts commands via stdin:
 *   render <comp> <frame> [out]   Render a frame
 *   status / st                   Show engine stats
 *   clear  / cc                   Clear all caches
 *   reload / rl                   Rebuild project (new binary requires
 *                                 manual restart; see NOTE above)
 *   help   / h                    Show help
 *   quit   / q                    Shutdown
 *
 * Or, via a UNIX-domain socket (RenderingGen → Chronon), keeps the same warm
 * engine alive across hundreds of jobs using a length-prefixed binary
 * protocol (see chronon_ipc.hpp):
 *   PREFETCH_ASSET <path>         Warm the asset cache
 *   PREPARE_PLAN   <comp-id>      Compile + plan once (PreparedRenderJob)
 *   RENDER_OVERLAY <frame> [out]  Render a frame from the prepared plan
 *   RENDER_JOB     <json>         Render a chronon.render-plan.v2 file
 *                                 (decoded once by daemon_render_job_payload;
 *                                 normalized once; dispatched as
 *                                 NormalizedRenderJob; optional `job_id`
 *                                 enables lifecycle lookup)
 *   RENDER_BATCH   <json>         Render 1–64 RENDER_JOB objects sequentially
 *                                 on this daemon, returning one result each
 *   GET_JOB_STATUS <job-id>       Return prepared/running/completed/failed
 *                                 status for a tracked RENDER_JOB
 *   STATUS                        Engine statistics
 *   SHUTDOWN                      Stop serving
 */
class DaemonService {
public:
    enum class JobState : std::uint8_t { Prepared, Running, Completed, Failed };
    struct JobStatusRecord {
        JobState state{JobState::Prepared};
        std::string message;
    };

    DaemonService(const CompositionRegistry& registry, DaemonOptions options);
    ~DaemonService();

    /// Blocking main loop — reads commands from stdin until 'quit'.
    void run();

    /// Blocking main loop over a UNIX-domain socket at `path`.  Serves the
    /// IPC protocol until a client sends SHUTDOWN (or a transport error).
    void run_socket(const std::string& path);

private:
    void handle_command(const std::string& line);
    void cmd_render(const std::vector<std::string>& args);
    void cmd_reload();
    void cmd_clear_caches();
    void cmd_status();
    void cmd_help();

    // ── UNIX-socket IPC dispatch (RenderingGen → Chronon) ───────────────
    ipc::Reply handle_ipc(const ipc::Request& req);
    ipc::Reply ipc_prefetch_asset(const std::string& path);
    ipc::Reply ipc_prepare_plan(const std::string& comp_id);
    ipc::Reply ipc_render_overlay(const std::string& args);
    ipc::Reply handle_render_job_payload(const std::string& payload);
    /// Shared execution path for an already decoded request. The batch
    /// handler uses it to preserve the one-decode/one-normalize contract.
    ipc::Reply handle_render_job_request(
        runtime::job::RenderJobRequest request, std::string job_id,
        bool batch_member = false);
    ipc::Reply ipc_render_batch(const std::string& payload);
    ipc::Reply ipc_status();
    ipc::Reply ipc_get_job_status(const std::string& job_id);
    ipc::Reply ipc_assemble_segments(const std::string& payload);
    [[nodiscard]] bool begin_job_status(std::string_view job_id);
    void update_job_status(std::string_view job_id, JobState state,
                           std::string message);
    [[nodiscard]] std::shared_ptr<SoftwareRenderer> warm_renderer_for_device(
        runtime::DeviceId device);

    const CompositionRegistry& m_registry;
    DaemonOptions m_options;
    std::unique_ptr<RenderEngine> m_engine;
    std::unordered_map<runtime::DeviceId, std::shared_ptr<SoftwareRenderer>>
        m_device_sessions;
    std::string m_backend{std::string{media::kDefaultBackend}};
    runtime::DeviceScheduler m_device_scheduler;
    // One owner of CUDA context + FFmpeg hwdevice per device, process-wide.
    // Resolution chain: DeviceScheduler → DeviceReservation → device() →
    // VideoRuntimeRegistry::get_or_create(device).
    std::shared_ptr<media::VideoRuntimeRegistry> m_video_runtimes;
    // Native FullGraph video slots/surfaces and scratch arena are retained per
    // device beside the renderer. Batch admission guarantees single-job use.
    std::unordered_map<runtime::DeviceId,
        std::shared_ptr<media::PersistentVideoRenderState>> m_video_render_states;
    mutable std::mutex m_video_render_states_mutex;
    // ── Per-domain IPC state (audit N2) ────────────────────────────────
    // One mutex per fact. A single global mutex made a full compile block
    // STATUS / PREFETCH / RENDER_OVERLAY, and made a disk write block the
    // concurrent daemon transport. State strings are gone: STATUS now reads
    // the prepared handle under m_prepared_mutex and the counters are atomic.
    //
    // shared_ptr, not unique_ptr: RENDER_OVERLAY snapshots the handle under
    // m_prepared_mutex and then renders lock-free, so a concurrent
    // PREPARE_PLAN cannot invalidate the plan mid-render.
    std::shared_ptr<PreparedRenderJob> m_prepared_job;    // PREPARE_PLAN result
    std::string m_prepared_comp_id;                       // comp bound to m_prepared_job
    mutable std::mutex m_prepared_mutex;                  // guards the two lines above
    // RenderEngine mutation domain (asset import, plan preparation). Kept
    // separate from the prepared-slot and overlay domains; RenderEngine
    // itself still has no documented concurrent-prepare contract.
    mutable std::mutex m_engine_mutex;
    // RENDER_OVERLAY execution domain. A PreparedRenderJob owns exactly one
    // single-job JobSession, so overlays on the same plan must serialize; no
    // other handler waits on this mutex.
    mutable std::mutex m_overlay_mutex;
    // Warm per-device session map. IPC workers run one thread per connection
    // (serve_concurrent), and the RENDER_JOB path does not hold any IPC
    // mutex; the map needs its own lock (audit N1).
    mutable std::mutex m_device_sessions_mutex;
    // A daemon owns one Vulkan/CUDA device context. Video RENDER_JOB creates
    // a job-scoped exporter/backend, so admitting multiple jobs concurrently
    // multiplies full-frame native surfaces until the device memory budget is
    // exhausted. Keep ingress concurrent for STATUS/PREFETCH, but gate the
    // actual GPU job domain at this owner boundary. This is the same
    // single-lane contract exposed by RenderingGen's gpu_lanes=1 setting.
    //
    // This mutex IS the execution domain, and the concurrency policy
    // (daemon_render_concurrency.hpp `decide_render_admission`) is what says so:
    // every job the daemon admits must carry a SERIAL verdict, and the job path
    // refuses to run one that does not (the one-job domain cannot honour an
    // overlap verdict). The verdict is data — it names the job class, charges
    // the measured per-job peak against the device, and reports the reason — so
    // the day a producer submits a disjoint chunk class, that refusal is the
    // signal that this boundary must change with it, instead of a silent
    // serialization nobody can explain.
    // Shared for policy-approved disjoint DirectYUV chunks; exclusive for
    // monolithic and FullGraph jobs. The IPC path chooses the side from the
    // same RenderPolicyDecision used for VRAM admission.
    mutable std::shared_mutex m_render_job_mutex;
    // RENDER_BATCH owns this domain exclusively for its entire ordered
    // sequence. Single jobs take the shared side, preventing interleaving
    // while retaining the existing policy-controlled GPU admission below.
    mutable std::shared_mutex m_render_batch_domain_mutex;
    // Dynamic VRAM-credit admission domain for RENDER_JOB.
    // Concurrency is governed dynamically by DeviceScheduler based on
    // VRAM credits and render job slots (default: 2 concurrent jobs).
    // When device resources are saturated, incoming jobs block on
    // m_admission_cv until an active job finishes and releases its reservation.
    mutable std::mutex m_admission_mutex;
    mutable std::condition_variable m_admission_cv;
    std::atomic<bool> m_running{true};
    std::atomic<int> m_render_count{0};
    std::atomic<double> m_total_render_ms{0.0};
    mutable std::mutex m_job_status_mutex;
    std::map<std::string, JobStatusRecord, std::less<>> m_job_statuses;
    std::deque<std::string> m_terminal_job_history;
    static constexpr std::size_t kRetainedTerminalJobStatuses = 256;
};

} // namespace chronon3d::cli
