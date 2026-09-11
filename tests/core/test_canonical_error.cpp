#include <doctest/doctest.h>

#include <chronon3d/core/error.hpp>
#include <chronon3d/runtime/frame_execution_result.hpp>
#include <chronon3d/runtime/job/job_session.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/sdk/render_error.hpp>

TEST_CASE("canonical error taxonomy keeps domain detail payloads") {
    const auto error = chronon::from_legacy<chronon3d::runtime::ExecutionFailure>(
        chronon::ErrorKind::FrameFailed, "test",
        "chronon3d::runtime::ExecutionFailure", "BackendFailure",
        "backend operation failed", "test_backend");

    CHECK(error.category == chronon::ErrorCategory::FrameFailed);
    CHECK(error.detail.domain == "test");
    CHECK(error.detail.legacy_type == "chronon3d::runtime::ExecutionFailure");
    CHECK(error.detail.legacy_code == "BackendFailure");
    CHECK(error.detail.component == "test_backend");
    CHECK(chronon::kLegacyErrorMappings.size() == 28);
    CHECK(chronon::is_registered_legacy_type(
        "chronon3d::runtime::ExecutionFailure"));
    CHECK_FALSE(chronon::is_registered_legacy_type(
        "chronon3d::runtime::ResourcePreparationError"));
}

TEST_CASE("canonical error mapping rejects unregistered legacy types") {
    enum class UnregisteredCode { Failure };
    const auto error = chronon::from_legacy<UnregisteredCode>(
        chronon::ErrorCategory::FrameFailed,
        "test", "chronon3d::test::UnregisteredCode", "Failure", "boom");

    CHECK(error.category == chronon::ErrorCategory::InvariantViolation);
    CHECK(error.detail.legacy_code == "UnregisteredLegacyErrorType");
}

TEST_CASE("JobSession promotes a frame error to the canonical failed job state") {
    chronon3d::runtime::JobSession session;
    session.begin_job();
    session.fail_job(chronon::make_error(
        chronon::ErrorKind::FrameFailed, "render_backend", "ExecutionFailure",
        "draw operation failed", "test"));

    CHECK(session.state() == chronon3d::runtime::JobState::Failed);
    REQUIRE(session.job_error().has_value());
    CHECK(session.job_error()->category == chronon::ErrorCategory::FrameFailed);
    CHECK(session.job_error()->detail.legacy_code == "ExecutionFailure");

    session.begin_job();
    CHECK(session.state() == chronon3d::runtime::JobState::Running);
    CHECK_FALSE(session.job_error().has_value());
}

TEST_CASE("SDK legacy RenderError maps to canonical control semantics") {
    chronon3d::sdk::RenderError legacy{
        .code = chronon3d::sdk::RenderErrorCode::Cancelled,
        .message = "cancelled by caller",
        .component = "sdk",
    };
    const auto canonical = legacy.canonical_error();
    CHECK(canonical.category == chronon::ErrorCategory::Cancelled);
    CHECK(canonical.detail.legacy_type ==
          "chronon3d::sdk::RenderErrorCode");
    CHECK(canonical.detail.legacy_code == "Cancelled");
}

TEST_CASE("cancellation and device loss use the canonical taxonomy") {
    const auto cancellation = chronon3d::runtime::FrameExecutionResult::cancelled(
        "cancelled by caller");
    CHECK(cancellation.error.category == chronon::ErrorCategory::Cancelled);

    const auto cancelled = chronon3d::runtime::FrameExecutionResult::failed(
        chronon3d::runtime::ExecutionFailure::DecodeFailure,
        "cancelled by caller");
    CHECK(cancelled.error.category == chronon::ErrorCategory::FrameFailed);

    const auto device_lost = chronon3d::runtime::FrameExecutionResult::failed(
        chronon3d::runtime::ExecutionFailure::DeviceLost,
        "device reset");
    CHECK(device_lost.error.category == chronon::ErrorCategory::DeviceLost);

    auto explicit_cancel = chronon::make_error(
        chronon::ErrorKind::Cancelled, "runtime", "Cancelled",
        "cancelled by caller", "executor");
    CHECK(explicit_cancel.is_cancelled());
}

TEST_CASE("backend failure stops the frame and transitions the job") {
    chronon3d::runtime::JobSession session;
    session.begin_job();
    const chronon3d::graph::NodeExecutionError backend_failure{
        chronon3d::graph::RenderBackendErrorCode::ExecutionFailure,
        "text_run", "draw_text_run failed"};

    const auto frame = chronon3d::runtime::FrameExecutionResult::failed(
        backend_failure.canonical_error(12, 99));
    CHECK_FALSE(frame.ok());
    CHECK(frame.error.category == chronon::ErrorCategory::FrameFailed);
    session.fail_job(frame.error);

    CHECK(session.state() == chronon3d::runtime::JobState::Failed);
    REQUIRE(session.job_error().has_value());
    CHECK(session.job_error()->detail.node_id == "text_run");
    CHECK(session.job_error()->detail.frame == 12);
    CHECK(session.job_error()->detail.job_id == 99);
}
