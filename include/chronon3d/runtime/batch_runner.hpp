#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/composition_registry.hpp>
#include <functional>
#include <string>
#include <vector>

namespace chronon3d::runtime {

struct BatchJob {
    std::string comp_id;
    std::string frames{"0"};
    std::string output;
    bool diagnostic = false;
    bool use_modular_graph = false;
};

struct BatchSummary {
    int total_jobs;
    int successful_jobs;
    int failed_jobs;
    long long total_ms;
};

class BatchRunner {
public:
    using JobExecutor = std::function<bool(const BatchJob&)>;

    static BatchSummary run(const std::vector<BatchJob>& jobs, JobExecutor executor);
};

} // namespace chronon3d::runtime
