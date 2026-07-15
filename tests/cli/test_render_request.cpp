#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/render_job.hpp>
#include <chronon3d/core/types/frame.hpp>

#include "utils/job/render_job.hpp"

#include <doctest/doctest.h>

using namespace chronon3d;
using namespace chronon3d::cli;

namespace {

CompositionRegistry make_test_registry() {
    CompositionRegistry registry;
    registry.add(TypedCompositionDescriptor<int>{
        .id = "test-comp",
        .category = "test",
        .defaults = 0,
        .resolve_metadata = [](int) {
            return CompositionMetadata{1920, 1080, FrameRate{30, 1}, Frame{300}};
        },
        .factory = [](int) {
            return composition({.name = "test-comp",
                                .width = 1920,
                                .height = 1080,
                                .duration = Frame{300}},
                               [](const FrameContext&) {});
        }
    }.to_descriptor());
    return registry;
}

} // namespace

TEST_CASE("resolve_render_request resolves composition and metadata") {
    auto registry = make_test_registry();

    RenderRequest request;
    request.comp_id = "test-comp";
    request.mode = RenderMode::Sequence;
    request.first_frame = Frame{0};
    request.last_frame = Frame{299};
    request.output = "output/test.png";

    auto result = resolve_render_request(registry, std::move(request));
    REQUIRE(result.has_value());
    CHECK(result->comp != nullptr);
    CHECK(result->metadata.width == 1920);
    CHECK(result->metadata.height == 1080);
    CHECK(result->metadata.fps == FrameRate{30, 1});
    CHECK(result->metadata.duration == Frame{300});
    CHECK(result->request.comp_id == "test-comp");
}

TEST_CASE("resolve_render_request returns error for unknown composition") {
    auto registry = make_test_registry();

    RenderRequest request;
    request.comp_id = "unknown";

    auto result = resolve_render_request(registry, std::move(request));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == RenderJobErrorCode::ValidationFailed);
}

TEST_CASE("ResolvedRenderJob::to_legacy_job produces valid RenderJob") {
    auto registry = make_test_registry();

    RenderRequest request;
    request.comp_id = "test-comp";
    request.mode = RenderMode::Still;
    request.still_frame = Frame{10};
    request.output = "output/test.png";

    auto resolved = resolve_render_request(registry, std::move(request));
    REQUIRE(resolved.has_value());

    RenderJob job = resolved->to_legacy_job();
    CHECK(job.comp != nullptr);
    CHECK(job.comp_id == "test-comp");
    CHECK(job.mode == RenderMode::Still);
    CHECK(job.still_frame == Frame{10});
    CHECK(job.output == "output/test.png");
    CHECK(job.registry == &registry);
}
