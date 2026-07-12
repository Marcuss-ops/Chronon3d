// SPDX-License-Identifier: MIT
//
// apps/chronon3d_cli/commands/render/command_render_job.cpp
//
// TICKET-MUSK-TEST-3  — `chronon3d render-job job.json` driver.
//
// Parses the JSON file via parse_render_job_json() (utility in
// utils/job/render_job_json.{hpp,cpp}) and delegates to existing
// command_render(...) + (when wants_thumbnail) command_still(...).
//
// Output artifacts:
//   * render_args.output         (frame sequence, delegated)
//   * still_args.output          (thumbnail, when want_thumbnail)
//   * render report                (delegated to command_render's --report)
//
// Forward-points (NOT in this PR):
//   * subtitles                  (logged warning in parser; render
//                                 unification deferred to follow-up)
//   * video encoding             (delegated via chronon3d_cli_video's
//                                 `video` subcommand in a follow-up)

#include "../../commands.hpp"
#include "../../utils/job/render_job_json.hpp"

#include <spdlog/spdlog.h>

namespace chronon3d::cli {

int command_render_job(const CompositionRegistry& registry,
                       const RenderJobArgs& args) {
    spdlog::info("render-job: rendering '{}' → '{}'",
                 args.render_args.comp_id, args.render_args.output);

    const int rc_render = command_render(registry, args.render_args);
    if (rc_render != 0) {
        spdlog::error("render-job: render phase failed (rc={})", rc_render);
        return rc_render;
    }

    if (args.want_thumbnail) {
        spdlog::info("render-job: generating thumbnail → '{}'",
                     args.still_args.output);
        const int rc_still = command_still(registry, args.still_args);
        if (rc_still != 0) {
            spdlog::warn("render-job: thumbnail generation failed (rc={}); "
                         "continuing", rc_still);
        }
    }

    spdlog::info("render-job: done (rc={})", rc_render);
    return rc_render;
}

} // namespace chronon3d::cli
