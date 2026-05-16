#include "../commands.hpp"
#include "../utils/batch_job_spec.hpp"
#include <chronon3d/runtime/batch_runner.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace cli {

int command_batch(const CompositionRegistry& registry, const std::vector<std::string>& job_specs) {
    if (job_specs.empty()) {
        spdlog::error("No batch jobs provided. Use --job composition|frames|output|diagnostic|graph");
        return 1;
    }

    std::vector<runtime::BatchJob> jobs;
    for (const auto& spec : job_specs) {
        std::string parse_error;
        auto parsed = parse_batch_job_spec(spec, &parse_error);
        if (!parsed) {
            spdlog::error("Invalid batch job '{}': {}", spec, parse_error);
            return 1;
        }
        jobs.push_back(runtime::BatchJob{
            .comp_id = parsed->comp_id,
            .frames = parsed->frames,
            .output = parsed->output,
            .diagnostic = parsed->diagnostic,
            .use_modular_graph = parsed->use_modular_graph
        });
    }

    auto summary = runtime::BatchRunner::run(jobs, [&](const runtime::BatchJob& job) {
        spdlog::info("batch: comp={} frames={} output={}", job.comp_id, job.frames, job.output);
        RenderArgs args;
        args.comp_id = job.comp_id;
        args.frames = job.frames;
        args.output = job.output;
        args.diagnostic = job.diagnostic;
        args.use_modular_graph = job.use_modular_graph;
        return command_render(registry, args) == 0;
    });

    spdlog::info("Batch complete: {} successful, {} failed in {}ms", 
                 summary.successful_jobs, summary.failed_jobs, summary.total_ms);
    
    return summary.failed_jobs > 0 ? 1 : 0;
}

} // namespace cli
} // namespace chronon3d
