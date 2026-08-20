// ============================================================================
// src/runtime/sdk_render_engine.cpp
//
// SINGLE bridge between the public sdk::RenderEngine surface and the internal
// chronon3d::RenderEngine adapter. The public boundary owns RGBA8 conversion
// and translates every internal failure into RenderError.
// ============================================================================

#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#if CHRONON3D_ENABLE_VIDEO
#include <chronon3d/media/video/video_sink_factory.hpp>
#endif
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <mutex>
#include <sstream>
#include <vector>
#include <optional>
#include <utility>

namespace chronon3d::sdk {

namespace {

chronon3d::RenderSettings convert_settings(const RenderSettings& sdk) {
    chronon3d::RenderSettings internal;
    internal.ssaa_factor      = static_cast<float>(std::max(1, sdk.antialiasing_samples));
    internal.motion_blur.mode = sdk.motion_blur
        ? chronon3d::MotionBlurMode::TemporalAccumulation
        : chronon3d::MotionBlurMode::Off;
    internal.motion_blur.samples           = sdk.motion_blur ? 8 : 1;
    internal.motion_blur.shutter_angle_deg = 180.0f;
    internal.motion_blur.shutter_phase_deg = 0.0f;
    internal.dirty.enabled                 = sdk.dirty_rects;
    internal.force_scalar_normal_blend     = sdk.deterministic;
    internal.fail_on_missing_assets       = true;
    // Render-plan jobs supply their canonical RenderBudget at the internal
    // plan boundary; the stable SDK settings POD remains layout-compatible.
    return internal;
}

void framebuffer_to_rgba8(const Framebuffer& fb,
                          std::vector<std::uint8_t>& out) {
    const i32 w = fb.width();
    const i32 h = fb.height();
    const usize count = static_cast<usize>(w) * h * 4;
    out.resize(count);

    const Color* src = fb.data();
    std::uint8_t* dst = out.data();

    for (usize i = 0; i < static_cast<usize>(w) * h; ++i) {
        const Color& c = src[i];
        dst[i * 4 + 0] = static_cast<std::uint8_t>(
            std::clamp(c.r * 255.0f, 0.0f, 255.0f));
        dst[i * 4 + 1] = static_cast<std::uint8_t>(
            std::clamp(c.g * 255.0f, 0.0f, 255.0f));
        dst[i * 4 + 2] = static_cast<std::uint8_t>(
            std::clamp(c.b * 255.0f, 0.0f, 255.0f));
        dst[i * 4 + 3] = static_cast<std::uint8_t>(
            std::clamp(c.a * 255.0f, 0.0f, 255.0f));
    }
}

void resize_rgba8_nearest(const std::vector<std::uint8_t>& source,
                          i32 source_width,
                          i32 source_height,
                          i32 target_width,
                          i32 target_height,
                          std::vector<std::uint8_t>& out) {
    out.resize(static_cast<usize>(target_width) * target_height * 4);
    for (i32 y = 0; y < target_height; ++y) {
        const i32 source_y = std::min(source_height - 1,
                                      (y * source_height) / target_height);
        for (i32 x = 0; x < target_width; ++x) {
            const i32 source_x = std::min(source_width - 1,
                                          (x * source_width) / target_width);
            const usize source_offset =
                (static_cast<usize>(source_y) * source_width + source_x) * 4;
            const usize target_offset =
                (static_cast<usize>(y) * target_width + x) * 4;
            std::copy_n(source.data() + source_offset, 4,
                        out.data() + target_offset);
        }
    }
}

RenderError runtime_error(std::string message) {
    const auto code = message.find("Prepared asset integrity check failed") !=
            std::string::npos
        ? RenderErrorCode::AssetChanged
        : RenderErrorCode::RuntimeFailure;
    return RenderError{.code = code, .message = std::move(message),
                       .component = "render"};
}

#if CHRONON3D_ENABLE_VIDEO
chronon3d::media::video::VideoCodec convert_codec(VideoCodec codec) {
    using Source = VideoCodec;
    using Target = chronon3d::media::video::VideoCodec;
    switch (codec) {
        case Source::H264: return Target::H264;
        case Source::H265: return Target::H265;
        case Source::VP9:  return Target::VP9;
        case Source::AV1:  return Target::AV1;
        case Source::Auto:
        default:           return Target::Auto;
    }
}

chronon3d::media::video::VideoContainer convert_container(
    VideoContainer container, const std::filesystem::path& output_path) {
    using Source = VideoContainer;
    using Target = chronon3d::media::video::VideoContainer;
    switch (container) {
        case Source::Mp4:  return Target::Mp4;
        case Source::Mkv:  return Target::Mkv;
        case Source::WebM: return Target::WebM;
        case Source::Auto:
        default: {
            const auto extension = output_path.extension().string();
            if (extension == ".mkv") return Target::Mkv;
            if (extension == ".webm") return Target::WebM;
            return Target::Mp4;
        }
    }
}
#endif // CHRONON3D_ENABLE_VIDEO

} // namespace

struct RenderEngine::Impl {
    chronon3d::RenderEngine   engine;
    // Engine-local resolver used by the RenderPlan JSON facade; mirrors the
    // assets root set through set_assets_root() exactly like the C ABI keeps
    // its own AssetResolver alongside the sdk::RenderEngine.
    chronon3d::assets::AssetResolver resolver;
    // The public contract allows settings mutation while a render is active
    // and serializes concurrent renders on one engine instance.  Keep the
    // synchronization at the SDK boundary so the internal OPP adapter does
    // not need to expose or duplicate a second ownership protocol.
    std::mutex                state_mutex;
    std::vector<std::uint8_t> pixel_buffer;
    // Reuse the prepared barrier for sequential render() calls on the same
    // composition. The public convenience API must not compile, warm up and
    // rebuild caches once per frame.
    std::unique_ptr<chronon3d::PreparedRenderJob> prepared_job;
    const chronon3d::Composition* prepared_composition{nullptr};
    int prepared_width{0};
    int prepared_height{0};
    RenderSettings             settings{};
    RenderOutput              last_output{};
    LogCallback                log_callback{};
    void*                      log_user{nullptr};

