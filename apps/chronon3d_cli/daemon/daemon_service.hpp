#pragma once

#include <chronon3d/core/composition/composition_registry.hpp>
#include <string>
#include <memory>
#include <vector>

namespace chronon3d {
    class RenderEngine;
    class Config;
}

namespace chronon3d::cli {

struct DaemonOptions {
    /// Root directory for asset resolution (fonts, images, etc.).
    std::string assets_root;

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
 */
class DaemonService {
public:
    DaemonService(const CompositionRegistry& registry, DaemonOptions options);
    ~DaemonService();

    /// Blocking main loop — reads commands from stdin until 'quit'.
    void run();

private:
    void handle_command(const std::string& line);
    void cmd_render(const std::vector<std::string>& args);
    void cmd_reload();
    void cmd_clear_caches();
    void cmd_status();
    void cmd_help();

    const CompositionRegistry& m_registry;
    DaemonOptions m_options;
    std::unique_ptr<RenderEngine> m_engine;
    bool m_running{true};
    int m_render_count{0};
    double m_total_render_ms{0.0};
};

} // namespace chronon3d::cli
