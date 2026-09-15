#include "render_plan_preparation.hpp"

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/compiler/compiler.hpp>
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
#include <unordered_set>
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

PreparedOverlayPackageSummary read_prepared_package(
    const std::string& path, const std::string& plan_id) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open prepared overlay package: " + path);
    }
    nlohmann::json package;
    try {
        input >> package;
    } catch (const std::exception& error) {
        throw std::runtime_error(
            "cannot parse prepared overlay package " + path + ": " + error.what());
    }
    if (!package.is_object()) {
        throw std::runtime_error("prepared overlay package must be a JSON object: " + path);
    }

    const auto package_plan_id = package.value("plan_id", std::string{});
    const auto content_hash = package.value("content_hash", std::string{});
    if (package_plan_id.empty() || content_hash.empty()) {
        throw std::runtime_error(
            "prepared overlay package requires plan_id and content_hash: " + path);
    }
    if (!plan_id.empty() && package_plan_id != plan_id) {
        throw std::runtime_error(
            "prepared overlay package plan_id does not match render plan: " + path);
    }

    const auto& text_runs = package.value("text_runs", nlohmann::json::array());
    const auto& assets = package.value("assets", nlohmann::json::array());
    const auto& overlays = package.value("overlays", nlohmann::json::array());
    if (!text_runs.is_array() || !assets.is_array() || !overlays.is_array()) {
        throw std::runtime_error(
            "prepared overlay package text_runs/assets/overlays must be arrays: " + path);
    }

    std::unordered_set<std::string> text_keys;
    for (const auto& text : text_runs) {
        const auto key = text.value("key", std::string{});
        const auto value = text.value("text", std::string{});
        if (key.empty() || value.empty() || !text_keys.insert(key).second) {
            throw std::runtime_error(
                "prepared overlay package contains an invalid or duplicate text key: " + path);
        }
    }

    std::unordered_set<std::string> asset_keys;
    for (const auto& asset : assets) {
        const auto key = asset.value("key", std::string{});
        const auto hash = asset.value("content_hash", std::string{});
        const auto logical_path = asset.value("logical_path", std::string{});
        if (key.empty() || hash.empty() || logical_path.empty() ||
            !asset_keys.insert(key).second) {
            throw std::runtime_error(
                "prepared overlay package contains an invalid or duplicate asset key: " + path);
        }
    }

    for (const auto& overlay : overlays) {
        const auto id = overlay.value("id", std::string{});
        const auto kind = overlay.value("kind", std::string{});
        if (id.empty()) {
            throw std::runtime_error(
                "prepared overlay package contains an overlay without an id: " + path);
        }
        if (kind == "text") {
            if (!text_keys.contains(overlay.value("text_key", std::string{}))) {
                throw std::runtime_error(
                    "prepared overlay package text overlay references an unknown text key: " + path);
            }
        } else if (kind == "image" || kind == "video") {
            if (!asset_keys.contains(overlay.value("asset_key", std::string{}))) {
                throw std::runtime_error(
                    "prepared overlay package media overlay references an unknown asset key: " + path);
            }
        }
    }

    return PreparedOverlayPackageSummary{
        path,
        package_plan_id,
        package.value("language", std::string{}),
        content_hash,
        assets.size(),
        text_runs.size(),
        overlays.size()};
}

/// Map the CLI's effective render settings into the backend-neutral fingerprint
/// identity.  Kept in one place so render, validate and future inspect all
/// derive the SAME request/content digest for a given plan + settings.
///
/// The plan is not a parameter: canvas-derived dimensions are the identity's
/// own rule, applied by render_plan::compute_render_job_fingerprint.
void apply_fingerprint_settings(render_plan::RenderPlanFingerprintOptions& fingerprint,
                                const RenderSettings& settings) {
    // width/height are deliberately NOT set here: a zero dimension means
    // "inherit the canvas", and that rule is part of the job identity, owned by
    // render_plan::compute_render_job_fingerprint (audit FP-1). Setting them
    // here as well would be a third copy of the same policy.
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
        std::optional<PreparedOverlayPackageSummary> prepared_package;
        if (!options.prepared_package.empty()) {
            prepared_package = read_prepared_package(
                options.prepared_package, plan.job_id);
            spdlog::info(
                "[overlay-prepare] validated package={} hash={} assets={} text_runs={} overlays={}",
                prepared_package->path, prepared_package->content_hash,
                prepared_package->asset_count, prepared_package->text_run_count,
                prepared_package->overlay_count);
        }
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
        apply_fingerprint_settings(fingerprint_options, settings);

        const auto compile_t0 = profiling::now();
        compiler::CompileOptions compile_options;
        compile_options.fingerprint = fingerprint_options;
        auto compiled = compiler::compile(plan, resolver, compile_options);
        if (!compiled) {
            const auto error = std::move(compiled).error();
            return render_plan::PlanDecodeError{error.path, error.message, "compile_error", error.component};
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
        context.prepared_package = std::move(prepared_package);
        context.program = std::make_shared<const compiler::CompiledProgram>(
            std::move(compiled).value());
		// The numeric compiler is the canonical plan authority. The current
		// FFmpeg video exporter still accepts CompiledComposition, so create its
		// temporary compatibility view from the already compiled program. This
		// call performs no second compilation.
		if (context.decoded.output.format != render_plan::OutputFormat::Png) {
			auto compatibility = render_plan::make_video_compatibility_view(
				context.decoded, resolver,
				context.program->metadata().asset_manifest,
				*context.program, fingerprint_options);
			if (!compatibility) return std::move(compatibility).error();
			context.video_compatibility_view = std::make_shared<const CompiledComposition>(
				std::move(compatibility).value());
		}
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
    const auto& meta = context.program->metadata();
    const auto& fp = context.program->fingerprint_data();
    js["job_id"]          = meta.job_id;
    js["content_digest"]  = fp.content_digest.hex();
    js["request_digest"]  = fp.request_digest.hex();
    js["width"]           = meta.width;
    js["height"]          = meta.height;
    js["fps_num"]         = meta.fps_numerator;
    js["fps_den"]         = meta.fps_denominator;
    js["duration_frames"] = meta.duration_frames;
    js["layers"]          = context.decoded.layers.size();
    js["status"]          = "PASS";
    std::cout << js.dump(2) << "\n";
    return 0;
}

} // namespace chronon3d::cli
