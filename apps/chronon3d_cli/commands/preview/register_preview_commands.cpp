// ==============================================================================
// TICKET-V3-CLI-UNIFICATION-PREVIEW (Blocco 4.1, Commit 3a of 3)
//
// `chronon preview <comp> --frames 0,30,60,90 -o preview/` — render a
// discrete list of frames to PNGs in a target directory.
//
// Per audit §18 verbatim:
//   chronon preview ProductLaunch \
//     --frames 0,30,60,90 \
//     -o preview/
//   oppure lo rimuovi da `RenderArgs`.  Non lasciare un campo morto.
//
//   Seconda fase:
//   chronon preview ProductLaunch \
//     --frames 0,30,60,90 \
//     --contact-sheet sheet.png
//
// This file (Commit 3a) implements the FIRST phase: a thin facade over
// the canonical plan_render_job + execute_render_job path (same as
// `chronon render` for a single frame, looped over the --frames list).
// Per-frame PNGs land in <output_dir>/frame_NNNN.png.
//
// The --contact-sheet flag is accepted but NOT yet applied (deferred to
// Commit 3b).  A one-line warning is emitted at startup to close the UX
// trap (mirror of the watch --props-file pattern in Commit 1).
//
// Per audit §18: "Deve usare lo stesso RenderJob e lo stesso renderer,
// non una preview pipeline."  This file calls plan_render_job /
// execute_render_job directly — NO new render path, NO new executor.
//
// Removal: `RenderArgs::quick_frames` (declared at commands.hpp:105,
// never read anywhere — verified by grep) is removed in the SAME
// commit because the new PreviewArgs subsumes its intent (rendering a
// subset of frames) with a more general API (specific frame list, not
// a count).
// ==============================================================================
#include "../../command_registry.hpp"
#include "../../commands.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace chronon3d::cli {

namespace {

// Parse a comma-separated frame list: "0,30,60,90" -> [0, 30, 60, 90].
// Empty tokens are skipped.  Trims whitespace around each token.
std::vector<int> parse_frames_string(const std::string& s) {
    std::vector<int> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        // Trim whitespace.
        auto l = tok.find_first_not_of(" \t\r\n");
        auto r = tok.find_last_not_of(" \t\r\n");
        if (l == std::string::npos) continue;
        tok = tok.substr(l, r - l + 1);
        if (tok.empty()) continue;
        try {
            out.push_back(std::stoi(tok));
        } catch (const std::exception&) {
            spdlog::warn("preview: ignoring non-integer frame token '{}'",
                         tok);
        }
    }
    return out;
}

// Format a 4-digit zero-padded frame suffix: 0 -> "0000", 60 -> "0060".
std::string frame_suffix(int frame) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d", frame);
    return std::string(buf);
}

}  // namespace

void register_preview_commands(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<PreviewArgs>();
    auto& args = *state;

    auto* cmd = app.add_subcommand(
        "preview",
        "Render a discrete list of frames to PNGs (and optionally a contact sheet)");
    cmd->add_option("<comp_id>", args.comp_id,
                    "Composition to render")
        ->required();
    cmd->add_option("--frames", args.frames,
                    "Comma-separated frame list (default: 0,30,60,90)")
        ->default_val("0,30,60,90");
    cmd->add_option("-o,--output-dir", args.output_dir,
                    "Output directory for per-frame PNGs (default: ./preview)")
        ->default_val("./preview");
    cmd->add_option("--cell-width", args.cell_width,
                    "Contact-sheet cell width (px, default 640) — used by --contact-sheet")
        ->default_val(640);
    cmd->add_option("--cell-padding", args.cell_padding,
                    "Contact-sheet cell padding (px, default 8) — used by --contact-sheet")
        ->default_val(8);
    // TICKET-PREVIEW-CONTACT-SHEET (Commit 3b, NOT YET IMPLEMENTED in 3a)
    // — accepted but not yet applied.  See forward-point ticket.
    cmd->add_option("--contact-sheet", args.contact_sheet,
                    "(Commit 3b forward-point) Compose all rendered frames into a grid PNG");
    cmd->allow_windows_style_options();

    cmd->callback([state, &ctx]() {
        const auto& a = *state;

        // TICKET-PREVIEW-CONTACT-SHEET (Commit 3b forward-point) — close
        // the UX trap: --contact-sheet is accepted but not yet applied.
        // Print a one-time warning so users are not silently misled.
        if (!a.contact_sheet.empty()) {
            spdlog::warn("--contact-sheet is a forward-point "
                         "(TICKET-PREVIEW-CONTACT-SHEET, Commit 3b): accepted "
                         "but not yet applied in this commit.  Per-frame "
                         "PNGs are still written to '{}'.",
                         a.output_dir.string());
        }

        // Parse the frame list.
        const auto frames = parse_frames_string(a.frames);
        if (frames.empty()) {
            fmt::print(stderr,
                "Error: --frames produced an empty list.  "
                "Pass e.g. --frames 0,30,60,90.\n");
            ctx.exit_code = 1;
            return;
        }

        // Ensure output directory exists.
        std::error_code ec;
        std::filesystem::create_directories(a.output_dir, ec);
        if (ec) {
            fmt::print(stderr,
                "Error: cannot create output dir '{}': {}\n",
                a.output_dir.string(), ec.message());
            ctx.exit_code = 1;
            return;
        }

        spdlog::info("👁  Chronon3D Preview — comp={} frames=[{}] output={}",
                     a.comp_id,
                     fmt::format("{}", fmt::join(frames, ",")),
                     a.output_dir.string());
        spdlog::info("   {} frame(s) to render", frames.size());

        // Per-frame render loop.  Each frame builds a fresh RenderArgs
        // with a single-frame `frames` string ("0" or "60") so the
        // canonical plan_render_job dispatches to RenderMode::Still.
        int rendered = 0;
        int failed = 0;
        const auto t_loop_start = std::chrono::steady_clock::now();
        for (const int frame : frames) {
            RenderArgs r;
            r.comp_id = a.comp_id;
            r.frames = std::to_string(frame);
            r.output = (a.output_dir /
                        fmt::format("frame_{}.png", frame_suffix(frame)))
                           .string();
            r.pipeline = a.pipeline;
            r.log_level = a.log_level;

            const auto t0 = std::chrono::steady_clock::now();
            auto plan = plan_render_job(ctx.registry, r);
            if (!plan) {
                spdlog::error("preview: plan_render_job failed for frame {}",
                              frame);
                ++failed;
                continue;
            }
            const bool ok = execute_render_job(ctx.registry, *plan);
            const auto t1 = std::chrono::steady_clock::now();
            const double ms =
                std::chrono::duration<double, std::milli>(t1 - t0).count();
            if (!ok) {
                spdlog::error("preview: execute_render_job failed for frame {}",
                              frame);
                ++failed;
                continue;
            }
            spdlog::info("  ✓ frame {} → {} ({:.1f} ms)",
                         frame, r.output, ms);
            ++rendered;
        }
        const auto t_loop_end = std::chrono::steady_clock::now();
        const double total_ms =
            std::chrono::duration<double, std::milli>(t_loop_end - t_loop_start)
                .count();

        spdlog::info("Preview complete: {}/{} frames rendered in {:.1f} ms "
                     "(output dir: {})",
                     rendered, frames.size(), total_ms,
                     a.output_dir.string());
        if (failed > 0) {
            spdlog::warn("Preview: {} frame(s) failed.", failed);
            ctx.exit_code = 1;
            return;
        }
        ctx.exit_code = 0;
    });
}

}  // namespace chronon3d::cli
