#pragma once

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/runtime/device_scheduler.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>
#include <chronon3d/media/video/video_job_execution_context.hpp>
#include "chronon_ipc.hpp"
#include <functional>
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <unordered_map>

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
using RenderJobDispatcher = std::function<ipc::Reply(
    const std::string&, std::shared_ptr<media::VideoJobExecutionContext>)>;
RenderJobDispatcher& render_job_dispatcher();
using WarmRenderJobDispatcher = std::function<ipc::Reply(
    const std::string&, std::shared_ptr<SoftwareRenderer>,
    std::shared_ptr<media::VideoJobExecutionContext>)>;
WarmRenderJobDispatcher& warm_render_job_dispatcher();

struct DaemonOptions {
    /// Root directory for asset resolution (fonts, images, etc.).
    std::string assets_root;

    /// Backend selected for the persistent runtime (software | vulkan | auto).
    std::string backend{"auto"};
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
 *   RENDER_JOB     <json>         Render a chronon.render-plan.v1 file
 *   STATUS                        Engine statistics
 *   SHUTDOWN                      Stop serving
 */
class DaemonService {
public:
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
    ipc::Reply ipc_status();
    [[nodiscard]] std::shared_ptr<SoftwareRenderer> warm_renderer_for_device(
        runtime::DeviceId device);

    const CompositionRegistry& m_registry;
    DaemonOptions m_options;
    std::unique_ptr<RenderEngine> m_engine;
    std::unordered_map<runtime::DeviceId, std::shared_ptr<SoftwareRenderer>>
        m_device_sessions;
    std::string m_backend{"auto"};
    runtime::DeviceScheduler m_device_scheduler;
    // One owner of CUDA context + FFmpeg hwdevice per device, process-wide.
    // Resolution chain: DeviceScheduler → DeviceReservation → device() →
    // VideoRuntimeRegistry::get_or_create(device).
    std::shared_ptr<media::VideoRuntimeRegistry> m_video_runtimes;
    std::unique_ptr<PreparedRenderJob> m_prepared_job;   // PREPARE_PLAN result
    std::string m_prepared_comp_id;                       // comp bound to m_prepared_job
    mutable std::mutex m_ipc_state_mutex;                 // warm session ownership
    bool m_running{true};
    int m_render_count{0};
    double m_total_render_ms{0.0};
};

} // namespace chronon3d::cli
