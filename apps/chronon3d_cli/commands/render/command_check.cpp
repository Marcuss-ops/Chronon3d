#include "../../cli_context.hpp"

#include "check_audio.hpp"
#include "check_findings.hpp"
#include "check_plan.hpp"
#include "check_video.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace chronon3d::cli {
namespace {

struct CheckArgs {
    std::string input;
    std::string plan;
    std::string assets_root;
    std::string output;
    std::string fail_on{"P1"};
    bool json{false};
    bool quick{false};
    bool skip_audio{false};
    bool skip_video{false};
    check::Thresholds thresholds;
};

/// Plan preparation logs through spdlog. A machine-readable report must not
/// be prefixed by log lines, so JSON/file output silences the logger for the
/// duration of the analysis and restores it on every exit path.
class ScopedLogSilencer {
public:
    explicit ScopedLogSilencer(const bool silence) : active_(silence) {
        if (active_) {
            previous_ = spdlog::get_level();
            spdlog::set_level(spdlog::level::off);
        }
    }
    ~ScopedLogSilencer() {
        if (active_) spdlog::set_level(previous_);
    }
    ScopedLogSilencer(const ScopedLogSilencer&) = delete;
    ScopedLogSilencer& operator=(const ScopedLogSilencer&) = delete;

private:
    bool active_{false};
    spdlog::level::level_enum previous_{spdlog::level::info};
};

void sort_findings(std::vector<check::Finding>& findings) {
    std::sort(findings.begin(), findings.end(),
        [](const check::Finding& a, const check::Finding& b) {
            const int severity_a = static_cast<int>(a.severity);
            const int severity_b = static_cast<int>(b.severity);
            if (severity_a != severity_b) return severity_a < severity_b;
            const int frame_a = a.first_frame.value_or(-1);
            const int frame_b = b.first_frame.value_or(-1);
            if (frame_a != frame_b) return frame_a < frame_b;
            return a.id < b.id;
        });
}

void emit_error(const CheckArgs& args, const std::string& message,
                CliContext& ctx) {
    if (args.json) {
        nlohmann::json error = {
            {"schema", "chronon.check.v1"},
            {"status", "ERROR"},
            {"input", args.input},
            {"plan", args.plan},
            {"error", message},
        };
        std::cout << error.dump(2) << '\n';
    } else {
        spdlog::error("check: {}", message);
    }
    ctx.exit_code = 2;
}

bool write_report_file(const std::string& path, const std::string& payload,
                       const CheckArgs& args, CliContext& ctx) {
    const std::filesystem::path output_path(path);
    if (output_path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(output_path.parent_path(), error);
        if (error) {
            emit_error(args,
                "cannot create report directory '" +
                    output_path.parent_path().string() + "': " + error.message(),
                ctx);
            return false;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        emit_error(args, "cannot open check report: " + path, ctx);
        return false;
    }
    output << payload;
    if (!output.good()) {
        emit_error(args, "cannot write check report: " + path, ctx);
        return false;
    }
    return true;
}

} // namespace

void register_check_command(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<CheckArgs>();
    auto* command = app.add_subcommand(
        "check",
        "Check a rendered video/image and a RenderPlan for delivery defects");
    command->add_option("input", state->input,
                        "Rendered video or image to check");
    command->add_option("--plan", state->plan,
                        "RenderPlan JSON to check for text/geometry defects");
    command->add_option("--assets-root", state->assets_root,
                        "Asset root used to resolve the RenderPlan fonts/assets");
    command->add_option("-o,--output", state->output,
                        "Write the machine-readable report to this JSON path");
    command->add_option("--fail-on", state->fail_on,
                        "Severity that makes the command fail: P0, P1, P2, P3")
        ->default_val("P1")
        ->check(CLI::IsMember({"P0", "P1", "P2", "P3"}));
    command->add_flag("--json", state->json,
                      "Emit the machine-readable report on stdout");
    command->add_flag("--quick", state->quick,
                      "Sample at most ~90 video frames instead of every frame");
    command->add_flag("--no-audio", state->skip_audio,
                      "Skip audio loudness/peak checks");
    command->add_flag("--no-video", state->skip_video,
                      "Skip video/image checks (use with --plan)");
    command->add_option("--black-seconds", state->thresholds.black_seconds,
                        "Black interval duration that becomes a finding")
        ->default_val(0.5)
        ->check(CLI::Range(0.0, 3600.0));
    command->add_option("--freeze-seconds", state->thresholds.freeze_seconds,
                        "Frozen interval duration that becomes a finding")
        ->default_val(1.0)
        ->check(CLI::Range(0.0, 3600.0));
    command->add_option("--flash-max-per-second",
                        state->thresholds.flash_max_per_second,
                        "Luminance transitions per second accepted before a flash finding")
        ->default_val(3)
        ->check(CLI::Range(0, 100));
    command->add_option("--max-clipped-fraction",
                        state->thresholds.max_clipped_fraction,
                        "Opaque-pixel highlight clipping fraction accepted per frame")
        ->default_val(0.02)
        ->check(CLI::Range(0.0, 1.0));
    command->add_option("--min-font-px", state->thresholds.min_font_px,
                        "Readable font-size floor at the reference canvas height")
        ->default_val(20.0)
        ->check(CLI::Range(1.0, 4096.0));
    command->add_option("--min-font-reference-height",
                        state->thresholds.min_font_reference_height,
                        "Canvas height the font-size floor is scaled from")
        ->default_val(1080.0)
        ->check(CLI::Range(1.0, 16384.0));
    command->add_option("--loudness-target", state->thresholds.loudness_target_lufs,
                        "Integrated loudness delivery target in LUFS")
        ->default_val(-14.0)
        ->check(CLI::Range(-70.0, 0.0));
    command->add_option("--loudness-tolerance",
                        state->thresholds.loudness_tolerance_lu,
                        "Allowed distance from the loudness target in LU")
        ->default_val(2.0)
        ->check(CLI::Range(0.0, 30.0));
    command->add_option("--max-peak-dbfs", state->thresholds.max_peak_dbfs,
                        "Sample peak ceiling in dBFS")
        ->default_val(-1.0)
        ->check(CLI::Range(-120.0, 0.0));
    command->add_option("--silence-lufs", state->thresholds.silence_lufs,
                        "Integrated loudness below which the track is silent")
        ->default_val(-60.0)
        ->check(CLI::Range(-120.0, 0.0));

    command->callback([state, &ctx]() {
        const auto& args = *state;
        const ScopedLogSilencer silence_logs(args.json || !args.output.empty());
        if (args.input.empty() && args.plan.empty()) {
            emit_error(args, "provide an input video/image or --plan <file>", ctx);
            return;
        }
        const auto fail_on = check::parse_severity(args.fail_on);
        if (!fail_on) {
            emit_error(args, "invalid --fail-on severity", ctx);
            return;
        }

        check::Report report;
        report.input = args.input;
        report.plan = args.plan;
        report.thresholds = args.thresholds;
        report.thresholds.fail_on = *fail_on;

        bool checked_anything = false;

        if (!args.input.empty() && !args.skip_video) {
            const auto scan = check::scan_video(args.input, report.thresholds,
                                                args.quick);
            if (!scan.scan) {
                emit_error(args, scan.error, ctx);
                return;
            }
            const auto findings = check::analyze_video(*scan.scan,
                                                       report.thresholds);
            for (const auto& finding : findings) {
                check::add_finding(report, finding);
            }
            check::add_check_run(report, scan.scan->is_video ? "video" : "image");
            if (scan.scan->quick) {
                report.notes.push_back(fmt::format(
                    "quick scan: {} of {} frame(s), step {}",
                    scan.scan->sampled_frames, scan.scan->total_frames,
                    scan.scan->sample_step));
            }
            checked_anything = true;

            if (scan.scan->is_video && !args.skip_audio) {
                const auto audio = check::analyze_audio(args.input);
                if (!audio.has_audio_stream) {
                    check::add_check_not_measured(report,
                        "audio: no audio stream");
                } else {
                    const auto audio_findings =
                        check::analyze_audio_metrics(audio, report.thresholds);
                    for (const auto& finding : audio_findings) {
                        check::add_finding(report, finding);
                    }
                    if (audio.measured) {
                        check::add_check_run(report, "audio");
                    } else {
                        check::add_check_not_measured(
                            report, "audio: " + audio.error);
                    }
                }
            }
        }

        if (!args.plan.empty()) {
            const auto plan = check::scan_plan(args.plan, args.assets_root);
            if (!plan.facts) {
                emit_error(args, "could not prepare render plan: " + plan.error,
                           ctx);
                return;
            }
            const auto findings = check::analyze_plan(*plan.facts,
                                                      report.thresholds);
            for (const auto& finding : findings) {
                check::add_finding(report, finding);
            }
            check::add_check_run(report, "plan");
            checked_anything = true;
        }

        if (!checked_anything) {
            emit_error(args, "no check was selected", ctx);
            return;
        }

        sort_findings(report.findings);
        const auto json = check::report_to_json(report);
        if (!args.output.empty()) {
            if (!write_report_file(args.output, json.dump(2) + "\n", args, ctx)) {
                return;
            }
        }
        if (args.json || args.output.empty()) {
            if (args.json) std::cout << json.dump(2) << '\n';
            else std::cout << check::report_to_text(report);
        } else {
            std::cout << check::report_to_text(report);
        }
        ctx.exit_code = check::exit_code(report);
    });
}

} // namespace chronon3d::cli
