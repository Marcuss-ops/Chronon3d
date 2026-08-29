#include "render_plan_preparation.hpp"

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>  // is_motion_blur_active
#include <chronon3d/core/profiling/profiling.hpp>
#include "../../utils/process_start.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <spdlog/spdlog.h>

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
        const auto preparation_t0 = profiling::now();
        const auto read_t0 = preparation_t0;
        const auto source = read_plan_source(options);
        const double read_ms = profiling::duration_ms(read_t0, profiling::now());
        startup_trace().plan_read_ms = read_ms;

        nlohmann::json root;
        const auto parse_t0 = profiling::now();
        try {
            root = nlohmann::json::parse(source);
        } catch (const std::exception& error) {
            return render_plan::PlanDecodeError{options.input, error.what()};
        }
        const double parse_ms = profiling::duration_ms(parse_t0, profiling::now());
        startup_trace().plan_json_parse_ms = parse_ms;

        auto plan_json = root;
        if (root.is_object() && root.value("schema", "") == "renderinggen.job" && root.contains("render_plan")) {
            plan_json = root.at("render_plan");
        }

        const auto decode_t0 = profiling::now();
        auto decoded = render_plan::decode_render_plan(plan_json);
        if (!decoded) {
            return std::move(decoded).error();
        }
        const double decode_ms = profiling::duration_ms(decode_t0, profiling::now());
        startup_trace().plan_decode_validate_ms = decode_ms;

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

        const auto resolver_t0 = profiling::now();
        chronon3d::assets::AssetResolver resolver;
        if (!effective_assets_root.empty()) {
            resolver.mount(std::filesystem::path{effective_assets_root});
        }
        const double resolver_ms = profiling::duration_ms(resolver_t0, profiling::now());
        startup_trace().plan_asset_resolve_ms = resolver_ms;

        RenderSettings settings;
        settings.fail_on_missing_assets = true;
        render_plan::RenderPlanFingerprintOptions fingerprint_options;
        apply_fingerprint_settings(fingerprint_options, settings, plan);

        const auto compile_t0 = profiling::now();
        auto compiled = render_plan::compile_render_plan(
            plan, resolver, fingerprint_options);
        if (!compiled) {
            return std::move(compiled).error();
        }
        const double compile_ms = profiling::duration_ms(compile_t0, profiling::now());
        startup_trace().plan_compile_ms = compile_ms;
        const double total_ms = profiling::duration_ms(preparation_t0, profiling::now());
        spdlog::info(
            "[plan-profile] read={:.2f}ms parse={:.2f}ms decode={:.2f}ms "
            "resolver={:.2f}ms compile_validate={:.2f}ms total={:.2f}ms",
            read_ms, parse_ms, decode_ms, resolver_ms, compile_ms, total_ms);

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
    js["fps_num"]         = context.prepared.canvas.fps.num();
    js["fps_den"]         = context.prepared.canvas.fps.den();
    js["duration_frames"] = context.prepared.canvas.duration.integral();
    js["layers"]          = context.decoded.layers.size();
    js["status"]          = "PASS";
    std::cout << js.dump(2) << "\n";
    return 0;
}

} // namespace chronon3d::cli