    void log(LogLevel level, std::string_view component,
             std::string_view message) const {
        if (log_callback) log_callback(level, component, message, log_user);
    }

    template <typename RenderFn>
    chronon3d::Result<RenderOutput, RenderError>
    render_frame(Frame frame, RenderFn&& render_fn) {
        std::lock_guard lock(state_mutex);
        try {
            if (settings.width <= 0 || settings.height <= 0) {
                auto result = RenderError{
                    .code = RenderErrorCode::InvalidSettings,
                    .message = "RenderSettings width and height must be positive",
                    .component = "render",
                };
                log(LogLevel::Error, result.component, result.message);
                return result;
            }
            const auto started = std::chrono::steady_clock::now();
            auto framebuffer = render_fn();

            if (const auto error = engine.last_render_error()) {
                const std::string message = error->message.empty()
                    ? "internal render graph reported a frame error"
                    : error->message;
                auto result = runtime_error(message);
                log(LogLevel::Error, result.component, result.message);
                return result;
            }
            if (!framebuffer) {
                auto result = runtime_error(
                    "internal RenderEngine::render() returned a null framebuffer");
                log(LogLevel::Error, result.component, result.message);
                return result;
            }

            std::vector<std::uint8_t> rendered_pixels;
            framebuffer_to_rgba8(*framebuffer, rendered_pixels);
            if (framebuffer->width() == settings.width &&
                framebuffer->height() == settings.height) {
                pixel_buffer = std::move(rendered_pixels);
            } else {
                resize_rgba8_nearest(
                    rendered_pixels, framebuffer->width(), framebuffer->height(),
                    settings.width, settings.height, pixel_buffer);
            }

            RenderOutput output;
            output.frame = frame;
            output.width = settings.width;
            output.height = settings.height;
            output.format = PixelFormat::Rgba8;
            output.bytes_per_row = static_cast<std::size_t>(output.width) * 4;
            output.pixels = pixel_buffer.data();
            output.elapsed_milliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            last_output = output;
            return output;
        } catch (const std::exception& error) {
            auto result = runtime_error(std::string("render exception: ") + error.what());
            log(LogLevel::Error, result.component, result.message);
            return result;
        } catch (...) {
            auto result = RenderError{
                .code = RenderErrorCode::InternalError,
                .message = "render: unknown exception",
                .component = "render",
            };
            log(LogLevel::Error, result.component, result.message);
            return result;
        }
    }

