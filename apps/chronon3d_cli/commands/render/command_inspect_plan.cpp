#include "command_inspect_plan.hpp"

#include "render_plan_inspection.hpp"
#include "render_plan_preparation.hpp"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace chronon3d::cli {

namespace {

nlohmann::json inspection_to_json(const ResolvedRenderPlanInspection& i) {
    nlohmann::json js;
    js["job"] = {
        {"id", i.job.id},
        {"schema", i.job.schema},
        {"content_digest", i.job.content_digest},
        {"request_digest", i.job.request_digest},
        {"asset_manifest_digest", i.job.asset_manifest_digest},
    };
    js["canvas"] = {
        {"width", i.canvas.width},
        {"height", i.canvas.height},
        {"fps_num", i.canvas.fps_num},
        {"fps_den", i.canvas.fps_den},
        {"frames", i.canvas.frames},
    };
    js["output"] = {
        {"path", i.output_path},
        {"format", i.output_format},
        {"codec", i.video_codec},
    };

    nlohmann::json layers = nlohmann::json::array();
    for (const auto& layer : i.layers) {
        nlohmann::json jl;
        jl["id"] = layer.id;
        jl["type"] = layer.type;
        if (!layer.semantic_role.empty()) {
            jl["semantic_role"] = layer.semantic_role;
        }
        jl["preset"] = {{"requested", layer.preset.requested}};
        if (!layer.asset.empty()) {
            jl["asset"] = layer.asset;
        }
        if (!layer.source.empty()) {
            jl["source"] = layer.source;
        }
        if (!layer.text.empty()) {
            jl["text"] = layer.text;
        }

        if (!layer.font.asset.empty() || !layer.font.family.empty()) {
            nlohmann::json font;
            if (!layer.font.asset.empty()) {
                font["asset"] = layer.font.asset;
            }
            if (!layer.font.family.empty()) {
                font["family"] = layer.font.family;
            }
            if (layer.font.weight) {
                font["weight"] = *layer.font.weight;
            }
            font["resolved"] = layer.font.resolved;
            jl["font"] = std::move(font);
        }
        if (layer.font_size) {
            jl["font_size"] = *layer.font_size;
        }
        if (!layer.fill.empty()) {
            jl["fill"] = layer.fill;
        }
        if (!layer.stroke.empty()) {
            jl["stroke"] = layer.stroke;
        }
        if (!layer.background.empty()) {
            jl["background"] = layer.background;
        }

        nlohmann::json layout = nlohmann::json::object();
        if (!layer.layout.requested_anchor.empty()) {
            layout["requested_anchor"] = layer.layout.requested_anchor;
        }
        if (!layer.layout.alignment.empty()) {
            layout["alignment"] = layer.layout.alignment;
        }
        if (layer.layout.x) {
            layout["x"] = *layer.layout.x;
        }
        if (layer.layout.y) {
            layout["y"] = *layer.layout.y;
        }
        if (layer.layout.width) {
            layout["width"] = *layer.layout.width;
        }
        if (layer.layout.height) {
            layout["height"] = *layer.layout.height;
        }
        if (layer.layout.offset_x) {
            layout["offset_x"] = *layer.layout.offset_x;
        }
        if (layer.layout.offset_y) {
            layout["offset_y"] = *layer.layout.offset_y;
        }
        if (!layout.empty()) {
            jl["layout"] = std::move(layout);
        }

        if (!layer.motion.preset.empty() || !layer.motion.unit.empty() ||
            layer.motion.enter_frames || layer.motion.exit_frames) {
            nlohmann::json motion;
            if (!layer.motion.preset.empty()) {
                motion["preset"] = layer.motion.preset;
            }
            if (!layer.motion.unit.empty()) {
                motion["unit"] = layer.motion.unit;
            }
            if (layer.motion.enter_frames) {
                motion["enter_frames"] = *layer.motion.enter_frames;
            }
            if (layer.motion.exit_frames) {
                motion["exit_frames"] = *layer.motion.exit_frames;
            }
            jl["motion"] = std::move(motion);
        }

        if (!layer.blend_mode.empty()) {
            jl["blend_mode"] = layer.blend_mode;
        }
        if (layer.opacity) {
            jl["opacity"] = *layer.opacity;
        }
        jl["loop"] = layer.loop;
        if (layer.start_frame) {
            jl["start_frame"] = *layer.start_frame;
        }
        if (layer.duration_frames) {
            jl["duration_frames"] = *layer.duration_frames;
        }
        layers.push_back(std::move(jl));
    }
    js["layers"] = std::move(layers);
    return js;
}

std::string inspection_to_text(const ResolvedRenderPlanInspection& i) {
    std::ostringstream out;
    out << "Job\n"
        << "  id                    " << i.job.id << '\n'
        << "  schema                " << i.job.schema << '\n'
        << "  content fingerprint   " << i.job.content_digest << '\n'
        << "  request fingerprint   " << i.job.request_digest << '\n'
        << "  asset manifest        " << i.job.asset_manifest_digest << "\n\n"
        << "Canvas\n"
        << "  " << i.canvas.width << 'x' << i.canvas.height << '\n'
        << "  " << i.canvas.fps_num << (i.canvas.fps_den != 1
            ? "/" + std::to_string(i.canvas.fps_den) : "") << " fps\n"
        << "  " << i.canvas.frames << " frames\n\n"
        << "Output\n"
        << "  path                  " << i.output_path << '\n'
        << "  format                " << i.output_format << '\n'
        << "  codec                 " << i.video_codec << '\n';

    for (const auto& layer : i.layers) {
        out << "\nLayer " << layer.id << '\n'
            << "  type                  " << layer.type << '\n';
        if (!layer.semantic_role.empty()) {
            out << "  semantic_role         " << layer.semantic_role << '\n';
        }
        if (!layer.preset.requested.empty()) {
            out << "  preset                " << layer.preset.requested << '\n';
        }
        if (!layer.asset.empty()) {
            out << "  asset                 " << layer.asset << '\n';
        }
        if (!layer.source.empty()) {
            out << "  source                " << layer.source << '\n';
        }
        if (!layer.text.empty()) {
            out << "  text                  " << layer.text << '\n';
        }

        const bool has_visual = !layer.font.asset.empty() ||
            !layer.font.family.empty() || layer.font.resolved ||
            layer.font_size || !layer.fill.empty() || !layer.stroke.empty() ||
            !layer.background.empty();
        if (has_visual) {
            out << "\n  Resolved visual\n";
            if (!layer.font.asset.empty()) {
                out << "    font                " << layer.font.asset << '\n';
            }
            if (!layer.font.family.empty()) {
                out << "    family              " << layer.font.family << '\n';
            }
            if (layer.font.weight) {
                out << "    weight              " << *layer.font.weight << '\n';
            }
            out << "    font resolved       "
                << (layer.font.resolved ? "yes" : "no") << '\n';
            if (layer.font_size) {
                out << "    size                " << *layer.font_size << '\n';
            }
            if (!layer.fill.empty()) {
                out << "    fill                " << layer.fill << '\n';
            }
            if (!layer.stroke.empty()) {
                out << "    stroke              " << layer.stroke << '\n';
            }
            if (!layer.background.empty()) {
                out << "    background          " << layer.background << '\n';
            }
        }

        const bool has_layout = !layer.layout.requested_anchor.empty() ||
            !layer.layout.alignment.empty() || layer.layout.x || layer.layout.y ||
            layer.layout.width || layer.layout.height ||
            layer.layout.offset_x || layer.layout.offset_y;
        if (has_layout) {
            out << "\n  Layout\n";
            if (!layer.layout.requested_anchor.empty()) {
                out << "    anchor requested    "
                    << layer.layout.requested_anchor << '\n';
            }
            if (!layer.layout.alignment.empty()) {
                out << "    alignment           " << layer.layout.alignment << '\n';
            }
            if (layer.layout.x) {
                out << "    x                   " << *layer.layout.x << '\n';
            }
            if (layer.layout.y) {
                out << "    y                   " << *layer.layout.y << '\n';
            }
            if (layer.layout.width) {
                out << "    width               " << *layer.layout.width << '\n';
            }
            if (layer.layout.height) {
                out << "    height              " << *layer.layout.height << '\n';
            }
            if (layer.layout.offset_x) {
                out << "    offset_x            " << *layer.layout.offset_x << '\n';
            }
            if (layer.layout.offset_y) {
                out << "    offset_y            " << *layer.layout.offset_y << '\n';
            }
        }

        if (!layer.motion.preset.empty() || !layer.motion.unit.empty() ||
            layer.motion.enter_frames || layer.motion.exit_frames) {
            out << "\n  Motion\n";
            if (!layer.motion.preset.empty()) {
                out << "    preset              " << layer.motion.preset << '\n';
            }
            if (!layer.motion.unit.empty()) {
                out << "    unit                " << layer.motion.unit << '\n';
            }
            if (layer.motion.enter_frames) {
                out << "    enter               " << *layer.motion.enter_frames
                    << " frames\n";
            }
            if (layer.motion.exit_frames) {
                out << "    exit                " << *layer.motion.exit_frames
                    << " frames\n";
            }
        }

        if (!layer.blend_mode.empty()) {
            out << "  blend_mode            " << layer.blend_mode << '\n';
        }
        if (layer.opacity) {
            out << "  opacity               " << *layer.opacity << '\n';
        }
        out << "  loop                  " << (layer.loop ? "yes" : "no") << '\n';
        if (layer.start_frame) {
            out << "  start_frame           " << *layer.start_frame << '\n';
        }
        if (layer.duration_frames) {
            out << "  duration_frames       " << *layer.duration_frames << '\n';
        }
    }
    return out.str();
}

} // namespace

