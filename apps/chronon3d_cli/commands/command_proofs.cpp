#include "../commands.hpp"
#include "../proof_suites.hpp"
#include <chronon3d/runtime/pipeline.hpp>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <filesystem>
#include <iostream>

namespace chronon3d {
namespace cli {

static int render_proof_suite(const CompositionRegistry& registry,
                               const ProofSuite& suite,
                               const std::filesystem::path& out_dir,
                               const ProofsArgs& p_args) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(out_dir, ec);

    int failed = 0;
    for (const auto& pf : suite.frames) {
        if (!registry.contains(pf.composition)) {
            spdlog::warn("Composition not registered (skipped): {}", pf.composition);
            continue;
        }
        const auto out = out_dir / pf.filename;
        spdlog::info("  {} frame {} → {}", pf.composition, pf.frame, out.string());

        RenderArgs args;
        args.comp_id   = pf.composition;
        // In the new RenderArgs, we use 'frames' string
        args.frames    = std::to_string(pf.frame);
        args.output    = out.string();
        args.use_modular_graph = p_args.use_modular_graph;
        args.ssaa              = p_args.ssaa;

        const int r = command_render(registry, args);
        if (r != 0 || !fs::exists(out) || fs::file_size(out) == 0) {
            spdlog::error("  FAILED: {} frame {}", pf.composition, pf.frame);
            ++failed;
        }
    }
    return failed == 0 ? 0 : 1;
}

int command_proofs(const CompositionRegistry& registry, const ProofsArgs& args) {
    if (args.suite == "list") {
        fmt::print("Available proof suites:\n");
        for (const auto& s : proof_suites())
            fmt::print("  {}\n", s.name);
        return 0;
    }

    if (args.suite == "all") {
        int result = 0;
        for (const auto& suite : proof_suites()) {
            spdlog::info("[proofs] Suite: {}", suite.name);
            const auto suite_dir = std::filesystem::path(args.output_dir) / suite.name;
            result |= render_proof_suite(registry, suite, suite_dir, args);
        }
        return result;
    }

    const auto suite = find_proof_suite(args.suite);
    if (!suite) {
        spdlog::error("Unknown proof suite '{}'. Use 'proofs list' to see available suites.", args.suite);
        return 1;
    }

    spdlog::info("[proofs] Suite: {}", suite->name);
    const auto suite_dir = std::filesystem::path(args.output_dir) / suite->name;
    return render_proof_suite(registry, *suite, suite_dir, args);
}

} // namespace cli
} // namespace chronon3d