    Impl() : engine() {}

    explicit Impl(RenderSettings settings_in)
        : engine() {
        settings = settings_in;
        engine.set_settings(convert_settings(settings));
    }
};

RenderEngine::RenderEngine()
    : m_impl(std::make_unique<Impl>())
{}

RenderEngine::RenderEngine(RenderSettings settings)
    : m_impl(std::make_unique<Impl>(settings))
{}

void RenderEngine::set_log_callback(LogCallback callback, void* user) {
    std::lock_guard lock(m_impl->state_mutex);
    m_impl->log_callback = std::move(callback);
    m_impl->log_user = user;
}

RenderEngine::~RenderEngine() = default;

RenderEngine::RenderEngine(RenderEngine&& other) noexcept
    : m_impl(std::move(other.m_impl))
{}

RenderEngine& RenderEngine::operator=(RenderEngine&& other) noexcept {
    if (this != &other) m_impl = std::move(other.m_impl);
    return *this;
}

chronon3d::Result<RenderOutput, RenderError>
RenderEngine::render(const chronon3d::Composition& composition, Frame frame) {
    return m_impl->render_frame(frame, [&] {
        // The prepared-job path is the canonical resource barrier: it
        // resolves and populates the engine-local asset caches before graph
        // execution. Calling the legacy direct adapter here leaves image
        // lookup-only rendering with a placeholder and breaks multi-engine
        // asset-root isolation.
        if (!m_impl->prepared_job ||
            m_impl->prepared_composition != &composition ||
            m_impl->prepared_width != m_impl->settings.width ||
            m_impl->prepared_height != m_impl->settings.height) {
            auto prepared = m_impl->engine.prepare(composition);
            m_impl->prepared_job = std::make_unique<chronon3d::PreparedRenderJob>(
                std::move(prepared));
            m_impl->prepared_composition = &composition;
            m_impl->prepared_width = m_impl->settings.width;
            m_impl->prepared_height = m_impl->settings.height;
        }
        return m_impl->prepared_job->render(chronon3d::Frame{frame.integral()});
    });
}

chronon3d::Result<RenderOutput, RenderError>
RenderEngine::render_compiled(
    const chronon3d::CompiledComposition& compiled, Frame frame) {
    return m_impl->render_frame(frame, [&] {
        auto settings = convert_settings(m_impl->settings);
        settings.render_budget = compiled.render_budget;
        m_impl->prepared_job.reset();
        m_impl->prepared_composition = nullptr;
        m_impl->engine.set_settings(settings);
        return m_impl->engine.render_compiled(
            compiled, chronon3d::Frame{frame.integral()});
    });
}

chronon3d::Result<RenderReport, RenderError>
RenderEngine::render_to_file(const RenderFileRequest& request,
                             const RenderCallbacks& callbacks) {
    std::lock_guard lock(m_impl->state_mutex);

    const auto reject = [&](RenderError error)
        -> chronon3d::Result<RenderReport, RenderError> {
        if (error.component.empty()) error.component = "render_job";
        m_impl->log(LogLevel::Error, error.component, error.message);
        return error;
    };

    if (!request.composition && !request.compiled_composition) {
        return reject(RenderError{RenderErrorCode::InvalidComposition,
                                   "RenderFileRequest requires a composition or compiled_composition"});
    }
    if (request.output_path.empty()) {
        return reject(RenderError{RenderErrorCode::InvalidSettings,
                                   "RenderFileRequest output_path must not be empty"});
    }
    if (!request.video.overwrite && std::filesystem::exists(request.output_path)) {
        return reject(RenderError{RenderErrorCode::InvalidSettings,
                                   "output already exists and overwrite is disabled"});
    }
    if (m_impl->settings.width <= 0 || m_impl->settings.height <= 0) {
        return reject(RenderError{RenderErrorCode::InvalidSettings,
                                   "RenderSettings width and height must be positive"});
    }

    const auto start = request.start_frame.integral();
    const auto end = request.end_frame.integral();
    const auto step = request.step.integral();
    if (end < start || step <= 0 || request.frame_rate.numerator <= 0 ||
        request.frame_rate.denominator <= 0) {
        return reject(RenderError{RenderErrorCode::InvalidSettings,
                                   "invalid frame range or frame rate"});
    }
    const auto total = static_cast<std::uint64_t>(((end - start) / step) + 1);
    if (request.limits.max_frames != 0 && total > request.limits.max_frames) {
        return reject(RenderError{RenderErrorCode::BudgetExceeded,
                                   "render job frame limit exceeded",
                                   "render_job"});
    }
    if (request.limits.max_width != 0 &&
        static_cast<std::uint64_t>(m_impl->settings.width) > request.limits.max_width) {
        return reject(RenderError{RenderErrorCode::BudgetExceeded,
                                   "render job width limit exceeded",
                                   "render_job"});
    }
    if (request.limits.max_height != 0 &&
        static_cast<std::uint64_t>(m_impl->settings.height) > request.limits.max_height) {
        return reject(RenderError{RenderErrorCode::BudgetExceeded,
                                   "render job height limit exceeded",
                                   "render_job"});
    }
    const auto width = static_cast<std::uint64_t>(m_impl->settings.width);
    const auto height = static_cast<std::uint64_t>(m_impl->settings.height);
    if (width != 0 && height > std::numeric_limits<std::uint64_t>::max() / width / 16) {
        return reject(RenderError{RenderErrorCode::OutOfMemory,
                                   "render job memory estimate overflow",
                                   "memory"});
    }
    const auto estimated_frame_bytes = width * height * 16;
    if (request.limits.max_memory_bytes != 0 &&
        estimated_frame_bytes > request.limits.max_memory_bytes) {
        return reject(RenderError{RenderErrorCode::BudgetExceeded,
                                   "render job memory limit exceeded",
                                   "memory"});
    }

#if CHRONON3D_ENABLE_VIDEO
    using namespace chronon3d::media::video;
    VideoSinkConfig config;
    config.stream.width = m_impl->settings.width;
    config.stream.height = m_impl->settings.height;
    config.stream.frame_rate_num = request.frame_rate.numerator;
    config.stream.frame_rate_den = request.frame_rate.denominator;
    config.stream.submitted_format = media::video::PixelFormat::RGBA8;
    config.encoder.codec = convert_codec(request.video.codec);
    config.encoder.bitrate = request.video.bitrate;
    config.encoder.crf = request.video.crf;
    const auto temp_path = request.output_path.string() + ".chronon.tmp." +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        request.output_path.extension().string();
    config.output.output_path = temp_path;
    config.output.container = convert_container(request.video.container, request.output_path);
    config.output.overwrite = request.video.overwrite;
    config.label = "sdk::RenderEngine::render_to_file";

    auto sink = create_video_sink(config);
    if (!sink || !sink->open(config)) {
        const auto message = sink ? sink->last_error_message()
                                  : std::string{"video sink unavailable"};
        return reject(RenderError{RenderErrorCode::BackendUnavailable,
                                   message.empty() ? "failed to open video sink" : message,
                                   "encoder"});
    }

    const auto started = std::chrono::steady_clock::now();
    const auto total_frames = ((end - start) / step) + 1;

    auto fail = [&](RenderError error) -> chronon3d::Result<RenderReport, RenderError> {
        sink->close();
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);
        if (error.component.empty()) error.component = "render_job";
        m_impl->log(LogLevel::Error, error.component, error.message);
        return error;
    };

