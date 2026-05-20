#include "../commands.hpp"
#include "../utils/cli_render_utils.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <array>
#include <algorithm>
#include <string_view>

namespace chronon3d {
namespace cli {

namespace {
static constexpr std::array<Frame, 3> k_proof_key_frames = {{0, 44, 89}};
static constexpr std::array<Frame, 3> k_demo_key_frames  = {{0, 89, 179}};
} // namespace

int command_proofs(const CompositionRegistry& registry, const ProofsArgs& args) {
    std::error_code ec;
    std::filesystem::create_directories(args.output_dir, ec);
    if (ec) {
        spdlog::error("[proofs] Cannot create output directory {}: {}", args.output_dir, ec.message());
        return 1;
    }

    RenderSettings settings;
    settings.use_modular_graph = true;   // proof compositions require graph path
    settings.ssaa_factor       = args.ssaa;
    settings.diagnostic        = true;

    auto renderer = create_renderer(registry, settings);

    int pass = 0;
    int fail = 0;
    int total_processed = 0;

    auto names = registry.available();
    std::sort(names.begin(), names.end());

    for (const auto& name : names) {
        // Only run on compositions tagged as "Proof" by naming convention or explicit showcase demos
        const bool is_proof = name.find("Proof") == 0;
        const bool is_demo  = (name == "ChrononIntroCard") || (name == "LilDirkClean");
        
        if (!is_proof && !is_demo) continue;

        ++total_processed;
        const auto comp = registry.create(name);
        int rendered = 0;

        const auto& key_frames = is_demo ? k_demo_key_frames : k_proof_key_frames;

        for (const Frame f : key_frames) {
            auto fb = renderer->render_frame(comp, f);
            if (!fb) {
                spdlog::error("[proofs] {} frame {} render failed", name, f);
                continue;
            }

            const std::string path = fmt::format("{}/{}_f{:03d}.png", args.output_dir, name, f);
            if (save_png(*fb, path)) {
                ++rendered;
            } else {
                spdlog::error("[proofs] Failed to write {}", path);
            }
        }

        const int total = static_cast<int>(key_frames.size());
        if (rendered == total) {
            spdlog::info("[proofs] {:30s} ✓  {}/{} frames", name, rendered, total);
            ++pass;
        } else {
            spdlog::warn("[proofs] {:30s} ✗  {}/{} frames", name, rendered, total);
            ++fail;
        }
    }

    const auto abs_dir = std::filesystem::absolute(args.output_dir).string();

    spdlog::info("");
    spdlog::info("[proofs] Result: {}/{} passed  →  {}", pass, total_processed, abs_dir);

    return fail > 0 ? 1 : 0;
}

} // namespace cli
} // namespace chronon3d
