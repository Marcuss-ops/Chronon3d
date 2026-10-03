#include "command_mask_propagate_detail.hpp"

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/effects/keying_matte.hpp>
#include <chronon3d/media/tracking/temporal_mask_propagator.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::cli {
namespace {

using mask_propagation_detail::load_tracking_frame;
using mask_propagation_detail::write_gray;

struct SegmentPoint {
    float x{0.0f};
    float y{0.0f};
};

std::vector<SegmentPoint> parse_segment_points(
    const std::vector<std::string>& values, const char* label) {
    if (values.empty())
        throw std::runtime_error(std::string("at least one --") + label + " point is required");
    std::vector<SegmentPoint> points;
    points.reserve(values.size());
    for (const auto& value : values) {
        const auto comma = value.find(',');
        if (comma == std::string::npos || comma == 0 || comma + 1 >= value.size())
            throw std::runtime_error(std::string("invalid --") + label + " point; expected normalized X,Y in [0,1]: " + value);
        try {
            std::size_t x_end = 0, y_end = 0;
            const float x = std::stof(value.substr(0, comma), &x_end);
            const float y = std::stof(value.substr(comma + 1), &y_end);
            if (x_end != comma || y_end != value.size() - comma - 1 ||
                !std::isfinite(x) || !std::isfinite(y) ||
                x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f)
                throw std::runtime_error("point is not finite normalized X,Y");
            points.push_back({x, y});
        } catch (const std::exception&) {
            throw std::runtime_error(std::string("invalid --") + label + " point; expected normalized X,Y in [0,1]: " + value);
        }
    }
    return points;
}

struct SegmentColorModel {
    std::array<double, 3> mean{};
    // Row-major inverse covariance for a regularized full-RGB Gaussian.
    std::array<double, 9> inverse_covariance{};
    double log_determinant{0.0};
};

int run_scribble_segmentation(
    const std::string& input_path, const std::string& output_path,
    const std::string& transfer, const std::vector<std::string>& foreground_values,
    const std::vector<std::string>& background_values, std::uint32_t seed_radius,
    std::uint32_t iterations, std::uint64_t maximum_work_units,
    float minimum_foreground_fraction, float maximum_foreground_fraction,
    float maximum_mean_uncertainty) {
    auto frame = load_tracking_frame(input_path, transfer);
    const auto width = static_cast<std::uint32_t>(frame->width());
    const auto height = static_cast<std::uint32_t>(frame->height());
    const std::uint64_t count64 = static_cast<std::uint64_t>(width) * height;
    if (count64 == 0 || count64 > 8'000'000)
        throw std::runtime_error("mask segmentation supports images up to 8 megapixels");
    if (iterations == 0 || maximum_work_units == 0 ||
        !std::isfinite(minimum_foreground_fraction) ||
        !std::isfinite(maximum_foreground_fraction) ||
        minimum_foreground_fraction < 0.0f || maximum_foreground_fraction > 1.0f ||
        minimum_foreground_fraction > maximum_foreground_fraction ||
        !std::isfinite(maximum_mean_uncertainty) ||
        maximum_mean_uncertainty < 0.0f || maximum_mean_uncertainty > 1.0f)
        throw std::runtime_error("invalid segmentation gate or work settings");
    const auto count = static_cast<std::size_t>(count64);
    // Include the extra fixed matrix-inversion and Mahalanobis work for the
    // full-covariance color models in the agent-configurable work budget.
    const auto work_per_pixel = 5ULL * static_cast<std::uint64_t>(iterations) + 16ULL;
    if (count64 > maximum_work_units / work_per_pixel)
        throw std::runtime_error("segmentation work estimate exceeds --maximum-work-units");
    const auto estimated_work_units = count64 * work_per_pixel;
    const auto foreground = parse_segment_points(foreground_values, "foreground");
    const auto background = parse_segment_points(background_values, "background");

    std::vector<std::array<float, 3>> colors(count);
    std::vector<float> source_coverage(count, 1.0f);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto premultiplied = frame->get_pixel(static_cast<i32>(x), static_cast<i32>(y));
            if (!std::isfinite(premultiplied.a) || premultiplied.a < 0.0f ||
                premultiplied.a > 1.0f)
                throw std::runtime_error("mask-segment source contains invalid alpha values");
            const auto index = static_cast<std::size_t>(y) * width + x;
            source_coverage[index] = premultiplied.a;
            const auto pixel = premultiplied.unpremultiplied();
            if (premultiplied.a > 0.0f && (!std::isfinite(pixel.r) ||
                !std::isfinite(pixel.g) || !std::isfinite(pixel.b)))
                throw std::runtime_error("mask-segment source contains non-finite RGB values");
            colors[index] = {pixel.r, pixel.g, pixel.b};
        }
    }
    std::vector<std::int8_t> labels(count, -1);
    const auto mark_points = [&](const std::vector<SegmentPoint>& points, std::int8_t label) {
        for (const auto& point : points) {
            const auto cx = static_cast<std::int32_t>(std::lround(point.x * (width - 1)));
            const auto cy = static_cast<std::int32_t>(std::lround(point.y * (height - 1)));
            const auto r = static_cast<std::int32_t>(seed_radius);
            for (auto y = std::max(0, cy - r); y <= std::min(static_cast<std::int32_t>(height) - 1, cy + r); ++y) {
                for (auto x = std::max(0, cx - r); x <= std::min(static_cast<std::int32_t>(width) - 1, cx + r); ++x) {
                    const auto dx = x - cx, dy = y - cy;
                    if (dx * dx + dy * dy > r * r) continue;
                    const auto index = static_cast<std::size_t>(y) * width + static_cast<std::uint32_t>(x);
                    if (source_coverage[index] <= 0.01f) continue;
                    if (labels[index] >= 0 && labels[index] != label)
                        throw std::runtime_error("foreground/background scribble regions overlap; move the seed points apart");
                    labels[index] = label;
                }
            }
        }
    };
    mark_points(foreground, 1);
    mark_points(background, 0);

    const auto fit_model = [&](std::int8_t label) {
        SegmentColorModel model;
        model.mean = {};
        double samples = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            if (labels[i] != label || source_coverage[i] <= 0.01f) continue;
            const double weight = source_coverage[i];
            for (std::size_t c = 0; c < 3; ++c) model.mean[c] += colors[i][c] * weight;
            samples += weight;
        }
        if (samples == 0) throw std::runtime_error("segmentation scribble has no covered pixels");
        for (auto& channel : model.mean) channel /= static_cast<double>(samples);
        std::array<double, 9> covariance{};
        for (std::size_t i = 0; i < count; ++i) {
            if (labels[i] != label || source_coverage[i] <= 0.01f) continue;
            std::array<double, 3> delta{};
            for (std::size_t c = 0; c < 3; ++c)
                delta[c] = colors[i][c] - model.mean[c];
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t column = 0; column < 3; ++column) {
                    covariance[row * 3 + column] += delta[row] * delta[column] *
                        source_coverage[i];
                }
            }
        }
        for (double& value : covariance) value /= static_cast<double>(samples);
        // A small diagonal regularizer keeps flat-color scribbles invertible
        // without discarding cross-channel covariance.
        covariance[0] += 0.0025;
        covariance[4] += 0.0025;
        covariance[8] += 0.0025;
        const double a = covariance[0], b = covariance[1], c = covariance[2];
        const double d = covariance[4], e = covariance[5], f = covariance[8];
        const double determinant = a * (d * f - e * e) -
            b * (b * f - c * e) + c * (b * e - c * d);
        if (!std::isfinite(determinant) || determinant <= 0.0)
            throw std::runtime_error("scribble color covariance is not positive definite");
        model.log_determinant = std::log(determinant);
        model.inverse_covariance = {
            (d * f - e * e) / determinant,
            (c * e - b * f) / determinant,
            (b * e - c * d) / determinant,
            (c * e - b * f) / determinant,
            (a * f - c * c) / determinant,
            (b * c - a * e) / determinant,
            (b * e - c * d) / determinant,
            (b * c - a * e) / determinant,
            (a * d - b * b) / determinant};
        return model;
    };
    const auto foreground_model = fit_model(1);
    const auto background_model = fit_model(0);
    std::vector<float> prior(count), alpha(count), next(count);
    const auto log_likelihood = [](const SegmentColorModel& model,
                                   const std::array<float, 3>& sample) {
        std::array<double, 3> delta{};
        for (std::size_t channel = 0; channel < 3; ++channel)
            delta[channel] = sample[channel] - model.mean[channel];
        double mahalanobis = 0.0;
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                mahalanobis += delta[row] *
                    model.inverse_covariance[row * 3 + column] * delta[column];
            }
        }
        return -0.5 * (model.log_determinant + std::max(mahalanobis, 0.0));
    };
    for (std::size_t i = 0; i < count; ++i) {
        const double foreground_log_likelihood =
            log_likelihood(foreground_model, colors[i]);
        const double background_log_likelihood =
            log_likelihood(background_model, colors[i]);
        const double logit = std::clamp(foreground_log_likelihood - background_log_likelihood,
                                        -30.0, 30.0);
        prior[i] = static_cast<float>(1.0 / (1.0 + std::exp(-logit)));
        alpha[i] = labels[i] == 1 ? 1.0f : (labels[i] == 0 ? 0.0f : prior[i]);
    }

    // Edge weights are derived once from linear BT.709 color distances.
    double mean_edge_distance = 0.0;
    std::uint64_t edge_count = 0;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto i = static_cast<std::size_t>(y) * width + x;
            for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{0, 1}}) {
                const auto nx = x + static_cast<std::uint32_t>(dx);
                const auto ny = y + static_cast<std::uint32_t>(dy);
                if ((dx && nx >= width) || (dy && ny >= height)) continue;
                const auto& a = colors[i];
                const auto& b = colors[static_cast<std::size_t>(ny) * width + nx];
                double distance = 0.0;
                for (std::size_t c = 0; c < 3; ++c) {
                    const double delta = a[c] - b[c];
                    distance += delta * delta;
                }
                const double coverage_delta = source_coverage[i] -
                    source_coverage[static_cast<std::size_t>(ny) * width + nx];
                distance += coverage_delta * coverage_delta;
                mean_edge_distance += distance;
                ++edge_count;
            }
        }
    }
    const double beta = 0.5 / std::max(mean_edge_distance / std::max<std::uint64_t>(edge_count, 1), 1.0e-8);
    std::vector<std::array<float, 4>> weights(count);
    constexpr std::array<std::pair<int, int>, 4> directions{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto i = static_cast<std::size_t>(y) * width + x;
            for (std::size_t direction = 0; direction < directions.size(); ++direction) {
                const auto nx = static_cast<std::int32_t>(x) + directions[direction].first;
                const auto ny = static_cast<std::int32_t>(y) + directions[direction].second;
                if (nx < 0 || ny < 0 || nx >= static_cast<std::int32_t>(width) || ny >= static_cast<std::int32_t>(height)) continue;
                const auto& a = colors[i];
                const auto& b = colors[static_cast<std::size_t>(ny) * width + static_cast<std::uint32_t>(nx)];
                double distance = 0.0;
                for (std::size_t c = 0; c < 3; ++c) {
                    const double delta = a[c] - b[c];
                    distance += delta * delta;
                }
                const double coverage_delta = source_coverage[i] -
                    source_coverage[static_cast<std::size_t>(ny) * width +
                                    static_cast<std::uint32_t>(nx)];
                distance += coverage_delta * coverage_delta;
                weights[i][direction] = static_cast<float>(std::exp(-beta * distance));
            }
        }
    }
    constexpr float data_weight = 0.2f;
    for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) {
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const auto i = static_cast<std::size_t>(y) * width + x;
                if (labels[i] >= 0) {
                    next[i] = labels[i] == 1 ? 1.0f : 0.0f;
                    continue;
                }
                float numerator = data_weight * prior[i];
                float denominator = data_weight;
                for (std::size_t direction = 0; direction < directions.size(); ++direction) {
                    const auto nx = static_cast<std::int32_t>(x) + directions[direction].first;
                    const auto ny = static_cast<std::int32_t>(y) + directions[direction].second;
                    if (nx < 0 || ny < 0 || nx >= static_cast<std::int32_t>(width) || ny >= static_cast<std::int32_t>(height)) continue;
                    const float weight = weights[i][direction];
                    numerator += weight * alpha[static_cast<std::size_t>(ny) * width + static_cast<std::uint32_t>(nx)];
                    denominator += weight;
                }
                next[i] = numerator / denominator;
            }
        }
        alpha.swap(next);
    }
    std::uint64_t foreground_pixels = 0;
    double uncertainty_sum = 0.0;
    double unseeded_coverage = 0.0;
    std::uint64_t transparent_pixels = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (source_coverage[i] <= 0.0f) ++transparent_pixels;
        if (labels[i] < 0 && source_coverage[i] > 0.0f) {
            uncertainty_sum += 4.0 * alpha[i] * (1.0 - alpha[i]) *
                source_coverage[i];
            unseeded_coverage += source_coverage[i];
        }
        alpha[i] *= source_coverage[i];
        foreground_pixels += alpha[i] > 0.5f;
    }
    const double foreground_fraction = static_cast<double>(foreground_pixels) / count;
    const double mean_uncertainty = unseeded_coverage <= 0.0
        ? 0.0 : uncertainty_sum / unseeded_coverage;
    const bool gates_passed = foreground_fraction >= minimum_foreground_fraction &&
        foreground_fraction <= maximum_foreground_fraction &&
        mean_uncertainty <= maximum_mean_uncertainty;
    const auto report_path = output_path + ".json";
    nlohmann::json report{
        {"schema", "chronon.scribble_segmentation.v1"},
        {"method", "seeded_linear_rgb_full_covariance_gaussian_models_with_edge_weighted_diffusion"},
        {"input", input_path}, {"output", output_path},
        {"matte_written", false},
        {"input_transfer", transfer}, {"working_space", "linear_bt709"},
        {"width", width}, {"height", height},
        {"iterations", iterations}, {"seed_radius_pixels", seed_radius},
        {"estimated_work_units", estimated_work_units},
        {"maximum_work_units", maximum_work_units},
        {"source_alpha_used_as_coverage", true},
        {"fully_transparent_pixels", transparent_pixels},
        {"foreground_seed_points", foreground_values},
        {"background_seed_points", background_values},
        {"seeded_pixel_count", static_cast<std::uint64_t>(count) -
            static_cast<std::uint64_t>(std::count(labels.begin(), labels.end(), std::int8_t{-1}))},
        {"foreground_fraction", foreground_fraction},
        {"mean_uncertainty", mean_uncertainty},
        {"uncertainty_definition", "source-alpha-weighted mean(4*soft_segmentation_alpha*(1-soft_segmentation_alpha)) over unseeded visible coverage"},
        {"gate", {
            {"status", gates_passed ? "pass" : "fail"},
            {"minimum_foreground_fraction", minimum_foreground_fraction},
            {"maximum_foreground_fraction", maximum_foreground_fraction},
            {"maximum_mean_uncertainty", maximum_mean_uncertainty}}},
        {"next_step", "inspect and correct matte, then use as mask-propagate seed"},
    };
    std::ofstream report_file(report_path, std::ios::binary | std::ios::trunc);
    if (!report_file) throw std::runtime_error("could not write segmentation report: " + report_path);
    report_file << report.dump(2) << '\n';
    if (!report_file.good()) throw std::runtime_error("could not finish segmentation report: " + report_path);
    if (!gates_passed) {
        spdlog::error("Scribble segmentation gate failed; inspect {}", report_path);
        return 2;
    }
    if (!write_gray(output_path, alpha, width, height))
        throw std::runtime_error("could not write segmented seed matte: " + output_path);
    report["matte_written"] = true;
    std::ofstream completed_report(report_path, std::ios::binary | std::ios::trunc);
    if (!completed_report)
        throw std::runtime_error("could not update segmentation report: " + report_path);
    completed_report << report.dump(2) << '\n';
    if (!completed_report.good())
        throw std::runtime_error("could not finish segmentation report: " + report_path);
    return 0;
}


} // namespace

