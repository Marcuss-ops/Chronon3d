#include <chronon3d/core/cancellation_token.hpp>
#include <csignal>
#include <cstdlib>
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace {

CancellationToken* s_global_token = nullptr;

extern "C" void signal_handler(int sig) {
    if (s_global_token) {
        spdlog::warn("[cancellation] Signal {} received — cancelling render...", sig);
        s_global_token->cancel();
    }
}

} // anonymous namespace

void install_signal_cancellation(CancellationToken& token) {
    s_global_token = &token;
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
}

void restore_default_signal_handlers() {
    s_global_token = nullptr;
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
}

} // namespace chronon3d