    // Compile authoring input once before entering the frame loop. Runtime
    // execution below is intentionally compiled-only, so a multi-frame file
    // render cannot rebuild scene/camera state for every frame.
    std::optional<chronon3d::CompiledComposition> local_compiled;
    const chronon3d::CompiledComposition* active_compiled =
        request.compiled_composition;
    if (active_compiled == nullptr) {
        auto compiled = chronon3d::compile_composition(
            *request.composition, chronon3d::CompositionCompileContext{});
        if (!compiled) {
            return fail(RenderError{
                RenderErrorCode::InvalidComposition,
                "composition compilation failed: " + compiled.error().message});
        }
        local_compiled.emplace(std::move(compiled).value());
        active_compiled = &*local_compiled;
    }

    std::vector<std::uint8_t> pixels;
    std::uint64_t rendered = 0;

    for (std::int64_t frame = start; frame <= end; frame += step) {
        if (callbacks.is_cancelled && callbacks.is_cancelled()) {
            return fail(RenderError{RenderErrorCode::Cancelled,
                                    "render cancelled by callback"});
        }

        auto settings = convert_settings(m_impl->settings);
        settings.render_budget = active_compiled->render_budget;
        m_impl->prepared_job.reset();
        m_impl->prepared_composition = nullptr;
        m_impl->engine.set_settings(settings);
        auto framebuffer = m_impl->engine.render_compiled(
            *active_compiled, chronon3d::Frame{frame});
        if (const auto error = m_impl->engine.last_render_error()) {
            return fail(runtime_error(error->message.empty()
                ? "internal render graph reported a frame error" : error->message));
        }
        if (!framebuffer) {
            return fail(runtime_error("internal renderer returned a null framebuffer"));
        }

        framebuffer_to_rgba8(*framebuffer, pixels);
        VideoFrameView view;
        view.data = pixels.data();
        view.stride_bytes = static_cast<std::size_t>(config.stream.width) * 4;
        view.width = config.stream.width;
        view.height = config.stream.height;
        view.pixel_format = media::video::PixelFormat::RGBA8;
        view.pts = static_cast<std::int64_t>(rendered);
        if (!sink->submit(view)) {
            const auto message = sink->last_error_message();
            return fail(RenderError{RenderErrorCode::RuntimeFailure,
                message.empty() ? "video sink rejected a rendered frame" : message});
        }

        ++rendered;
        if (callbacks.progress) {
            callbacks.progress(Frame{frame}, Frame{total_frames});
        }
        if (end - frame < step) break;
    }

