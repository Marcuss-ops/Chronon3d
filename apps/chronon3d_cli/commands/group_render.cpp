// ==============================================================================
// CLI Group: Render
// Contains: render, validate, preflight, proofs, bake_layer, graph
// ==============================================================================
#include "command_registry.hpp"
#include "commands.hpp"
#include "render/render_profiles.hpp"
#include "../utils/common/cli_asset_preflight_utils.hpp"
#include "../utils/common/props_file.hpp"
#include "../utils/job/cli_render_utils.hpp"

#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/assets/asset_preflight_resolver.hpp>
#include <chronon3d/assets/render_preflight.hpp>

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace chronon3d::cli {
namespace {

struct ValidateState {
    std::string comp_id;
    std::string props_file;
    std::string output;
    std::string profile{"production"};
};

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

const char* prop_type_name(PropType type) {
    switch (type) {
        case PropType::String:  return "string";
        case PropType::Integer: return "integer";
        case PropType::Float:   return "float";
        case PropType::Boolean: return "boolean";
        case PropType::Enum:    return "enum";
        case PropType::Color:   return "color";
        case PropType::Path:    return "path";
    }
    return "unknown";
}

bool validate_schema_surface(const CompositionDescriptor& descriptor,
                             const PropsFileResult& loaded) {
    if (!descriptor.schema) return true;

    bool valid = true;
    std::unordered_set<std::string> declared;
    declared.reserve(descriptor.schema->fields.size());

    for (const auto& field : descriptor.schema->fields) {
        declared.insert(field.name);
        if (field.required && !field.default_value &&
            !loaded.props.values.contains(field.name)) {
            spdlog::error("Missing required prop '{}' ({})",
                          field.name, prop_type_name(field.type));
            valid = false;
        }
    }

    for (const auto& key : loaded.keys) {
        if (!declared.contains(key)) {
            spdlog::error("Unknown prop '{}' for composition '{}'", key, descriptor.id);
            valid = false;
        }
    }
    return valid;
}

void print_props_error(const std::string& composition_id,
                       const PropsError& error) {
    const std::string key = error.key.empty()
        ? std::string{}
        : " [" + error.key + "]";
    spdlog::error("Props validation failed for '{}'{}: {}",
                  composition_id, key, error.message);
}

int run_validate(CliContext& ctx, const ValidateState& args) {
    const auto descriptor = ctx.registry.descriptor_of(args.comp_id);
    if (!descriptor) {
        spdlog::error("Unknown composition: {}", args.comp_id);
        return 1;
    }
    fmt::print("[1/6] composition exists: {}\n", descriptor->id);

    auto loaded = load_props_file(args.props_file);
    if (!loaded.ok) {
        spdlog::error("{}", loaded.error);
        return 1;
    }
    loaded.props.assets = &ctx.assets;
    if (!validate_schema_surface(*descriptor, loaded)) return 1;

    auto prepared_result = descriptor->prepare_props(loaded.props);
    if (!prepared_result) {
        print_props_error(args.comp_id, prepared_result.error());
        return 1;
    }
    PreparedComposition prepared = std::move(prepared_result).value();
    if (!prepared.construct) {
        spdlog::error("Composition '{}' preparation returned no constructor",
                      args.comp_id);
        return 1;
    }
    fmt::print("[2/6] props decoded: {} value(s)\n", loaded.props.values.size());
    fmt::print("[3/6] props valid\n");

    std::optional<Composition> composition;
    const auto ensure_composition = [&]() -> Composition& {
        if (!composition) {
            composition.emplace(prepared.construct());
        }
        return *composition;
    };

    std::optional<CompositionMetadata> resolved_metadata = prepared.metadata;
    if (!resolved_metadata && descriptor->width && descriptor->height &&
        descriptor->fps && descriptor->duration) {
        resolved_metadata = CompositionMetadata{
            *descriptor->width,
            *descriptor->height,
            *descriptor->fps,
            *descriptor->duration
        };
    }

    try {
        if (!resolved_metadata) {
            const Composition& value = ensure_composition();
            resolved_metadata = CompositionMetadata{
                value.width(),
                value.height(),
                value.frame_rate(),
                value.duration()
            };
        }
    } catch (const std::exception& error) {
        spdlog::error("Composition construction failed for '{}': {}",
                      args.comp_id, error.what());
        return 1;
    }

    const FrameRate rate = resolved_metadata->fps;
    const i32 width = resolved_metadata->width;
    const i32 height = resolved_metadata->height;
    const Frame duration = resolved_metadata->duration;
    fmt::print("[4/6] metadata: {}x{}  {}/{} fps  {} frames\n",
               width, height, rate.numerator, rate.denominator,
               duration.integral());

    assets::AssetManifest manifest;
    std::filesystem::path assets_root;
    bool used_declared_manifest = false;

    try {
        if (prepared.asset_manifest) {
            manifest = std::move(*prepared.asset_manifest);
            used_declared_manifest = true;
            if (prepared.assets_root) {
                assets_root = *prepared.assets_root;
            } else {
                assets_root = ensure_composition().assets_root();
            }
        } else {
            Composition& value = ensure_composition();
            assets_root = value.assets_root();
            const Frame last = duration > Frame{0} ? duration - Frame{1} : Frame{0};
            for (Frame frame = Frame{0}; frame <= last; frame += Frame{1}) {
                const FrameContext frame_context{
                    .frame = frame,
                    .local_frame = frame,
                    .frame_time = 0.0f,
                    .duration = duration,
                    .frame_rate = rate,
                    .width = width,
                    .height = height,
                    .assets_root = assets_root,
                    .assets = &ctx.assets
                };
                manifest.merge(value.evaluate(frame_context).asset_manifest());
            }
        }
    } catch (const std::exception& error) {
        spdlog::error("Asset manifest resolution failed: {}", error.what());
        return 1;
    }

    auto resolver = make_cli_resolver(assets_root);
    auto asset_result = AssetPreflightResolver::check_manifest(manifest, resolver);
    std::vector<PreflightIssue> issues = std::move(asset_result.issues);
    fmt::print("[5/6] asset manifest/preflight: {} asset(s), {} issue(s){}\n",
               manifest.assets().size(), issues.size(),
               used_declared_manifest ? " (declared)" : " (evaluated fallback)");

    RenderArgs settings_args;
    settings_args.comp_id = args.comp_id;
    apply_render_profile(settings_args, args.profile,
                         [](std::string_view) { return false; });
    const auto settings = settings_from_args(
        settings_args, true, settings_args.pipeline.diagnostic);
    (void)settings;

    if (!args.output.empty()) {
        auto& output_preflight = RenderPreflight::instance();
        output_preflight.clear();
        output_preflight.require_output_path(args.output);
        auto output_issues = output_preflight.validate(ctx.assets, resolver);
        output_preflight.clear();
        issues.insert(issues.end(), output_issues.begin(), output_issues.end());
    }
    fmt::print("[6/6] output/settings valid: profile={}{}\n",
               args.profile,
               args.output.empty()
                   ? std::string{}
                   : fmt::format(" output={}", args.output));

    if (!issues.empty()) {
        const auto text = format_preflight_issues_text(issues);
        if (has_preflight_errors(issues)) {
            fmt::print(stderr, "{}", text);
            return 1;
        }
        fmt::print("{}", text);
    }

    fmt::print("VALIDATION OK — no frame was rendered.\n");
    return 0;
}

} // namespace

void register_validate_commands(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<ValidateState>();
    auto* command = app.add_subcommand(
        "validate",
        "Validate composition props, metadata, assets and output settings without rendering");
    command->add_option("input", state->comp_id, "Composition name")->required();
    command->add_option("--props-file", state->props_file,
                        "Flat JSON object containing composition props");
    command->add_option("-o,--output", state->output,
                        "Optional output path to validate");
    command->add_option("--profile", state->profile,
                        "Render settings profile: draft | preview | production | maximum")
        ->default_val("production")
        ->transform([](std::string value) { return lower_copy(std::move(value)); })
        ->check(CLI::IsMember(
            std::set<std::string>{"draft", "preview", "production", "maximum"},
            CLI::ignore_case));
    command->callback([state, &ctx]() {
        try {
            ctx.exit_code = run_validate(ctx, *state);
        } catch (const std::exception& error) {
            spdlog::error("Validation failed: {}", error.what());
            ctx.exit_code = 1;
        }
    });
}

} // namespace chronon3d::cli

namespace chronon3d::cli::group_render {
void register_commands(CLI::App& app, CliContext& ctx) {
    register_render_commands(app, ctx);
    register_validate_commands(app, ctx);
    register_bake_layer_commands(app, ctx);
}
} // namespace chronon3d::cli::group_render
