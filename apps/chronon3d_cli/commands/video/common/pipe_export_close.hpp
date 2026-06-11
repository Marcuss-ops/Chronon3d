#pragma once

#include "pipe_export_session.hpp"
#include "pipe_export_setup.hpp"

#include <chronon3d/core/profiling/counters.hpp>

namespace chronon3d::cli {

/// Benchmark breakdown data computed during the close phase.
struct PipeExportBenchmarkData {
    // Wall clock
    double setup_ms{0.0};
    double render_ms{0.0};
    double encode_ms{0.0};
    double wall_time_ms{0.0};

    // Encoder detail
    double write_blocked_ms{0.0};
    double native_convert_ms{0.0};
    double native_send_ms{0.0};
    double native_receive_ms{0.0};
    double native_mux_ms{0.0};
    double native_trailer_ms{0.0};

    // Render loop detail
    double render_graph_eval_ms{0.0};
    double queue_wait_ms{0.0};
    double writer_encode_ms{0.0};
    double conv_copy_ms{0.0};
};

/// Close the encoder, compute benchmark data, log all timing breakdowns.
/// Returns the benchmark data for use in telemetry.
[[nodiscard]] PipeExportBenchmarkData close_pipe_export(
    IVideoEncoder& encoder,
    RenderCounters* counters,
    double render_ms,
    double encode_ms,
    double wall_time_ms,
    double setup_ms,
    double render_graph_eval_ms,
    double queue_wait_ms,
    std::atomic<uint64_t>& writer_encode_us_total,
    const std::string& encoder_backend);

} // namespace chronon3d::cli
