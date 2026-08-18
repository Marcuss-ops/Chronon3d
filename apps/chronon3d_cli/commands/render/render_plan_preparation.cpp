#include "render_plan_preparation.hpp"

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>  // is_motion_blur_active

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace chronon3d::cli {

namespace {

std::string read_plan_source(const RenderPlanPreparationOptions& options) {
    if (!options.json.empty()) {
        return options.json;
    }
    std::ifstream input(options.input);
    if (!input) {
        throw std::runtime_error("cannot open render plan: " + options.input);
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

/// Map the effective render settings into the backend-neutral fingerprint
/// identity.  Kept in one place so render, validate and future inspect all
/// derive the SAME request/content digest for a given plan + settings.
void apply_fingerprint_settings(render_plan::RenderPlanFingerprintOptions& fingerprint,
                                const RenderSettings& settings,
                                const render_plan::RenderPlan& plan) {
    fingerprint.render_settings.width = plan.canvas.width;
    fingerprint.render_settings.height = plan.canvas.height;
    fingerprint.render_settings.ssaa_factor = settings.ssaa_factor;
    fingerprint.render_settings.motion_blur =
        chronon3d::is_motion_blur_active(settings.motion_blur);
    fingerprint.render_settings.dirty_rects = settings.dirty.enabled;
    fingerprint.render_settings.dirty_bitmask = settings.dirty.use_bitmask;
    fingerprint.render_settings.dirty_tiles = settings.dirty.use_tiles;
    fingerprint.render_settings.parallel_tiles = settings.dirty.parallel_tiles;
    fingerprint.render_settings.tile_size = settings.dirty.tile_size;
    fingerprint.render_settings.tile_dirty_ratio_threshold =
        settings.dirty.tile_dirty_ratio_threshold;
    fingerprint.render_settings.optimize_compositing =
        settings.compositing.optimize_compositing;
    fingerprint.render_settings.deterministic =
        settings.force_scalar_normal_blend;
    fingerprint.render_settings.force_scalar_normal_blend =
        settings.force_scalar_normal_blend;
}

} // namespace

Result<PreparedRenderPlanContext, render_plan::PlanDecodeError>
prepare_render_plan(const RenderPlanPreparationOptions& options) {
    try {
        nlohmann::json root;
        try {
            root = nlohmann::json::parse(read_plan_source(options));
        } catch (const std::exception& error) {
            return render_plan::PlanDecodeError{options.input, error.what()};
        }

        auto decoded = render_plan::decode_render_plan(root);
        if (!decoded) {
            return std::move(decoded).error();
        }

        const std::string effective_assets_root = !options.assets_root.empty()
            ? options.assets_root
            : (std::getenv("CHRONON3D_CLI_ASSETS_ROOT")
                ? std::getenv("CHRONON3D_CLI_ASSETS_ROOT") : "");

        auto plan = std::move(decoded).value();
        // Explicit plan output.crf overrides the engine default when the plan
        // document carries it (the decoder defaults an absent crf to 0, so
        // presence is checked on the raw document, never the decoded value).
        const auto& output_obj = root.value("output", nlohmann::json::object());
        if (output_obj.contains("crf") && output_obj.at("crf").is_number_integer()) {
            plan.output.crf = output_obj.at("crf").get<int>();
        }

        chronon3d::assets::AssetResolver resolver;
        if (!effective_assets_root.empty()) {
            resolver.mount(std::filesystem::path{effective_assets_root});
        }

        RenderSettings settings;
        settings.fail_on_missing_assets = true;
        render_plan::RenderPlanFingerprintOptions fingerprint_options;
        apply_fingerprint_settings(fingerprint_options, settings, plan);

        auto compiled = render_plan::compile_render_plan(
            plan, resolver, fingerprint_options);
        if (!compiled) {
            return std::move(compiled).error();
        }

        PreparedRenderPlanContext context;
        context.decoded = std::move(plan);
        context.prepared = std::move(compiled).value();
        context.resolver = std::move(resolver);
        context.effective_assets_root = effective_assets_root;
        context.settings = settings;
        return context;
    } catch (const std::exception& error) {
        return render_plan::PlanDecodeError{options.input, error.what()};
    }
}

int validate_render_plan(const RenderPlanPreparationOptions& options) {
    const auto result = prepare_render_plan(options);
    if (!result) {
        nlohmann::json js;
        js["valid"]   = false;
        js["plan"]    = options.input;
        js["error"]   = "PlanDecodeError";
        js["path"]    = result.error().path;
        js["message"] = result.error().message;
        js["status"]  = "FAIL";
        std::cout << js.dump(2) << "\n";
        return 1;
    }

    const auto& context = result.value();
    nlohmann::json js;
    js["valid"]           = true;
    js["plan"]            = options.input;
    js["job_id"]          = context.prepared.job_id;
    js["content_digest"]  = context.prepared.fingerprint.content_digest.hex();
    js["request_digest"]  = context.prepared.fingerprint.request_digest.hex();
    js["width"]           = context.prepared.canvas.width;
    js["height"]          = context.prepared.canvas.height;
    js["fps"]             = context.prepared.canvas.fps;
    js["duration_frames"] = context.prepared.canvas.duration.integral();
    js["layers"]          = context.decoded.layers.size();
    js["status"]          = "PASS";
    std::cout << js.dump(2) << "\n";
    return 0;
}

} // namespace chronon3d::cli
