#pragma once

#include <string>

namespace chronon3d::cli {

// NOTE: BenchArgs (with the orphan `stats_json_file` field) was removed on
// 2026-10-24 as zero-caller legacy of the bench-app split: the live
// `chronon3d::cli::BenchArgs` is declared in apps/chronon3d_bench/args.hpp and
// is the only one with real writers (`--stats-json`, `--node-memory-json`).
// BenchConvertArgs stays here because commands/dev/command_bench_convert.cpp
// still needs it.
struct BenchConvertArgs {
    std::string comp_id;
    int frame{0};
    int iterations{10};
    std::string format{"yuv420p"};
    bool apply_gamma{true};
};

} // namespace chronon3d::cli