    if (!sink->flush() || !sink->close()) {
        const auto message = sink->last_error_message();
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);
        return RenderError{RenderErrorCode::RuntimeFailure,
            message.empty() ? "video sink failed while finalizing output" : message};
    }

    std::error_code publish_error;
    std::filesystem::rename(temp_path, request.output_path, publish_error);
    if (publish_error) {
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);
        return RenderError{RenderErrorCode::RuntimeFailure,
            "cannot publish output atomically: " + publish_error.message()};
    }

    return RenderReport{
        .output_path = request.output_path,
        .rendered_frames = rendered,
        .elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count(),
    };
#else
    return reject(RenderError{RenderErrorCode::BackendUnavailable,
                               "Built without CHRONON3D_ENABLE_VIDEO support.",
                               "encoder"});
#endif
}

chronon3d::Result<std::shared_ptr<const chronon3d::CompiledComposition>,
                  RenderError>
RenderEngine::compile_plan_json(std::string_view json) {
    std::lock_guard lock(m_impl->state_mutex);
    try {
        const auto root = nlohmann::json::parse(json.begin(), json.end());
        const auto decoded = chronon3d::render_plan::decode_render_plan(root);
        if (!decoded) {
            return RenderError{RenderErrorCode::InvalidPlan,
                               decoded.error().message, "render_plan"};
        }
        chronon3d::render_plan::RenderPlanFingerprintOptions fingerprint_options;
        fingerprint_options.render_settings.width = decoded->canvas.width;
        fingerprint_options.render_settings.height = decoded->canvas.height;
        fingerprint_options.render_settings.deterministic = false;
        fingerprint_options.render_settings.force_scalar_normal_blend = false;
        auto compiled = chronon3d::render_plan::compile_render_plan(
            decoded.value(), m_impl->resolver, fingerprint_options);
        if (!compiled) {
            return RenderError{RenderErrorCode::InvalidPlan,
                               compiled.error().message, "render_plan"};
        }
        auto prepared = std::move(compiled).value();
        // Apply the plan canvas to the engine output size so subsequent
        // render_compiled()/render_to_file() render at native resolution.
        RenderSettings settings = m_impl->settings;
        settings.width = prepared.canvas.width;
        settings.height = prepared.canvas.height;
        m_impl->settings = settings;
        m_impl->prepared_job.reset();
        m_impl->prepared_composition = nullptr;
        m_impl->engine.set_settings(convert_settings(settings));
        return std::make_shared<const chronon3d::CompiledComposition>(
            std::move(prepared.compiled_composition));
    } catch (const std::exception& error) {
        return RenderError{RenderErrorCode::DecodeFailure,
            std::string{"render plan JSON parse failed: "} + error.what(),
            "render_plan"};
    }
}

