#include "batch_runner.hpp"
#include <chronon3d/core/profiling/profiling.hpp>

namespace chronon3d::cli {

BatchSummary BatchRunner::run(const std::vector<BatchJob>& jobs, JobExecutor executor) {
    const auto t0 = profiling::now();
    BatchSummary summary{ static_cast<int>(jobs.size()), 0, 0, 0 };

    for (const auto& job : jobs) {
        if (executor(job)) {
            summary.successful_jobs++;
        } else {
            summary.failed_jobs++;
        }
    }

    const auto t1 = profiling::now();
    summary.total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    
    return summary;
}

} // namespace chronon3d::cli
