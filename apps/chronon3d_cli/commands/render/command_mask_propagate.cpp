#include "../../cli_context.hpp"
#include "command_mask_propagate_detail.hpp"
#include "command_mask_propagate_state.hpp"
#include "command_mask_propagate_state.hpp"

#include <chronon3d/backends/image/image_loader.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <chronon3d/effects/keying_matte.hpp>
#include <chronon3d/media/decode_result.hpp>
#include <chronon3d/media/tracking/temporal_mask_propagator.hpp>
#include <chronon3d/media/video/media_probe.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/video_sink_factory.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace chronon3d::cli {
namespace {

using media::tracking::PropagatedMask;
using media::tracking::SoftMaskView;
using media::tracking::TemporalFlowOptions;
using media::tracking::TemporalMaskPropagator;
using media::tracking::TrackingFrameView;





TrackingFrameView view_of(const Framebuffer& frame) {
    const auto count = static_cast<std::size_t>(frame.allocated_width()) *
                       static_cast<std::size_t>(frame.height());
    return {std::span<const Color>(frame.data(), count),
            static_cast<std::uint32_t>(frame.width()),
            static_cast<std::uint32_t>(frame.height()),
            static_cast<std::uint32_t>(frame.allocated_width()), true};
}

SoftMaskView view_of(const std::vector<float>& alpha,
                     std::uint32_t width, std::uint32_t height) {
    return {std::span<const float>(alpha), width, height, width};
}



void publish_staged_file(const std::string& staged_path,
                         const std::string& output_path) {
    const std::filesystem::path destination(output_path);
    if (destination.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
            throw std::runtime_error("could not create matte output directory: " +
                                     error.message());
    }
    const auto temporary = output_path + ".chronon-tmp.png";
    std::error_code error;
    std::filesystem::copy_file(staged_path, temporary,
        std::filesystem::copy_options::overwrite_existing, error);
    if (error)
        throw std::runtime_error("could not stage matte for publication: " +
                                 error.message());
    std::filesystem::rename(temporary, output_path, error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("could not publish matte output: " + error.message());
    }
}

void write_json_report_atomic(const std::string& report_path,
                              const nlohmann::json& report) {
    const std::filesystem::path destination(report_path);
    if (destination.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
            throw std::runtime_error("could not create report directory: " +
                                     error.message());
    }
    const auto temporary = report_path + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("could not open report staging file: " + temporary);
        output << report.dump(2) << '\n';
        if (!output.good()) {
            output.close();
            std::filesystem::remove(temporary);
            throw std::runtime_error("could not write report staging file: " + temporary);
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, report_path, error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("could not publish confidence report: " +
                                 error.message());
    }
}

PropagatedMask seed_result(const std::vector<float>& alpha,
                           std::uint32_t width, std::uint32_t height) {
    PropagatedMask result;
    result.width = width;
    result.height = height;
    result.alpha = alpha;
    result.confidence.assign(alpha.size(), 1.0f);
    return result;
}

int propagate(const MaskPropagationState& state, float minimum_confidence) {
    mask_propagation_detail::validate_plan_patch_option_set(state);
    if (state.frames.empty() || state.seed_frame >= state.frames.size())
        throw std::runtime_error("seed-frame index is outside the input sequence");
    if (state.frames.size() > 10'000)
        throw std::runtime_error("at most 10000 frames per propagation job are allowed");
    if (std::filesystem::path(state.output_pattern).extension() != ".png")
        throw std::runtime_error("mask output pattern must end in .png");

    auto seed_frame = mask_propagation_detail::load_tracking_frame(state.frames[state.seed_frame], state.input_transfer);

    const auto width = static_cast<std::uint32_t>(seed_frame->width());
    const auto height = static_cast<std::uint32_t>(seed_frame->height());
    if (static_cast<std::uint64_t>(width) * height > 8'000'000)
        throw std::runtime_error("frame size exceeds the 8-megapixel CPU tracking limit");
    TemporaryMaskSpool spool;

    const auto read_mask = [&](const std::string& path) {
        auto image = mask_propagation_detail::load_frame(path);
        if (image->width() != seed_frame->width() ||
            image->height() != seed_frame->height())
            throw std::runtime_error("seed and correction masks must match the video frame dimensions: " + path);
        bool has_transparency = false;
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x)
                has_transparency |= image->get_pixel(static_cast<i32>(x),
                                                      static_cast<i32>(y)).a < 0.999f;
        std::vector<float> alpha(static_cast<std::size_t>(width) * height);
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const auto pixel = image->get_pixel(static_cast<i32>(x),
                                                     static_cast<i32>(y));
                const float coverage = has_transparency
                    ? pixel.a
                    : 0.2126f * pixel.r + 0.7152f * pixel.g + 0.0722f * pixel.b;
                alpha[static_cast<std::size_t>(y) * width + x] =
                    std::clamp(coverage, 0.0f, 1.0f);
            }
        }
        return alpha;
    };
    std::map<std::size_t, std::string> anchors{{state.seed_frame, state.seed_mask}};
    for (const auto& specification : state.correction_masks) {
        const auto separator = specification.find('=');
        if (separator == std::string::npos || separator == 0 || separator + 1 >= specification.size())
            throw std::runtime_error("correction mask must use FRAME=PATH syntax: " + specification);
        std::size_t frame_index = 0;
        try {
            const auto frame_text = specification.substr(0, separator);
            std::size_t parsed = 0;
            frame_index = std::stoull(frame_text, &parsed);
            if (parsed != frame_text.size()) throw std::invalid_argument("trailing characters");
        } catch (const std::exception&) {
            throw std::runtime_error("invalid correction frame index: " + specification);
        }
        if (frame_index >= state.frames.size())
            throw std::runtime_error("correction frame index is outside the input sequence: " + specification);
        if (!anchors.emplace(frame_index, specification.substr(separator + 1)).second)
            throw std::runtime_error("more than one seed/correction mask was supplied for frame " +
                                     std::to_string(frame_index));
    }
    const auto seed_alpha = read_mask(anchors.at(state.seed_frame));

    TemporalFlowOptions options;
    options.minimum_confidence = minimum_confidence;
    if (state.source_video_transfer == "smpte2084")
        options.photometric_scale = 100.0f;
    std::uint64_t propagation_steps = 0;
    const auto add_steps = [&](std::uint64_t count) {
        if (count > std::numeric_limits<std::uint64_t>::max() - propagation_steps)
            throw std::runtime_error("estimated propagation step count overflows");
        propagation_steps += count;
    };
    add_steps(static_cast<std::uint64_t>(anchors.begin()->first));
    add_steps(static_cast<std::uint64_t>(state.frames.size() - 1 - anchors.rbegin()->first));
    for (auto previous = anchors.begin(), current = std::next(previous);
         current != anchors.end(); ++previous, ++current) {
        const auto gap = current->first - previous->first - 1;
        if (gap > std::numeric_limits<std::uint64_t>::max() / 2)
            throw std::runtime_error("estimated propagation step count overflows");
        add_steps(static_cast<std::uint64_t>(gap) * 2);
    }
    const auto pair_work = TemporalMaskPropagator::estimate_pair_work_units(width, height, options);
    const auto projected_work = pair_work != 0 && propagation_steps >
        std::numeric_limits<std::uint64_t>::max() / pair_work
        ? std::numeric_limits<std::uint64_t>::max() : propagation_steps * pair_work;
    if (projected_work > state.maximum_total_work_units)
        throw std::runtime_error("projected propagation work " + std::to_string(projected_work) +
            " exceeds --maximum-total-work-units " +
            std::to_string(state.maximum_total_work_units));
    nlohmann::json report{
        {"schema", "chronon.temporal_mask_propagation.v1"},
        {"seed_frame", state.seed_frame},
        {"width", width},
        {"height", height},
        {"confidence_threshold", minimum_confidence},
        {"minimum_mean_confidence", state.minimum_mean_confidence},
        {"minimum_mean_foreground_confidence", state.minimum_mean_foreground_confidence},
        {"maximum_foreground_loss", state.maximum_foreground_loss},
        {"input_transfer", state.input_transfer},
        {"source_video", state.source_video.empty()
            ? nlohmann::json(nullptr) : nlohmann::json(state.source_video)},
        {"source_video_transfer", state.source_video_transfer.empty()
            ? nlohmann::json(nullptr) : nlohmann::json(state.source_video_transfer)},
        {"photometric_scale", options.photometric_scale},
        {"tracking_working_space", "linear_bt709"},
        {"edge_refinement", {
            {"method", state.refinement_radius == 0 ? "none" : "guided_filter"},
            {"radius", state.refinement_radius},
            {"epsilon", state.refinement_epsilon},
            {"authored_anchors_refined", false},
            {"confidence_measured_before_refinement", true},
        }},
        {"anchor_count", anchors.size()},
        {"propagation_steps", propagation_steps},
        {"estimated_work_units_per_pair", pair_work},
        {"projected_work_units", projected_work},
        {"maximum_total_work_units", state.maximum_total_work_units},
        {"between_anchor_merge", "bidirectional_confidence_weighted"},
        {"temporary_spool", "compressed_png_intermediates_cleaned_on_exit"},
        {"frames", nlohmann::json::array()},
    };

    const auto write_report = [&](std::size_t index, const PropagatedMask& mask) {
        const auto path = mask_propagation_detail::output_path(state.output_pattern, index);
        const auto confidence_path = mask_propagation_detail::output_path(state.output_pattern, index, "_confidence");
        auto frame = mask_propagation_detail::load_tracking_frame(state.frames[index], state.input_transfer);
        std::vector<float> refined_alpha;
        const bool did_refine = state.refinement_radius != 0 && !anchors.contains(index);
        if (did_refine) {
            refined_alpha = TemporalMaskPropagator::refine_mask_edges(
                view_of(*frame), view_of(mask.alpha, mask.width, mask.height),
                {state.refinement_radius, state.refinement_epsilon});
        }
        const auto& output_alpha = did_refine ? refined_alpha : mask.alpha;
        if (!mask_propagation_detail::write_gray(spool.staged_output_path(index, false), output_alpha,
                        mask.width, mask.height))
            throw std::runtime_error("could not stage propagated mask: " + path);
        if (!mask_propagation_detail::write_gray(spool.staged_output_path(index, true), mask.confidence,
                        mask.width, mask.height))
            throw std::runtime_error("could not stage confidence matte: " +
                                     confidence_path);
        const auto source_digest = state.source_video.empty()
            ? assets::sha256_file(state.frames[index]) : std::nullopt;
        const std::string source_sha256 = state.source_video.empty()
            ? (source_digest ? source_digest->hex() : std::string{})
            : state.source_video_sha256;
        if (source_sha256.empty())
            throw std::runtime_error("could not hash tracking source frame: " +
                                     state.frames[index]);
        nlohmann::json source_presentation_time = nullptr;
        if (!state.source_video.empty()) {
            if (index >= state.source_presentation_times.size())
                throw std::runtime_error("source presentation timestamp is missing for frame " +
                                         std::to_string(index));
            const auto& time = state.source_presentation_times[index];
            source_presentation_time = {
                {"pts", time.ticks()},
                {"time_base_num", time.time_base.numerator},
                {"time_base_den", time.time_base.denominator},
            };
        }
        double visible_coverage = 0.0;
        double visible_confidence_sum = 0.0;
        double foreground_confidence_sum = 0.0;
        double foreground_visible_coverage = 0.0;
        std::uint64_t foreground_confidence_samples = 0;
        for (std::size_t pixel = 0; pixel < mask.alpha.size(); ++pixel) {
            const auto x = static_cast<i32>(pixel % mask.width);
            const auto y = static_cast<i32>(pixel / mask.width);
            const float alpha = frame->get_pixel(x, y).a;
            const double coverage = std::isfinite(alpha)
                ? std::clamp(static_cast<double>(alpha), 0.0, 1.0) : 0.0;
            visible_coverage += coverage;
            visible_confidence_sum += coverage * mask.confidence[pixel];
            if (mask.alpha[pixel] <= 0.01f || coverage <= 0.0) continue;
            foreground_confidence_sum += coverage * mask.confidence[pixel];
            foreground_visible_coverage += coverage;
            ++foreground_confidence_samples;
        }
        const double mean_confidence = visible_coverage > 0.0
            ? visible_confidence_sum / visible_coverage : 0.0;
        const double mean_foreground_confidence = foreground_confidence_samples == 0
            ? 0.0 : foreground_confidence_sum / foreground_visible_coverage;
        const double foreground_loss = mask.foreground_samples == 0 ? 0.0
            : static_cast<double>(mask.rejected_foreground_samples) /
                static_cast<double>(mask.foreground_samples);
        const bool authored_anchor = anchors.contains(index);
        const bool confidence_gate_passed = authored_anchor ||
            (mean_confidence >= state.minimum_mean_confidence &&
             mean_foreground_confidence >= state.minimum_mean_foreground_confidence &&
             foreground_loss <= state.maximum_foreground_loss);
        report["frames"].push_back({
            {"index", index},
            {"source", state.source_video.empty()
                ? state.frames[index]
                : state.source_video + "#presentation_frame=" + std::to_string(index)},
            {"source_sha256", source_sha256},
            {"source_presentation_time", source_presentation_time},
            {"mask", path},
            {"confidence_map", confidence_path},
            {"mean_confidence", mean_confidence},
            {"visible_alpha_coverage", visible_coverage},
            {"mean_foreground_confidence", mean_foreground_confidence},
            {"foreground_visible_alpha_coverage", foreground_visible_coverage},
            {"foreground_confidence_samples", foreground_confidence_samples},
            {"foreground_samples", mask.foreground_samples},
            {"rejected_foreground_samples", mask.rejected_foreground_samples},
            {"foreground_loss_fraction", foreground_loss},
            {"authored_anchor", authored_anchor},
            {"confidence_gate_passed", confidence_gate_passed},
            {"edge_refinement", did_refine ? "guided_filter" : "none"},
            {"rejected_low_texture", mask.rejected_low_texture},
            {"rejected_occlusion", mask.rejected_occlusion},
            {"rejected_large_motion", mask.rejected_large_motion},
        });
    };

    for (const auto& [frame_index, path] : anchors) {
        const auto alpha = frame_index == state.seed_frame
            ? seed_alpha : read_mask(path);
        write_report(frame_index, seed_result(alpha, width, height));
    }

    const auto propagate_outward = [&](std::size_t anchor_index, int direction) {
        auto current_frame = mask_propagation_detail::load_tracking_frame(state.frames[anchor_index], state.input_transfer);
        auto current_alpha = read_mask(anchors.at(anchor_index));
        std::vector<float> current_confidence(current_alpha.size(), 1.0f);
        std::size_t index = anchor_index;
        while (direction < 0 ? index > 0 : index + 1 < state.frames.size()) {
            const auto next_index = direction < 0 ? index - 1 : index + 1;
            auto next_frame = mask_propagation_detail::load_tracking_frame(state.frames[next_index], state.input_transfer);
            auto propagated = TemporalMaskPropagator::propagate(
                view_of(*current_frame), view_of(*next_frame),
                SoftMaskView{current_alpha, width, height, 0,
                             current_confidence, 0}, options);
            write_report(next_index, propagated);
            current_alpha = std::move(propagated.alpha);
            current_confidence = std::move(propagated.confidence);
            current_frame = std::move(next_frame);
            index = next_index;
        }
    };

    const auto first_anchor = anchors.begin()->first;
    const auto last_anchor = anchors.rbegin()->first;
    propagate_outward(first_anchor, -1);
    propagate_outward(last_anchor, 1);

    // Between authored corrections, compute independent forward and backward
    // mattes, then blend by per-pixel tracking confidence. Intermediate PNGs
    // are losslessly represented at the command's 8-bit output precision and
    // keep memory bounded to a few frames rather than the whole clip.
    for (auto interval = std::next(anchors.begin()); interval != anchors.end(); ++interval) {
        const auto left = std::prev(interval);
        const std::size_t begin = left->first;
        const std::size_t end = interval->first;
        if (end <= begin + 1) continue;

        std::vector<std::array<std::uint64_t, 5>> forward_rejections(end - begin + 1);
        auto forward_frame = mask_propagation_detail::load_tracking_frame(state.frames[begin], state.input_transfer);
        auto forward_alpha = read_mask(left->second);
        std::vector<float> forward_confidence(forward_alpha.size(), 1.0f);
        for (std::size_t i = begin + 1; i < end; ++i) {
            auto target_frame = mask_propagation_detail::load_tracking_frame(state.frames[i], state.input_transfer);
            auto propagated = TemporalMaskPropagator::propagate(
                view_of(*forward_frame), view_of(*target_frame),
                SoftMaskView{forward_alpha, width, height, 0,
                             forward_confidence, 0}, options);
            if (!mask_propagation_detail::write_gray(spool.frame_path(i, false), propagated.alpha, width, height) ||
                !mask_propagation_detail::write_gray(spool.frame_path(i, true), propagated.confidence, width, height))
                throw std::runtime_error("could not spool forward mask propagation frame " +
                                         std::to_string(i));
            forward_rejections[i - begin] = {
                propagated.rejected_low_texture,
                propagated.rejected_occlusion,
                propagated.rejected_large_motion,
                propagated.foreground_samples,
                propagated.rejected_foreground_samples};
            forward_alpha = std::move(propagated.alpha);
            forward_confidence = std::move(propagated.confidence);
            forward_frame = std::move(target_frame);
        }

        auto backward_frame = mask_propagation_detail::load_tracking_frame(state.frames[end], state.input_transfer);
        auto backward_alpha = read_mask(interval->second);
        std::vector<float> backward_confidence(backward_alpha.size(), 1.0f);
        for (std::size_t i = end - 1; i > begin; --i) {
            auto target_frame = mask_propagation_detail::load_tracking_frame(state.frames[i], state.input_transfer);
            auto backward = TemporalMaskPropagator::propagate(
                view_of(*backward_frame), view_of(*target_frame),
                SoftMaskView{backward_alpha, width, height, 0,
                             backward_confidence, 0}, options);
            const auto forward_mask = read_mask(spool.frame_path(i, false));
            const auto forward_confidence = read_mask(spool.frame_path(i, true));

            PropagatedMask combined;
            combined.width = width;
            combined.height = height;
            combined.alpha.resize(backward.alpha.size());
            combined.confidence.resize(backward.alpha.size());
            for (std::size_t pixel = 0; pixel < combined.alpha.size(); ++pixel) {
                const float forward_weight = std::clamp(forward_confidence[pixel], 0.0f, 1.0f);
                const float backward_weight = std::clamp(backward.confidence[pixel], 0.0f, 1.0f);
                const float total = forward_weight + backward_weight;
                combined.alpha[pixel] = total > 1.0e-6f
                    ? (forward_mask[pixel] * forward_weight +
                       backward.alpha[pixel] * backward_weight) / total
                    : 0.0f;
                combined.confidence[pixel] = 0.5f * total;
            }
            const auto& rejected = forward_rejections[i - begin];
            combined.rejected_low_texture = rejected[0] + backward.rejected_low_texture;
            combined.rejected_occlusion = rejected[1] + backward.rejected_occlusion;
            combined.rejected_large_motion = rejected[2] + backward.rejected_large_motion;
            combined.foreground_samples = rejected[3] + backward.foreground_samples;
            combined.rejected_foreground_samples = rejected[4] +
                backward.rejected_foreground_samples;
            write_report(i, combined);

            backward_alpha = std::move(backward.alpha);
            backward_confidence = std::move(backward.confidence);
            backward_frame = std::move(target_frame);
        }
    }

    std::sort(report["frames"].begin(), report["frames"].end(),
        [](const nlohmann::json& a, const nlohmann::json& b) {
            return a.at("index").get<std::size_t>() < b.at("index").get<std::size_t>();
        });

    std::vector<std::size_t> confidence_gate_failures;
    for (const auto& frame : report["frames"]) {
        if (!frame.at("authored_anchor").get<bool>() &&
            !frame.at("confidence_gate_passed").get<bool>())
            confidence_gate_failures.push_back(frame.at("index").get<std::size_t>());
    }
    report["confidence_gate"] = {
        {"status", confidence_gate_failures.empty() ? "pass" : "fail"},
        {"minimum_mean_confidence", state.minimum_mean_confidence},
        {"minimum_mean_foreground_confidence", state.minimum_mean_foreground_confidence},
        {"maximum_foreground_loss", state.maximum_foreground_loss},
        {"authored_anchor_frames_exempt", true},
        {"failed_frames", confidence_gate_failures},
    };

    report["outputs_published"] = confidence_gate_failures.empty();
    if (confidence_gate_failures.empty()) {
        for (std::size_t index = 0; index < state.frames.size(); ++index) {
            publish_staged_file(spool.staged_output_path(index, false),
                                mask_propagation_detail::output_path(state.output_pattern, index));
            publish_staged_file(spool.staged_output_path(index, true),
                                mask_propagation_detail::output_path(state.output_pattern, index, "_confidence"));
        }
    }

    const auto report_path = state.output_pattern + ".json";
    write_json_report_atomic(report_path, report);
    if (!confidence_gate_failures.empty()) {
        spdlog::error(
            "Mask propagation confidence gate rejected {} frame(s); see {}. No matte outputs or plan attachment were published.",
            confidence_gate_failures.size(), report_path);
        return 2;
    }
    return 0;
}


} // namespace



