// SPDX-License-Identifier: MIT
//
// apps/chronon3d_cli/utils/job/render_job_json.hpp
//
// TICKET-MUSK-TEST-3  — JSON adapter for the `render-job <file>.json`
// CLI subcommand.
//
// Layering:
//   * JSON parser → RenderJobArgs (struct below)
//   * command_render_job(registry, RenderJobArgs) drives existing
//     command_render(...) + (optionally) command_still(...).
//
// The adapter layer is intentionally minimal: only required fields
// (`comp_id`, `frames`, `output`) are mandatory. Optional fields with
// documented defaults are pulled out via small helpers in the .cpp.
// Subtitles / video encoding are forward-points; tested elsewhere.

#pragma once

#include "../../commands.hpp"
#include "cli_render_utils.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace chronon3d::cli {

struct RenderJobArgs {
    RenderArgs render_args;       // delegated to command_render(...)
    StillArgs  still_args;        // delegated to command_still(...) when want_thumbnail
    bool       want_thumbnail{true};
    bool       want_report{true};
    bool       want_subtitles{false};   // forward-point (see CHANGELOG Test #3)
};

// Parse a job.json file into a RenderJobArgs.
//
//   * Returns std::nullopt and logs error via spdlog on:
//       - file not readable
//       - malformed JSON
//       - missing required field (comp_id | frames | output)
//       - type mismatch on a required field
//   * On success, the entire RenderJobArgs struct is populated.
//     Optional fields fall back to documented defaults.
std::optional<RenderJobArgs> parse_render_job_json(const std::string& path);

// File-existence + extension sanity check used by the CLI subcommand.
// Returns true if path ends with ".json" (case-insensitive) AND is openable.
bool looks_like_job_json(const std::string& path) noexcept;

} // namespace chronon3d::cli
