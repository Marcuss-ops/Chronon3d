#pragma once

#include <chrono>

namespace chronon3d::cli {

struct StartupBreakdown {
    double cli_init_ms{0.0};
    double plan_prepare_ms{0.0};
    double encoder_create_ms{0.0};
    double encoder_open_hw_ctx_ms{0.0};
    double cuda_compositor_warmup_ms{0.0};
    double encoder_open_nvenc_ms{0.0};
    double encoder_open_mux_header_ms{0.0};
    double vulkan_instance_ms{0.0};
    double vulkan_device_ms{0.0};
    double vulkan_pipelines_ms{0.0};
    double renderer_runtime_init_ms{0.0};
    double other_startup_ms{0.0};
    double total_startup_ms{0.0};
};

struct PrepareBreakdown {
    double font_preflight_ms{0.0};
    double pool_warmup_ms{0.0};
    double triple_arena_alloc_ms{0.0};
    double writer_thread_spawn_ms{0.0};
    double other_prepare_ms{0.0};
    double total_prepare_ms{0.0};
};

} // namespace chronon3d::cli