void register_mask_propagation_command(CLI::App& app, CliContext& ctx) {
    register_mask_segment_command(app, ctx);

    auto state = std::make_shared<MaskPropagationState>();
    auto* command = app.add_subcommand(
        "mask-propagate",
        "Propagate an authored soft mask over ordered frames or tagged video on CPU");
    command->add_option("frames", state->frames,
                        "Ordered 8-bit SDR frame images");
    command->add_option("--video-input", state->source_video,
                        "Decode explicitly tagged SDR/HDR video directly instead of supplying frame images");
    command->add_option("--seed-mask", state->seed_mask,
                        "PNG/JPEG seed matte: alpha if transparent, otherwise linear luma")
        ->required();
    command->add_option("--seed-frame", state->seed_frame,
                        "Zero-based index of the frame matching the seed matte")
        ->required();
    command->add_option("--input-transfer", state->input_transfer,
                        "Transfer for extracted frame RGB; matte images remain linear coverage data")
        ->check(CLI::IsMember(std::set<std::string>{"srgb", "rec709"}))
        ->default_val("srgb");
    command->add_option("--correction-mask", state->correction_masks,
                        "Authored correction matte as FRAME=PATH; may be repeated and anchors propagation in both directions");
    command->add_option("-o,--output", state->output_pattern,
                        "Output PNG path pattern containing #### (default: mask_####.png)")
        ->default_val("mask_####.png");
    command->add_option("--minimum-confidence", state->minimum_confidence,
                        "Reject propagated pixels below this per-pixel confidence")
        ->check(CLI::Range(0.0f, 1.0f));
    command->add_option("--minimum-mean-confidence", state->minimum_mean_confidence,
                        "Fail the job when a non-anchor frame's mean pixel confidence is lower; 0 disables this gate")
        ->check(CLI::Range(0.0f, 1.0f))
        ->default_val(0.0f);
    command->add_option("--minimum-mean-foreground-confidence",
                        state->minimum_mean_foreground_confidence,
                        "Fail when mean confidence over propagated matte coverage is lower; 0 disables this gate")
        ->check(CLI::Range(0.0f, 1.0f))
        ->default_val(0.0f);
    command->add_option("--maximum-foreground-loss", state->maximum_foreground_loss,
                        "Fail when more than this fraction of propagated foreground samples lose a valid track; 1 disables this gate")
        ->check(CLI::Range(0.0f, 1.0f))
        ->default_val(1.0f);
    command->add_option("--maximum-total-work-units", state->maximum_total_work_units,
                        "Reject jobs whose estimated total CPU tracking work exceeds this budget (default: 100 billion)")
        ->check(CLI::Range(std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max()))
        ->default_val(100'000'000'000ULL);
    command->add_option("--maximum-video-spool-bytes", state->maximum_video_spool_bytes,
                        "Reject direct video jobs whose estimated temporary decoded frames exceed this byte budget (default: 8 GiB)")
        ->check(CLI::Range(std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max()))
        ->default_val(8ULL * 1024ULL * 1024ULL * 1024ULL);
    command->add_option("--refine-radius", state->refinement_radius,
                        "Apply edge-aware guided-filter refinement to propagated mattes; 0 disables it and authored anchors remain exact")
        ->check(CLI::Range(0U, 32U))
        ->default_val(0U);
    command->add_option("--refine-epsilon", state->refinement_epsilon,
                        "Regularization for the normalized linear-light guided filter")
        ->check(CLI::Range(1.0e-6f, 1.0f))
        ->default_val(1.0e-3f);
    command->add_option("--plan-input", state->plan_input,
                        "Optional V3 RenderPlan to update with a track-matte relation");
    command->add_option("--plan-output", state->plan_output,
                        "Write the matte-linked RenderPlan to this path");
    command->add_option("--target-layer", state->target_layer,
                        "Existing RenderPlan layer that receives the propagated matte");
    command->add_option("--matte-video", state->matte_video,
                        "Encoded matte video path, or output path with --encode-matte-video");
    command->add_flag("--encode-matte-video", state->encode_matte_video,
                      "Encode generated opaque grayscale masks with the native CPU H.264 encoder at the RenderPlan frame rate");
    command->add_option("--matte-layer-id", state->matte_layer_id,
                        "New video layer id for the matte source");
    command->add_option("--matte-type", state->matte_type,
                        "Track matte operation for the opaque grayscale matte video")
        ->check(CLI::IsMember(std::set<std::string>{
            "luma", "luma_inverted"}));
    command->callback([state, &ctx] {
        try {
            std::unique_ptr<TemporaryMaskSpool> source_spool;
            if (!state->source_video.empty()) {
                source_spool = std::make_unique<TemporaryMaskSpool>();
                mask_propagation_detail::decode_sdr_video_frames(*state, *source_spool);
            } else if (state->frames.empty()) {
                throw std::runtime_error("provide ordered frame images or --video-input");
            }
            ctx.exit_code = propagate(*state, state->minimum_confidence);
            if (ctx.exit_code == 0 && state->encode_matte_video) {
                const auto first = mask_propagation_detail::load_tracking_frame(
                    state->frames.front(), state->input_transfer);
                const auto verification = mask_propagation_detail::encode_propagated_matte_video(*state,
                    static_cast<std::uint32_t>(first->width()),
                    static_cast<std::uint32_t>(first->height()));
                mask_propagation_detail::append_encoded_video_verification(*state, verification);
            }
            if (ctx.exit_code == 0 && mask_propagation_detail::has_plan_patch_options(*state))
                mask_propagation_detail::attach_matte_video_to_render_plan(*state);
        } catch (const std::exception& error) {
            spdlog::error("Mask propagation failed: {}", error.what());
            ctx.exit_code = 1;
        }
    });

    auto input = std::make_shared<std::string>();
    auto output = std::make_shared<std::string>("seed_matte.png");
    auto transfer = std::make_shared<std::string>("srgb");
    auto key_r = std::make_shared<float>(0.0f);
    auto key_g = std::make_shared<float>(1.0f);
    auto key_b = std::make_shared<float>(0.0f);
    auto threshold = std::make_shared<float>(0.20f);
    auto softness = std::make_shared<float>(0.08f);
    auto clip_black = std::make_shared<float>(0.0f);
    auto clip_white = std::make_shared<float>(1.0f);
    auto choke = std::make_shared<float>(0.0f);
    auto garbage_x = std::make_shared<float>(0.0f);
    auto garbage_y = std::make_shared<float>(0.0f);
    auto garbage_w = std::make_shared<float>(0.0f);
    auto garbage_h = std::make_shared<float>(0.0f);
    auto garbage_feather = std::make_shared<float>(0.0f);
    auto min_foreground_fraction = std::make_shared<float>(0.0f);
    auto max_foreground_fraction = std::make_shared<float>(1.0f);
    auto* key_command = app.add_subcommand(
        "mask-key",
        "Generate a CPU chroma-key seed matte from one PNG/JPEG frame");
    key_command->add_option("frame", *input, "Input PNG/JPEG frame")->required();
    key_command->add_option("-o,--output", *output, "Output opaque grayscale matte PNG")
        ->default_val("seed_matte.png");
    key_command->add_option("--input-transfer", *transfer,
        "Frame transfer function (sRGB default or Rec.709)")
        ->check(CLI::IsMember(std::set<std::string>{"srgb", "rec709"}))
        ->default_val("srgb");
    key_command->add_option("--key-r", *key_r, "Linear BT.709 key color red component")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--key-g", *key_g, "Linear BT.709 key color green component")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(1.0f);
    key_command->add_option("--key-b", *key_b, "Linear BT.709 key color blue component")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--threshold", *threshold,
        "Normalized chroma distance below which the screen is removed")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.20f);
    key_command->add_option("--softness", *softness,
        "Soft transition width above the chroma threshold")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.08f);
    key_command->add_option("--clip-black", *clip_black,
        "Remap matte values at or below this value to zero")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--clip-white", *clip_white,
        "Remap matte values at or above this value to one")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(1.0f);
    key_command->add_option("--choke", *choke,
        "Erode the keyed edge in coverage space")
        ->check(CLI::Range(0.0f, 0.999f))->default_val(0.0f);
    key_command->add_option("--garbage-x", *garbage_x,
        "Normalized keep-rectangle left edge (0 disables garbage matte)")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--garbage-y", *garbage_y,
        "Normalized keep-rectangle top edge")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--garbage-width", *garbage_w,
        "Normalized keep-rectangle width (0 disables garbage matte)")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--garbage-height", *garbage_h,
        "Normalized keep-rectangle height (0 disables garbage matte)")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--garbage-feather", *garbage_feather,
        "Normalized soft edge for the garbage rectangle")
        ->check(CLI::Range(0.0f, 0.5f))->default_val(0.0f);
    key_command->add_option("--minimum-foreground-fraction", *min_foreground_fraction,
        "Fail if fewer than this fraction of pixels have coverage above 0.5")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--maximum-foreground-fraction", *max_foreground_fraction,
        "Fail if more than this fraction of pixels have coverage above 0.5")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(1.0f);
    key_command->callback([input, output, transfer, key_r, key_g, key_b,
                           threshold, softness, clip_black, clip_white, choke,
                           garbage_x, garbage_y, garbage_w, garbage_h,
                           garbage_feather, min_foreground_fraction,
                           max_foreground_fraction, &ctx] {
        try {
            if (*clip_white <= *clip_black)
                throw std::runtime_error("--clip-white must be greater than --clip-black");
            if (*min_foreground_fraction > *max_foreground_fraction)
                throw std::runtime_error(
                    "--minimum-foreground-fraction must not exceed --maximum-foreground-fraction");
            if ((*garbage_w == 0.0f) != (*garbage_h == 0.0f) ||
                *garbage_x + *garbage_w > 1.0f ||
                *garbage_y + *garbage_h > 1.0f)
                throw std::runtime_error(
                    "garbage matte requires both width and height, and its rectangle must fit normalized image bounds");
            auto frame = mask_propagation_detail::load_tracking_frame(*input, *transfer);
            const auto width = static_cast<std::uint32_t>(frame->width());
            const auto height = static_cast<std::uint32_t>(frame->height());
            if (static_cast<std::uint64_t>(width) * height > 8'000'000)
                throw std::runtime_error("frame size exceeds the 8-megapixel CPU keying limit");
            KeyingParams params;
            params.mode = KeyingMode::Chroma;
            params.key_color = Color{*key_r, *key_g, *key_b, 1.0f};
            params.threshold = *threshold;
            params.softness = *softness;
            params.spill = 0.0f;
            params.clip_black = *clip_black;
            params.clip_white = *clip_white;
            params.choke = *choke;
            params.garbage_x = *garbage_x;
            params.garbage_y = *garbage_y;
            params.garbage_w = *garbage_w;
            params.garbage_h = *garbage_h;
            params.garbage_feather = *garbage_feather;
            std::vector<float> alpha(static_cast<std::size_t>(width) * height);
            double coverage_sum = 0.0;
            std::uint64_t foreground_pixels = 0;
            std::uint64_t soft_edge_pixels = 0;
            float coverage_min = 1.0f;
            float coverage_max = 0.0f;
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    const auto pixel = frame->get_pixel(static_cast<i32>(x), static_cast<i32>(y));
                    const float coverage =
                        effects::keying_coverage(pixel.unpremultiplied(),
                            static_cast<int>(x), static_cast<int>(y),
                            static_cast<int>(width), static_cast<int>(height), params);
                    alpha[static_cast<std::size_t>(y) * width + x] = coverage;
                    coverage_sum += coverage;
                    coverage_min = std::min(coverage_min, coverage);
                    coverage_max = std::max(coverage_max, coverage);
                    foreground_pixels += coverage > 0.5f;
                    soft_edge_pixels += coverage > 0.0f && coverage < 1.0f;
                }
            }
            const double pixel_count = static_cast<double>(alpha.size());
            const double foreground_fraction = pixel_count == 0.0
                ? 0.0 : static_cast<double>(foreground_pixels) / pixel_count;
            const bool gate_passed = foreground_fraction >= *min_foreground_fraction &&
                                     foreground_fraction <= *max_foreground_fraction;
            // The foreground-fraction gate is an acceptance boundary: do not
            // publish a seed matte that the caller has explicitly rejected.
            if (gate_passed && !mask_propagation_detail::write_gray(*output, alpha, width, height))
                throw std::runtime_error("could not write generated seed matte: " + *output);
            std::cout << nlohmann::json{
                {"schema", "chronon.chroma_key_matte.v1"},
                {"input", *input}, {"output", *output},
                {"output_written", gate_passed},
                {"width", width}, {"height", height},
                {"input_transfer", *transfer},
                {"coverage_statistics", {
                    {"minimum", coverage_min}, {"maximum", coverage_max},
                    {"mean", pixel_count == 0.0 ? 0.0 : coverage_sum / pixel_count},
                    {"foreground_threshold", 0.5f},
                    {"foreground_pixels", foreground_pixels},
                    {"foreground_fraction", foreground_fraction},
                    {"soft_edge_pixels", soft_edge_pixels}}},
                {"foreground_fraction_gate", {
                    {"minimum", *min_foreground_fraction},
                    {"maximum", *max_foreground_fraction},
                    {"passed", gate_passed}}},
                {"key_color_linear_bt709", {*key_r, *key_g, *key_b}},
                {"threshold", *threshold}, {"softness", *softness},
                {"clip_black", *clip_black}, {"clip_white", *clip_white},
                {"choke", *choke},
                {"garbage_matte", {
                    {"x", *garbage_x}, {"y", *garbage_y},
                    {"width", *garbage_w}, {"height", *garbage_h},
                    {"feather", *garbage_feather}}},
                {"intended_use", "mask-propagate seed matte"}
            }.dump(2) << '\n';
            if (!gate_passed) {
                spdlog::error(
                    "Chroma-key foreground fraction {} is outside [{}, {}]",
                    foreground_fraction, *min_foreground_fraction,
                    *max_foreground_fraction);
                ctx.exit_code = 2;
            } else {
                ctx.exit_code = 0;
            }
        } catch (const std::exception& error) {
            spdlog::error("Mask keying failed: {}", error.what());
            ctx.exit_code = 1;
        }
    });
}

} // namespace chronon3d::cli
