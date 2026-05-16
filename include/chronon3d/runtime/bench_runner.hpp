#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/runtime/pipeline.hpp>

#include <string>
#include <vector>

namespace chronon3d::runtime {

struct BenchResult {
    std::string comp_id;
    int frames;
    int warmup;
    double total_elapsed_ms;
    double avg_ms;
    double fps;
};

class BenchRunner {
public:
    static BenchResult run(const std::string& comp_id,
                           Composition& comp,
                           Renderer& renderer,
                           int frames,
                           int warmup);
};

} // namespace chronon3d::runtime