chronon3d::Result<RenderReport, RenderError>
RenderEngine::render_plan_file(const std::filesystem::path& plan_path,
                               std::filesystem::path output_path,
                               std::filesystem::path assets_root) {
    std::ifstream input(plan_path);
    if (!input) {
        return RenderError{RenderErrorCode::DecodeFailure,
                           "cannot open render plan: " + plan_path.string(),
                           "render_plan"};
    }
    std::ostringstream contents;
    contents << input.rdbuf();

    if (!assets_root.empty()) {
        set_assets_root(std::move(assets_root));
    }

    std::shared_ptr<const chronon3d::CompiledComposition> compiled_holder;
    RenderFileRequest request;
    {
        std::lock_guard lock(m_impl->state_mutex);
        try {
            const auto root = nlohmann::json::parse(contents.str());
            const auto decoded = chronon3d::render_plan::decode_render_plan(root);
            if (!decoded) {
                return RenderError{RenderErrorCode::InvalidPlan,
                                   decoded.error().message, "render_plan"};
            }
            chronon3d::render_plan::RenderPlanFingerprintOptions fingerprint_options;
            fingerprint_options.render_settings.width = decoded->canvas.width;
            fingerprint_options.render_settings.height = decoded->canvas.height;
            fingerprint_options.render_settings.deterministic = false;
            fingerprint_options.render_settings.force_scalar_normal_blend = false;
            auto compiled = chronon3d::render_plan::compile_render_plan(
                decoded.value(), m_impl->resolver, fingerprint_options);
            if (!compiled) {
                return RenderError{RenderErrorCode::InvalidPlan,
                                   compiled.error().message, "render_plan"};
            }
            auto prepared = std::move(compiled).value();
            RenderSettings settings = m_impl->settings;
            settings.width = prepared.canvas.width;
            settings.height = prepared.canvas.height;
            m_impl->settings = settings;
            m_impl->prepared_job.reset();
            m_impl->prepared_composition = nullptr;
            m_impl->engine.set_settings(convert_settings(settings));

            const auto fps = prepared.canvas.fps > 0 ? prepared.canvas.fps : 30;
            const auto duration = prepared.canvas.duration.integral();
            compiled_holder =
                std::make_shared<const chronon3d::CompiledComposition>(
                    std::move(prepared.compiled_composition));

            request.compiled_composition = compiled_holder.get();
            request.output_path = output_path.empty()
                ? std::filesystem::path{prepared.output.path}
                : output_path;
            request.start_frame = Frame{0};
            request.end_frame = Frame{duration > 0 ? duration - 1 : 0};
            request.frame_rate = FrameRate{fps, 1};
        } catch (const std::exception& error) {
            return RenderError{RenderErrorCode::DecodeFailure,
                std::string{"render plan error: "} + error.what(),
                "render_plan"};
        }
    }

    return render_to_file(request, {});
}

void RenderEngine::set_settings(const RenderSettings& settings) {
    std::lock_guard lock(m_impl->state_mutex);
    m_impl->settings = settings;
    m_impl->prepared_job.reset();
    m_impl->prepared_composition = nullptr;
    m_impl->engine.set_settings(convert_settings(settings));
}

void RenderEngine::set_assets_root(std::filesystem::path root) {
    std::lock_guard lock(m_impl->state_mutex);
    // A relative value is an explicit caller choice, not a resolver fallback.
    // Canonicalize it once at assignment so later CWD changes cannot alter the
    // engine-local mount.
    if (root.is_relative()) {
        root = std::filesystem::absolute(root);
    }
    m_impl->engine.set_assets_root(root);
    m_impl->resolver.mount(std::move(root));
}

} // namespace chronon3d::sdk