void register_mask_segment_command(CLI::App& app, CliContext& ctx) {
    struct SegmentState {
        std::string input;
        std::string output{"seed_matte.png"};
        std::string transfer{"srgb"};
        std::vector<std::string> foreground;
        std::vector<std::string> background;
        std::uint32_t seed_radius{5};
        std::uint32_t iterations{64};
        std::uint64_t maximum_work_units{2'000'000'000ULL};
        float minimum_foreground_fraction{0.001f};
        float maximum_foreground_fraction{0.999f};
        float maximum_mean_uncertainty{1.0f};
    };
    auto segment_state = std::make_shared<SegmentState>();
    auto* segment_command = app.add_subcommand(
        "mask-segment",
        "Create an agent-guided soft subject matte from foreground/background points on CPU");
    segment_command->add_option("input", segment_state->input,
                                "8-bit SDR PNG/JPEG source image")
        ->required();
    segment_command->add_option("--foreground", segment_state->foreground,
                                "Normalized foreground seed point X,Y; repeat to add points")
        ->required();
    segment_command->add_option("--background", segment_state->background,
                                "Normalized background seed point X,Y; repeat to add points")
        ->required();
    segment_command->add_option("--input-transfer", segment_state->transfer,
                                "Input interpretation: srgb (default) or rec709")
        ->check(CLI::IsMember(std::set<std::string>{"srgb", "rec709"}))
        ->default_val("srgb");
    segment_command->add_option("--seed-radius", segment_state->seed_radius,
                                "Radius in pixels assigned to each hard foreground/background point")
        ->check(CLI::Range(0U, 256U))
        ->default_val(5U);
    segment_command->add_option("--iterations", segment_state->iterations,
                                "Edge-weighted matte diffusion iterations")
        ->check(CLI::Range(1U, 500U))
        ->default_val(64U);
    segment_command->add_option("--maximum-work-units", segment_state->maximum_work_units,
                                "Fail before solving if pixel-iteration work exceeds this budget")
        ->check(CLI::Range(std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max()))
        ->default_val(2'000'000'000ULL);
    segment_command->add_option("--minimum-foreground-fraction", segment_state->minimum_foreground_fraction,
                                "Fail the report gate when too little of the frame is foreground")
        ->check(CLI::Range(0.0f, 1.0f))
        ->default_val(0.001f);
    segment_command->add_option("--maximum-foreground-fraction", segment_state->maximum_foreground_fraction,
                                "Fail the report gate when too much of the frame is foreground")
        ->check(CLI::Range(0.0f, 1.0f))
        ->default_val(0.999f);
    segment_command->add_option("--maximum-mean-uncertainty", segment_state->maximum_mean_uncertainty,
                                "Gate on average matte ambiguity 4*alpha*(1-alpha); 1 disables this gate")
        ->check(CLI::Range(0.0f, 1.0f))
        ->default_val(1.0f);
    segment_command->add_option("-o,--output", segment_state->output,
                                "Output soft matte PNG (a JSON gate report is written beside it)")
        ->default_val("seed_matte.png");
    segment_command->callback([segment_state, &ctx] {
        try {
            ctx.exit_code = run_scribble_segmentation(
                segment_state->input, segment_state->output,
                segment_state->transfer, segment_state->foreground,
                segment_state->background, segment_state->seed_radius,
                segment_state->iterations, segment_state->maximum_work_units,
                segment_state->minimum_foreground_fraction,
                segment_state->maximum_foreground_fraction,
                segment_state->maximum_mean_uncertainty);
        } catch (const std::exception& error) {
            spdlog::error("Scribble segmentation failed: {}", error.what());
            ctx.exit_code = 1;
        }
    });
}

} // namespace chronon3d::cli
