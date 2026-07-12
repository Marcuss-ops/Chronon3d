// SPDX-License-Identifier: MIT
//
// apps/chronon3d_cli/utils/job/render_job_json.cpp
//
// Implementation of parse_render_job_json + looks_like_job_json.
// TICKET-MUSK-TEST-3.

#include "render_job_json.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace chronon3d::cli {

namespace {

// Helper: required string field. Returns std::nullopt + spdlog error on
// missing or type mismatch. §honesty: never invents a value; the caller
// must retry with a corrected job.json.
std::optional<std::string> require_string(const nlohmann::json& j,
                                          const std::string& key) {
    if (!j.contains(key)) {
        spdlog::error("job.json: missing required field '{}'", key);
        return std::nullopt;
    }
    if (!j[key].is_string()) {
        spdlog::error("job.json: field '{}' must be a string", key);
        return std::nullopt;
    }
    return j[key].get<std::string>();
}

// Helper: optional string field with default. Logs a warning on type
// mismatch and falls back to default (transparent recovery, not invent).
std::string optional_string(const nlohmann::json& j,
                            const std::string& key,
                            const std::string& def) {
    if (!j.contains(key)) return def;
    if (!j[key].is_string()) {
        spdlog::warn("job.json: field '{}' is not a string, using default '{}'",
                     key, def);
        return def;
    }
    return j[key].get<std::string>();
}

bool optional_bool(const nlohmann::json& j,
                   const std::string& key,
                   bool def) {
    if (!j.contains(key)) return def;
    if (!j[key].is_boolean()) {
        spdlog::warn("job.json: field '{}' is not a bool, using default '{}'",
                     key, def ? "true" : "false");
        return def;
    }
    return j[key].get<bool>();
}

double optional_number(const nlohmann::json& j,
                       const std::string& key,
                       double def) {
    if (!j.contains(key)) return def;
    if (!j[key].is_number()) {
        spdlog::warn("job.json: field '{}' is not a number, using default '{}'",
                     key, std::to_string(def));
        return def;
    }
    return j[key].get<double>();
}

// Derive the thumbnail output path given the render output path.
// Strategy: <base>.thumbnail.png — beside the render output, never
// overwrites an existing <base>.png.
std::string derive_thumbnail_path(const std::string& render_output) {
    std::filesystem::path p(render_output);
    if (p.has_parent_path()) {
        return (p.parent_path() / (p.stem().string() + ".thumbnail.png")).string();
    }
    return p.stem().string() + ".thumbnail.png";
}

} // namespace

std::optional<RenderJobArgs> parse_render_job_json(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        spdlog::error("job.json: cannot open '{}'", path);
        return std::nullopt;
    }

    nlohmann::json j;
    try {
        in >> j;
    } catch (const nlohmann::json::parse_error& e) {
        spdlog::error("job.json: parse error at byte {}: {}",
                      e.byte, e.what());
        return std::nullopt;
    }

    if (!j.is_object()) {
        spdlog::error("job.json: top-level must be an object");
        return std::nullopt;
    }

    RenderJobArgs out;

    // required fields
    auto comp_id = require_string(j, "comp_id");
    auto frames  = require_string(j, "frames");
    auto output  = require_string(j, "output");
    if (!comp_id || !frames || !output) {
        return std::nullopt;
    }
    out.render_args.comp_id = *comp_id;
    out.render_args.frames  = *frames;
    out.render_args.output  = *output;

    // optional fields with documented defaults
    out.render_args.log_level = optional_string(j, "log_level", "info");
    out.render_args.pipeline.diagnostic = optional_bool(j, "diagnostic", false);
    out.render_args.pipeline.quality.motion_blur =
        optional_bool(j, "motion_blur", false);
    out.render_args.pipeline.quality.ssaa =
        optional_number(j, "ssaa", 1.0);
    out.render_args.report  = optional_bool(j, "report", true);
    out.want_thumbnail      = optional_bool(j, "thumbnail", true);
    out.want_subtitles      = optional_bool(j, "subtitles", false);

    // Derive StillArgs from RenderArgs for the thumbnail path.
    out.still_args.comp_id  = *comp_id;
    out.still_args.output   = derive_thumbnail_path(*output);
    out.still_args.log_level = out.render_args.log_level;
    out.still_args.pipeline = out.render_args.pipeline;
    {
        FrameRange range = parse_frames(*frames);
        out.still_args.frame = range.start;
    }
    // thumbnail skips the asset preflight by default (per-still convention).
    out.still_args.skip_preflight = true;

    // Forward-point: subtitles not implemented in this PR. Logged once.
    if (out.want_subtitles) {
        spdlog::warn("job.json: 'subtitles': true is a forward-point — "
                     "see docs/CHANGELOG.md 'Test #3' for status.");
    }

    return out;
}

bool looks_like_job_json(const std::string& path) noexcept {
    if (path.size() < 5) return false;
    auto ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (ext != ".json") return false;
    std::ifstream in(path);
    return in.good();
}

} // namespace chronon3d::cli
