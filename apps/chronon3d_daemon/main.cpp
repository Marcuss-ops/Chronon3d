// ---------------------------------------------------------------------------
// apps/chronon3d_daemon/main.cpp — Chronon3D IPC daemon (ADR-024, Level 1)
// ---------------------------------------------------------------------------

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/runtime/gpu_device_lost_error.hpp>

#include <spdlog/spdlog.h>

#include "src/ipc/ipc_codec.hpp"
#include "src/ipc/ipc_command_dispatcher.hpp"
#include "src/ipc/unix_socket_transport.hpp"

#ifdef CHRONON3D_ENABLE_CRASH_HANDLER
#include "src/core/crash/crash_handler.hpp"
#endif

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace chronon3d;
using namespace chronon3d::ipc;

namespace {

struct DaemonArgs {
    std::string socket_path{"/tmp/chronon.sock"};
    std::string assets_root;
    std::string backend{"auto"};
};

DaemonArgs parse_args(int argc, char* argv[]) {
    DaemonArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--socket" && i + 1 < argc) {
            args.socket_path = argv[++i];
        } else if (arg == "--assets-root" && i + 1 < argc) {
            args.assets_root = argv[++i];
        } else if (arg == "--backend" && i + 1 < argc) {
            args.backend = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            fmt::print(stderr,
                "Usage: chronon3d_daemon [options]\n\n"
                "  --socket PATH      Unix socket path (default: /tmp/chronon.sock)\n"
                "  --assets-root DIR  Asset resolution root\n"
                "  --backend NAME     Render backend: auto|software|vulkan\n"
                "  --help, -h         Show this help\n");
            std::exit(0);
        }
    }
    return args;
}

graph::BackendPreference preference_from_string(std::string_view s) {
    if (s == "software") return graph::BackendPreference::Software;
    if (s == "vulkan")   return graph::BackendPreference::GPU;
    return graph::BackendPreference::Auto;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

#ifdef CHRONON3D_ENABLE_CRASH_HANDLER
    // Install the fatal crash handler FIRST — before any engine work.
    // A library must never install signal handlers in a client process;
    // the daemon is an application boundary, so it owns this decision.
    chronon3d::crash::install();
#endif

    const auto args = parse_args(argc, argv);

    spdlog::info("Chronon3D Daemon v0.1 (ADR-024 Level 1)");
    spdlog::info("  socket      : {}", args.socket_path);
    spdlog::info("  assets root : {}",
        args.assets_root.empty() ? "(current dir)" : args.assets_root);
    spdlog::info("  backend     : {}", args.backend);

    Config config = Config::from_environment();
    config.set_backend_preference(preference_from_string(args.backend));

    std::unique_ptr<RenderEngine> engine;
    if (args.assets_root.empty()) {
        engine = std::make_unique<RenderEngine>(std::move(config));
    } else {
        engine = std::make_unique<RenderEngine>(std::move(config), args.assets_root);
    }

    spdlog::info("RenderEngine initialised.");

    IpcCommandDispatcher dispatcher(std::move(engine));

    const FrameHandler handler = [&dispatcher, &args](const WireFrame& request) -> WireFrame {
        auto decoded = IpcCodec::decode_request(request);
        if (!decoded) {
            return IpcCodec::encode_reply(0, IpcResponse{
                IpcCreateCompositionResult{3, "bad request"}});
        }

        const auto [msg_id, req] = *decoded;

        if (std::holds_alternative<IpcShutdown>(req)) {
            return IpcCodec::encode_reply(msg_id, IpcResponse{
                IpcShutdownResult{4, "bye"}});
        }

#ifdef CHRONON3D_ENABLE_CRASH_HANDLER
        // Populate the thread-local crash context so a fatal signal during
        // dispatch reports the composition being rendered.  The C-strings
        // point into the decoded request, which lives through dispatch().
        chronon3d::crash::CrashContext crash_ctx;
        crash_ctx.backend = args.backend.c_str();
        if (const auto* rf = std::get_if<IpcRenderFrame>(&req)) {
            crash_ctx.composition_id = rf->composition_id.c_str();
            crash_ctx.frame          = rf->frame_index;
        } else if (const auto* cc = std::get_if<IpcCreateComposition>(&req)) {
            crash_ctx.composition_id = cc->composition_id.c_str();
        }
        chronon3d::crash::set_crash_context(&crash_ctx);
#endif

        const auto response = dispatcher.dispatch(req);

        if (runtime::gpu_worker_poisoned()) {
            spdlog::critical(
                "GPU device lost; terminating poisoned worker for clean restart");
            std::_Exit(EXIT_FAILURE);
        }

#ifdef CHRONON3D_ENABLE_CRASH_HANDLER
        chronon3d::crash::set_crash_context(nullptr);
#endif

        return IpcCodec::encode_reply(msg_id, response);
    };

    UnixSocketTransport transport;
    try {
        transport.listen(args.socket_path);
    } catch (const std::system_error& e) {
        spdlog::error("Failed to bind socket '{}': {}", args.socket_path, e.what());
        return 1;
    }

    spdlog::info("Daemon listening on {}", transport.address());
    spdlog::info("Waiting for clients...");

    const int rc = transport.serve(handler);

    if (rc != 0) {
        spdlog::error("Transport loop exited with error {} ({})", rc, std::strerror(rc));
    }

    spdlog::info("Daemon shutdown. {}", dispatcher.status_json());
    return rc != 0 ? 1 : 0;
}
