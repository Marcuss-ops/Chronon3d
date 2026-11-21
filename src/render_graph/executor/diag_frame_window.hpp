#pragma once

#include <cstdint>
#include <cstdlib>
#include <string_view>

// PERF-DISPATCH-01 — sampling window for the opt-in per-node diagnostic.
//
// The per-node dispatch stream ([exec-node-diag]) used to be pinned to the
// first three frames, where lazy pipeline creation, first decode and first
// upload dominate: the numbers said nothing about the steady state the render
// loop actually lives in. The window is now selectable through
// CHRONON3D_DIAG_EXEC_FRAMES="first-last" (inclusive, both optional), while the
// default stays 0-2 so an unset environment keeps the historical output byte
// for byte.
//
// The gate is only consulted when CHRONON3D_DIAG_EXEC_LOG is set, so a render
// without diagnostics never parses the environment. The [DIAG-exec] cache line
// inside execute_single_node keeps its own gate and is not windowed here.
[[nodiscard]] inline bool diag_frame_window_enabled(std::int64_t frame) noexcept {
    struct Window {
        std::int64_t first{0};
        std::int64_t last{2};
    };
    static const Window window = [] {
        Window parsed{};
        const char* value = std::getenv("CHRONON3D_DIAG_EXEC_FRAMES");
        if (!value) return parsed;
        const std::string_view setting(value);
        if (setting.empty()) return parsed;
        const auto dash = setting.find('-');
        const auto to_int = [](std::string_view text,
                               std::int64_t fallback) noexcept {
            if (text.empty()) return fallback;
            std::int64_t result = 0;
            for (const char c : text) {
                if (c < '0' || c > '9') return fallback;
                result = result * 10 + (c - '0');
            }
            return result;
        };
        parsed.first = dash == std::string_view::npos
            ? to_int(setting, parsed.first)
            : to_int(setting.substr(0, dash), parsed.first);
        parsed.last = dash == std::string_view::npos
            ? parsed.first
            : to_int(setting.substr(dash + 1), parsed.last);
        if (parsed.last < parsed.first) parsed.last = parsed.first;
        return parsed;
    }();
    return frame >= window.first && frame <= window.last;
}
