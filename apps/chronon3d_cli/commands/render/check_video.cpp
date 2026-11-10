#include "check_video.hpp"

#include "scope_analysis_detail.hpp"

#include <chronon3d/backends/image/image_loader.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/media/video/media_probe.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <utility>

namespace chronon3d::cli::check {
namespace {

// Framebuffer pixels are premultiplied linear BT.709, not sRGB code values.
// 1e-4 linear is roughly 0.01 sRGB, so a dark-but-visible overlay background
// is not mistaken for a black frame. A frame is black only when essentially
// every opaque pixel is below that floor; sparse highlights keep it alive.
constexpr double kBlackLumaLinear = 1.0e-4;
constexpr double kBlackMinFraction = 0.999;
constexpr double kEmptyVisibleFraction = 1.0e-6;
constexpr double kClipHighLuma = 0.98;
constexpr double kClipLowLuma = 1.0e-4;
constexpr double kOpaqueAlpha = 0.999;
constexpr double kVisibleAlpha = 1.0e-6;

[[nodiscard]] double luma_of(const Color& color) noexcept {
    return 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
}

[[nodiscard]] double frame_diff_luma(const Framebuffer& current,
                                     const Framebuffer& previous) {
    if (current.width() != previous.width() ||
        current.height() != previous.height()) {
        return 1.0;
    }
    double sum = 0.0;
    std::uint64_t count = 0;
    for (i32 y = 0; y < current.height(); ++y) {
        const Color* current_row = current.pixels_row(y);
        const Color* previous_row = previous.pixels_row(y);
        for (i32 x = 0; x < current.width(); ++x) {
            const Color& a = current_row[x];
            const Color& b = previous_row[x];
            if (a.a <= kVisibleAlpha && b.a <= kVisibleAlpha) continue;
            sum += std::abs(luma_of(a.unpremultiplied()) -
                            luma_of(b.unpremultiplied()));
            ++count;
        }
    }
    return count == 0 ? 0.0 : sum / static_cast<double>(count);
}

struct Interval {
    int first_frame{0};
    int last_frame{0};
    int count{0};
};

[[nodiscard]] double interval_seconds(const Interval& interval,
                                      double effective_fps) noexcept {
    if (effective_fps <= 0.0) return 0.0;
    return static_cast<double>(interval.count) / effective_fps;
}

[[nodiscard]] std::vector<Interval> runs_of(
    const std::vector<FrameMetrics>& frames,
    const std::function<bool(const FrameMetrics&)>& predicate) {
    std::vector<Interval> runs;
    std::optional<Interval> active;
    for (const auto& frame : frames) {
        if (predicate(frame)) {
            if (!active) active = Interval{frame.frame, frame.frame, 1};
            else {
                active->last_frame = frame.frame;
                ++active->count;
            }
        } else if (active) {
            runs.push_back(*active);
            active.reset();
        }
    }
    if (active) runs.push_back(*active);
    return runs;
}

} // namespace

FrameMetrics measure_frame(const Framebuffer& framebuffer, const int frame,
                           const Framebuffer* previous) {
    FrameMetrics metrics;
    metrics.frame = frame;

    const double pixel_count = static_cast<double>(framebuffer.width()) *
                               static_cast<double>(framebuffer.height());
    if (pixel_count <= 0.0) return metrics;

    double luma_sum = 0.0;
    std::uint64_t visible = 0;
    std::uint64_t opaque = 0;
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    std::uint64_t near_black = 0;

    for (i32 y = 0; y < framebuffer.height(); ++y) {
        const Color* row = framebuffer.pixels_row(y);
        for (i32 x = 0; x < framebuffer.width(); ++x) {
            const Color& pixel = row[x];
            const double alpha = std::max(0.0, static_cast<double>(pixel.a));
            if (alpha <= kVisibleAlpha) continue;
            ++visible;
            // The framebuffer stores premultiplied linear RGBA; temporal and
            // black thresholds are defined on straight-alpha luma.
            const double luma = luma_of(pixel.unpremultiplied());
            luma_sum += luma;
            if (alpha >= kOpaqueAlpha) {
                ++opaque;
                if (luma >= kClipHighLuma) ++high;
                if (luma <= kClipLowLuma) ++low;
                if (luma <= kBlackLumaLinear) ++near_black;
            }
        }
    }

    metrics.mean_luma = visible > 0
        ? luma_sum / static_cast<double>(visible) : 0.0;
    metrics.visible_fraction = static_cast<double>(visible) / pixel_count;
    metrics.opaque_fraction = static_cast<double>(opaque) / pixel_count;
    metrics.near_black_fraction = opaque > 0
        ? static_cast<double>(near_black) / static_cast<double>(opaque) : 0.0;
    metrics.clipped_high_fraction = opaque > 0
        ? static_cast<double>(high) / static_cast<double>(opaque) : 0.0;
    metrics.clipped_low_fraction = opaque > 0
        ? static_cast<double>(low) / static_cast<double>(opaque) : 0.0;
    metrics.black = metrics.opaque_fraction >= 0.5 &&
                    metrics.near_black_fraction >= kBlackMinFraction;

    if (previous != nullptr) {
        metrics.diff_from_previous = frame_diff_luma(framebuffer, *previous);
    }
    return metrics;
}

std::vector<Finding> analyze_video(const VideoScan& scan,
                                   const Thresholds& thresholds) {
    std::vector<Finding> findings;
    if (scan.frames.empty()) return findings;

    const double effective_fps =
        scan.fps > 0.0 ? scan.fps / static_cast<double>(std::max(1, scan.sample_step))
                       : 0.0;

    if (std::all_of(scan.frames.begin(), scan.frames.end(),
                    [](const FrameMetrics& frame) {
                        return frame.visible_fraction <= kEmptyVisibleFraction;
                    })) {
        Finding finding;
        finding.id = "video.empty_alpha";
        finding.category = "video";
        finding.severity = Severity::P0;
        finding.message = "every sampled frame is fully transparent";
        finding.fix = "check the composition output alpha and the render-plan layers";
        finding.details = {
            {"sampled_frames", scan.sampled_frames},
            {"total_frames", scan.total_frames},
        };
        findings.push_back(std::move(finding));
    }

    const auto black_runs = runs_of(scan.frames, [](const FrameMetrics& frame) {
        return frame.black;
    });
    for (const auto& run : black_runs) {
        const double seconds = interval_seconds(run, effective_fps);
        if (seconds < thresholds.black_seconds) continue;
        const bool whole_video = run.count >= scan.frames.size() &&
                                 scan.sampled_frames > 1;
        Finding finding;
        finding.id = "video.black";
        finding.category = "video";
        finding.severity = whole_video ? Severity::P0 : Severity::P1;
        finding.message = whole_video
            ? "the whole sampled video is black"
            : "black interval longer than the configured floor";
        finding.fix = "check the scene, camera clip planes and the output alpha; "
                      "if the black is intentional, raise --black-seconds";
        finding.first_frame = run.first_frame;
        finding.last_frame = run.last_frame;
        if (effective_fps > 0.0) {
            finding.first_second = static_cast<double>(run.first_frame) / effective_fps;
            finding.last_second = static_cast<double>(run.last_frame + 1) / effective_fps;
        }
        finding.details = {
            {"seconds", seconds},
            {"threshold_seconds", thresholds.black_seconds},
            {"sampled_frames", run.count},
        };
        findings.push_back(std::move(finding));
    }

    if (effective_fps > 0.0 && scan.frames.size() > 1) {
        std::vector<Interval> freeze_runs;
        std::optional<Interval> active;
        for (std::size_t index = 1; index < scan.frames.size(); ++index) {
            const auto& frame = scan.frames[index];
            const bool frozen = !frame.black &&
                frame.diff_from_previous <= thresholds.freeze_max_luma_delta;
            if (frozen) {
                if (!active) {
                    active = Interval{scan.frames[index - 1].frame, frame.frame, 2};
                } else {
                    active->last_frame = frame.frame;
                    ++active->count;
                }
            } else if (active) {
                freeze_runs.push_back(*active);
                active.reset();
            }
        }
        if (active) freeze_runs.push_back(*active);

        for (const auto& run : freeze_runs) {
            const double seconds = interval_seconds(run, effective_fps);
            if (seconds < thresholds.freeze_seconds) continue;
            Finding finding;
            finding.id = "video.freeze";
            finding.category = "video";
            finding.severity = Severity::P1;
            finding.message = "frozen interval longer than the configured floor";
            finding.fix = "animate the layer or split the hold; if the freeze is "
                          "intentional, raise --freeze-seconds";
            finding.first_frame = run.first_frame;
            finding.last_frame = run.last_frame;
            finding.first_second = static_cast<double>(run.first_frame) / effective_fps;
            finding.last_second = static_cast<double>(run.last_frame + 1) / effective_fps;
            finding.details = {
                {"seconds", seconds},
                {"threshold_seconds", thresholds.freeze_seconds},
                {"mean_abs_luma_delta", thresholds.freeze_max_luma_delta},
            };
            findings.push_back(std::move(finding));
        }
    }

    if (effective_fps > 0.0 && scan.frames.size() > 1) {
        const int window = std::max(1, static_cast<int>(std::ceil(effective_fps)));
        int best_count = 0;
        int best_index = -1;
        for (std::size_t start = 1; start < scan.frames.size(); ++start) {
            int flashes = 0;
            const std::size_t end = std::min(scan.frames.size(),
                start + static_cast<std::size_t>(window));
            for (std::size_t index = start; index < end; ++index) {
                const double delta = std::abs(
                    scan.frames[index].mean_luma -
                    scan.frames[index - 1].mean_luma);
                if (delta >= thresholds.flash_luma_delta) ++flashes;
            }
            if (flashes > best_count) {
                best_count = flashes;
                best_index = static_cast<int>(start);
            }
        }
        if (best_count > thresholds.flash_max_per_second) {
            Finding finding;
            finding.id = "video.flash";
            finding.category = "video";
            finding.severity = Severity::P1;
            finding.message = "more luminance transitions than the photosensitivity floor";
            finding.fix = "reduce the transition rate or lower the contrast between "
                          "the alternating frames";
            finding.first_frame = scan.frames[static_cast<std::size_t>(best_index)].frame;
            finding.details = {
                {"transitions_per_second", best_count},
                {"threshold_per_second", thresholds.flash_max_per_second},
                {"luma_delta", thresholds.flash_luma_delta},
                {"window_frames", window},
            };
            findings.push_back(std::move(finding));
        }
    }

    const auto clipped = std::max_element(
        scan.frames.begin(), scan.frames.end(),
        [](const FrameMetrics& a, const FrameMetrics& b) {
            return a.clipped_high_fraction < b.clipped_high_fraction;
        });
    if (clipped != scan.frames.end() &&
        clipped->clipped_high_fraction > thresholds.max_clipped_fraction) {
        Finding finding;
        finding.id = "video.highlights_clipped";
        finding.category = "video";
        finding.severity = Severity::P2;
        finding.message = "highlights are clipped beyond the configured fraction";
        finding.fix = "lower the key/light intensity or bring the highlights into range";
        finding.first_frame = clipped->frame;
        finding.details = {
            {"clipped_fraction", clipped->clipped_high_fraction},
            {"threshold_fraction", thresholds.max_clipped_fraction},
        };
        findings.push_back(std::move(finding));
    }

    std::sort(findings.begin(), findings.end(),
        [](const Finding& a, const Finding& b) {
            const int severity_a = static_cast<int>(a.severity);
            const int severity_b = static_cast<int>(b.severity);
            if (severity_a != severity_b) return severity_a < severity_b;
            return a.first_frame.value_or(0) < b.first_frame.value_or(0);
        });
    return findings;
}

VideoScanResult scan_video(const std::string& input,
                           const Thresholds& thresholds,
                           const bool quick) {
    VideoScanResult result;
    (void)thresholds;

    if (scope_detail::is_video_input(input)) {
        result.scan.emplace();
        auto& scan = *result.scan;
        scan.input = input;
        scan.is_video = true;
        scan.quick = quick;

#if defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)
        const auto probe = media::video::probe_media(input);
        if (!probe) {
            result.scan.reset();
            result.error = "could not probe video: " + probe.error().message;
            return result;
        }
        const auto stream_it = std::find_if(
            probe->streams.begin(), probe->streams.end(),
            [](const auto& stream) {
                return stream.kind == media::video::MediaStreamKind::Video;
            });
        if (stream_it == probe->streams.end()) {
            result.scan.reset();
            result.error = "input has no video stream";
            return result;
        }
        const double fps = stream_it->frame_rate.denominator != 0
            ? static_cast<double>(stream_it->frame_rate.numerator) /
              static_cast<double>(stream_it->frame_rate.denominator)
            : 0.0;
        scan.fps = fps;
        scan.quick = quick;

        media::NativeVideoFrameDecoder decoder;
        decoder.set_software_only(true);
        const auto count = decoder.indexed_sample_count(input);
        if (count == 0) {
            result.scan.reset();
            result.error = "video decoder could not build a sample index";
            return result;
        }
        if (count > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            result.scan.reset();
            result.error = "video has more frames than this command can index";
            return result;
        }
        scan.total_frames = static_cast<int>(count);
        scan.sample_step = quick
            ? std::max(1, static_cast<int>(std::ceil(
                  static_cast<double>(count) / 90.0)))
            : 1;

        std::shared_ptr<Framebuffer> previous;
        for (std::uint64_t index = 0; index < count;
             index += static_cast<std::uint64_t>(scan.sample_step)) {
            auto decoded = decoder.decode_frame_index_at(
                input, static_cast<std::int64_t>(index),
                stream_it->width, stream_it->height);
            const auto* frame = media::decoded_frame_if(decoded);
            if (frame == nullptr || !frame->framebuffer) {
                const auto* failure = media::decode_failure_if(decoded);
                result.scan.reset();
                result.error = "could not decode frame " +
                    std::to_string(index) +
                    (failure ? ": " + failure->diagnostic.message : std::string{});
                return result;
            }
            scan.frames.push_back(measure_frame(
                *frame->framebuffer, static_cast<int>(index),
                previous ? previous.get() : nullptr));
            previous = frame->framebuffer;
        }
        scan.sampled_frames = static_cast<int>(scan.frames.size());
        scan.decoder_status = "native_ffmpeg_software";
        return result;
#else
        result.scan.reset();
        result.error = "video check requires CHRONON3D_ENABLE_NATIVE_FFMPEG";
        return result;
#endif
    }

    if (scope_detail::supported_scope_image(input)) {
        auto framebuffer = scope_detail::load_scope_image(input);
        if (!framebuffer) {
            result.error = "could not decode image: " + input;
            return result;
        }
        result.scan.emplace();
        auto& scan = *result.scan;
        scan.input = input;
        scan.is_image = true;
        scan.total_frames = 1;
        scan.sampled_frames = 1;
        scan.fps = 0.0;
        scan.frames.push_back(measure_frame(*framebuffer, 0, nullptr));
        scan.decoder_status = scope_detail::is_exr_input(input) ? "exr" : "image";
        return result;
    }

    result.error = "unsupported check input (expected a supported video or image)";
    return result;
}

} // namespace chronon3d::cli::check
