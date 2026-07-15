#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include "../../utils/job/render_job.hpp"

#include <CLI/CLI.hpp>
#include <chronon3d/backends/image/image_loader.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace chronon3d::cli {

namespace {

std::vector<int> parse_frames_string(const std::string& text) {
    std::vector<int> frames;
    std::stringstream stream(text);
    std::string token;

    while (std::getline(stream, token, ',')) {
        const auto first = token.find_first_not_of(" \t\r\n");
        const auto last = token.find_last_not_of(" \t\r\n");
        if (first == std::string::npos) continue;

        token = token.substr(first, last - first + 1);
        try {
            frames.push_back(std::stoi(token));
        } catch (const std::exception&) {
            spdlog::warn("preview: ignoring non-integer frame token '{}'", token);
        }
    }

    return frames;
}

std::string frame_suffix(int frame) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d", frame);
    return buffer;
}

std::filesystem::path preview_frame_path(
    const std::filesystem::path& output_dir,
    int frame)
{
    return output_dir / fmt::format("frame_{}.png", frame_suffix(frame));
}

bool compose_contact_sheet(const std::vector<std::string>& png_paths,
                           const std::string& output_path,
                           int cell_width,
                           int cell_padding) {
    if (png_paths.empty()) return false;

    std::vector<std::shared_ptr<Framebuffer>> frames;
    frames.reserve(png_paths.size());
    for (const auto& path : png_paths) {
        auto framebuffer = io::load_image_as_framebuffer(
            path, cell_width, /*height=*/-1);
        if (!framebuffer) {
            spdlog::warn("preview: failed to load '{}' for contact sheet", path);
            continue;
        }
        frames.push_back(std::move(framebuffer));
    }
    if (frames.empty()) return false;

    const int count = static_cast<int>(frames.size());
    const int columns = static_cast<int>(
        std::ceil(std::sqrt(static_cast<double>(count))));
    const int rows = static_cast<int>(
        std::ceil(static_cast<double>(count) / columns));
    const int cell_height = frames.front()->height();
    const int actual_cell_width = frames.front()->width();
    const int width = columns * actual_cell_width +
                      (columns + 1) * cell_padding;
    const int height = rows * cell_height + (rows + 1) * cell_padding;

    auto sheet = std::make_shared<Framebuffer>(width, height);
    sheet->clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    for (int index = 0; index < count; ++index) {
        const int column = index % columns;
        const int row = index / columns;
        const int x = cell_padding + column * (actual_cell_width + cell_padding);
        const int y = cell_padding + row * (cell_height + cell_padding);
        sheet->blit(*frames[index], x, y);
    }

    const std::filesystem::path sheet_path(output_path);
    if (sheet_path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(sheet_path.parent_path(), error);
        if (error) {
            spdlog::error("preview: cannot create contact-sheet directory '{}': {}",
                          sheet_path.parent_path().string(), error.message());
            return false;
        }
    }

    return save_png(*sheet, output_path);
}

} // namespace

void register_preview_commands(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<PreviewArgs>();
    auto& args = *state;

    auto* command = app.add_subcommand(
        "preview",
        "Render selected frames through one canonical RenderJob session");
    command->add_option("<comp_id>", args.comp_id, "Composition to render")
        ->required();
    command->add_option("--frames", args.frames,
                        "Comma-separated frame list")
        ->default_val("0,30,60,90");
    command->add_option("-o,--output-dir", args.output_dir,
                        "Output directory for per-frame PNGs")
        ->default_val("./preview");
    command->add_option("--contact-sheet", args.contact_sheet,
                        "Compose rendered frames into a grid PNG");
    command->add_option("--cell-width", args.cell_width,
                        "Contact-sheet cell width")
        ->default_val(640);
    command->add_option("--cell-padding", args.cell_padding,
                        "Contact-sheet cell padding")
        ->default_val(8);
    command->allow_windows_style_options();

    command->callback([state, &ctx]() {
        const auto& preview = *state;
        const auto frames = parse_frames_string(preview.frames);
        if (frames.empty()) {
            spdlog::error("preview: --frames produced an empty list");
            ctx.exit_code = 1;
            return;
        }

        std::error_code error;
        std::filesystem::create_directories(preview.output_dir, error);
        if (error) {
            spdlog::error("preview: cannot create '{}': {}",
                          preview.output_dir.string(), error.message());
            ctx.exit_code = 1;
            return;
        }

        RenderArgs render_args;
        render_args.comp_id = preview.comp_id;
        render_args.frames = std::to_string(frames.front());
        render_args.output =
            (preview.output_dir / "frame_####.png").string();
        render_args.pipeline = preview.pipeline;
        render_args.log_level = preview.log_level;
        render_args.cpu_budget = ctx.cpu_budget;
        render_args.command_line = ctx.command_line;

        auto job = make_render_job(ctx.registry, render_args);
        if (!job) {
            ctx.exit_code = 1;
            return;
        }

        job->mode = RenderMode::Sequence;
        job->selected_frames.reserve(frames.size());
        for (const int frame : frames) {
            job->selected_frames.emplace_back(frame);
        }
        const auto [min_frame, max_frame] = std::minmax_element(
            frames.begin(), frames.end());
        job->first_frame = Frame{*min_frame};
        job->last_frame = Frame{*max_frame};
        job->frame_step = Frame{1};

        spdlog::info("Preview {} frames [{}] in one renderer session",
                     preview.comp_id, fmt::join(frames, ","));
        const auto started = std::chrono::steady_clock::now();
        const auto result = execute_render_job(*job);
        const double total_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();

        std::vector<std::string> rendered_paths;
        rendered_paths.reserve(frames.size());
        for (const int frame : frames) {
            const auto path = preview_frame_path(preview.output_dir, frame);
            if (std::filesystem::is_regular_file(path)) {
                rendered_paths.push_back(path.string());
            }
        }

        const int rendered = static_cast<int>(rendered_paths.size());
        const int failed = static_cast<int>(frames.size()) - rendered;
        if (!result) {
            spdlog::error("preview failed: {}", result.error().message);
        }
        spdlog::info("Preview complete: {}/{} frame(s) in {:.1f} ms",
                     rendered, frames.size(), total_ms);

        bool contact_sheet_ok = true;
        if (!preview.contact_sheet.empty()) {
            contact_sheet_ok = compose_contact_sheet(
                rendered_paths, preview.contact_sheet,
                preview.cell_width, preview.cell_padding);
            if (!contact_sheet_ok) {
                spdlog::error("preview: contact sheet generation failed");
            } else {
                spdlog::info("Contact sheet saved to {}", preview.contact_sheet);
            }
        }

        ctx.exit_code = result && failed == 0 && contact_sheet_ok ? 0 : 1;
    });
}

} // namespace chronon3d::cli