int command_inspect_plan(const InspectPlanArgs& args) {
    RenderPlanPreparationOptions options;
    options.input = args.plan_file;
    options.assets_root = args.assets_root;

    const auto preparation = prepare_render_plan(options);
    if (!preparation) {
        const auto& error = preparation.error();
        if (args.json) {
            nlohmann::json js;
            js["error"] = "PlanDecodeError";
            js["path"] = error.path;
            js["message"] = error.message;
            js["status"] = "FAIL";
            std::cout << js.dump(2) << '\n';
        } else {
            std::cerr << "inspect: " << error.path << ": "
                      << error.message << '\n';
        }
        return 1;
    }

    const auto inspection = build_render_plan_inspection(preparation.value());

    if (args.json) {
        std::cout << inspection_to_json(inspection).dump(2) << '\n';
    } else {
        std::cout << inspection_to_text(inspection);
    }
    return 0;
}

void register_inspect_plan_command(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<InspectPlanArgs>();
    auto* cmd = app.add_subcommand(
        "inspect", "Inspect a chronon.render-plan.v1 file (read-only resolved view)");
    cmd->add_option("--plan", state->plan_file, "Render plan JSON file")->required();
    cmd->add_option("--assets-root", state->assets_root,
                    "Asset root for render-plan resolution");
    cmd->add_flag("--json,!--no-json", state->json,
                  "Emit machine-readable JSON (default: human text)");
    cmd->callback([state, &ctx]() {
        ctx.exit_code = command_inspect_plan(*state);
    });
}

} // namespace chronon3d::cli
