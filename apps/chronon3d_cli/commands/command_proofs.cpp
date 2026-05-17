#include "../commands.hpp"
#include "../utils/cli_render_utils.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <array>
#include <algorithm>

namespace chronon3d {
namespace cli {

namespace {

static constexpr std::array<const char*, 13> k_proof_names = {{
    // Geometric diagnostics
    "ProofDepthLadder",
    "ProofPanParallax",
    "ProofTiltParallax",
    "ProofRollStability",
    "ProofOverlayFixed",
    "ProofOrbitSubject",
    "ProofEdgeOnRotation",
    // YouTube realistic proofs
    "ProofYouTubeDepthTitle",
    "ProofYouTubeImageDof",
    "ProofYouTubeQuoteScene",
    "ProofYouTubeNewsCard",
    "ProofYouTubeParallaxThumbnail",
    // Showcase demos
    "ChrononIntroCard",
}};

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

    auto renderer = create_renderer(registry, settings);

    int pass = 0;
    int fail = 0;

    for (const char* name : k_proof_names) {
        if (!registry.contains(name)) {
            spdlog::warn("[proofs] {:30s} ✗  not registered — skipping", name);
            ++fail;
            continue;
        }

        const auto comp = registry.create(name);
        int rendered = 0;

        const bool is_demo = std::string_view(name).find("Chronon") != std::string_view::npos;
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

    const int total_proofs = static_cast<int>(k_proof_names.size());
    const auto abs_dir = std::filesystem::absolute(args.output_dir).string();

    spdlog::info("");
    spdlog::info("[proofs] Result: {}/{} passed  →  {}", pass, total_proofs, abs_dir);

    return fail > 0 ? 1 : 0;
}

} // namespace cli
} // namespace chronon3d
