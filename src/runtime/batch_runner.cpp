#include <chronon3d/runtime/batch_runner.hpp>
#include <chrono>

namespace chronon3d::runtime {

BatchSummary BatchRunner::run(const std::vector<BatchJob>& jobs, JobExecutor executor) {
    const auto t0 = std::chrono::steady_clock::now();
    BatchSummary summary{ static_cast<int>(jobs.size()), 0, 0, 0 };

    for (const auto& job : jobs) {
        if (executor(job)) {
            summary.successful_jobs++;
        } else {
            summary.failed_jobs++;
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    summary.total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    
    return summary;
}

} // namespace chronon3d::runtime
