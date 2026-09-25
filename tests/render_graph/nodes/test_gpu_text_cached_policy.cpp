#include <doctest/doctest.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/sink.h>

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <mutex>
#include <string>
#include <vector>

#include "src/render_graph/nodes/text_run/gpu_text_cached_policy.hpp"
#include "src/render_graph/nodes/text_run/gpu_text_run.hpp"

using chronon3d::graph::text_run::detail::TextExecutionRoute;
using chronon3d::graph::text_run::detail::TextRouteReason;
using chronon3d::graph::text_run::detail::resolve_text_execution_route;

namespace {

constexpr bool kNativeSurfaces = true;
constexpr bool kNoNativeSurfaces = false;

class TextRouteCaptureSink final : public spdlog::sinks::sink {
public:
    void log(const spdlog::details::log_msg& message) override {
        std::lock_guard lock(m_mutex);
        m_messages.emplace_back(message.payload.begin(), message.payload.end());
    }
    void flush() override {}
    void set_pattern(const std::string&) override {}
    void set_formatter(std::unique_ptr<spdlog::formatter>) override {}

    [[nodiscard]] std::vector<std::string> messages() const {
        std::lock_guard lock(m_mutex);
        return m_messages;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_messages;
};

class ScopedTextRouteLogCapture {
public:
    ScopedTextRouteLogCapture() : m_previous(spdlog::default_logger()) {
        m_sink = std::make_shared<TextRouteCaptureSink>();
        m_logger = std::make_shared<spdlog::logger>("text_route_test", m_sink);
        m_logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(m_logger);
    }
    ~ScopedTextRouteLogCapture() { spdlog::set_default_logger(m_previous); }

    [[nodiscard]] const TextRouteCaptureSink& sink() const noexcept { return *m_sink; }

private:
    std::shared_ptr<spdlog::logger> m_previous;
    std::shared_ptr<TextRouteCaptureSink> m_sink;
    std::shared_ptr<spdlog::logger> m_logger;
};

std::size_t text_route_log_count(const TextRouteCaptureSink& sink) {
    const auto messages = sink.messages();
    return static_cast<std::size_t>(std::count_if(
        messages.begin(), messages.end(), [](const std::string& message) {
            return message.find("[text-route]") != std::string::npos;
        }));
}

} // namespace

TEST_CASE("Cached GPU text policy prioritizes explicit rejection gates") {
    const auto rejected =
        resolve_text_execution_route(true, kNativeSurfaces, false, true, true, true);
    CHECK(rejected.route == TextExecutionRoute::Reject);
    CHECK(rejected.reason == TextRouteReason::UnsupportedAnimation);

    // An unsupported animator stack is rejected before the lane is consulted:
    // the software hand-off must never mask a stack the GPU cannot express.
    const auto rejected_without_native_surfaces =
        resolve_text_execution_route(true, kNoNativeSurfaces, false, true, true, true);
    CHECK(rejected_without_native_surfaces.route == TextExecutionRoute::Reject);
    CHECK(rejected_without_native_surfaces.reason ==
          TextRouteReason::UnsupportedAnimation);

    const auto software =
        resolve_text_execution_route(false, kNativeSurfaces, true, true, true, true);
    CHECK(software.route == TextExecutionRoute::Software);
    CHECK(software.reason == TextRouteReason::ForcedSoftware);
}

TEST_CASE("Cached GPU text policy hands a non-native-surface lane to software") {
    // Every native text kernel needs a resident glyph surface.  A lane that
    // cannot host one has no GPU route, even with the prepared cache and the
    // surface registry present; it must not pick MTSDF and fail per frame.
    const auto decision =
        resolve_text_execution_route(false, kNoNativeSurfaces, false, true, true, true);
    CHECK(decision.route == TextExecutionRoute::Software);
    CHECK(decision.reason == TextRouteReason::NativeSurfacesUnavailable);
}

TEST_CASE("Cached GPU text policy fails closed without native MTSDF resources") {
    CHECK(resolve_text_execution_route(false, kNativeSurfaces, false, true, true, true)
              .route == TextExecutionRoute::NativeMtsdf);
    CHECK(resolve_text_execution_route(false, kNativeSurfaces, false, false, true, true)
              .route == TextExecutionRoute::Reject);
    CHECK(resolve_text_execution_route(false, kNativeSurfaces, false, true, false, true)
              .route == TextExecutionRoute::Reject);
    CHECK(resolve_text_execution_route(false, kNativeSurfaces, false, true, true, false)
              .route == TextExecutionRoute::Reject);
}

TEST_CASE("Cached GPU text route logs only when diagnostics are enabled") {
    using namespace chronon3d;
    using namespace chronon3d::graph::text_run;

    const auto run_case = [](bool diagnostics_enabled) {
        ScopedTextRouteLogCapture capture;
        RenderCounters counters;
        profiling::ProfilingGuard profiling_guard(&counters, nullptr);

        graph::RenderGraphContext context;
        context.policy.diagnostics_enabled = diagnostics_enabled;
        context.frame_input.frame = Frame{17};
        TextRunShape shape;
        shape.animators.emplace_back(); // invalid/unlowerable: route rejects before resource access
        Framebuffer destination(1, 1);
        const auto result = draw_cached_text_run(
            context, destination, shape, glm::mat4{1.0f}, 1.0f);

        CHECK_FALSE(result.ok());
        CHECK(counters.gpu_text_route_reject_count.load(std::memory_order_relaxed) == 1);
        return text_route_log_count(capture.sink());
    };

    CHECK(run_case(false) == 0);
    CHECK(run_case(true) == 1);
}

TEST_CASE("Cached GPU text policy names every route and reason") {
    using chronon3d::graph::text_run::detail::text_execution_route_name;
    using chronon3d::graph::text_run::detail::text_route_reason_name;

    CHECK(std::string(text_execution_route_name(TextExecutionRoute::NativeMtsdf)) ==
          "native_mtsdf");
    CHECK(std::string(text_execution_route_name(TextExecutionRoute::Software)) ==
          "software");
    CHECK(std::string(text_execution_route_name(TextExecutionRoute::Reject)) == "reject");
    CHECK(std::string(text_route_reason_name(
              TextRouteReason::NativeSurfacesUnavailable)) ==
          "native_surfaces_unavailable");
}
