#pragma once

#include <cstddef>
#include <string>

namespace chronon3d::cli {

struct BenchConvertArgs {
    std::string comp_id;
    int frame{0};
    int iterations{10};
    std::string format{"yuv420p"};
    bool apply_gamma{true};
};

struct BenchArgs {
    std::string comp_id;
    int frames{100};
    int warmup{10};
    bool no_dirty_rects{false};
    std::string json_file;
    std::string compare_file;
    std::string stats_json_file;
    bool quiet{false};
    bool include_frame_times{false};
    double fail_if_avg_slower_pct{0.0};
    bool warmup_renderer{false};
    std::size_t warmup_framebuffers{2};
    bool warmup_dummy_frame{false};
};

} // namespace chronon3d::cli
