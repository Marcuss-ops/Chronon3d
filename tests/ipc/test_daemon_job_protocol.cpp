#include <doctest/doctest.h>

#include "src/daemon/daemon_runtime.hpp"
#include "src/ipc/ipc_command_dispatcher.hpp"

#include <chronon3d/core/config.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

std::optional<chronon3d::ipc::IpcResponse> simulated_request(
    chronon3d::ipc::IpcCommandDispatcher& dispatcher,
    std::uint64_t message_id,
    const chronon3d::ipc::IpcRequest& request) {
    using namespace chronon3d::ipc;
    const auto wire_request = IpcCodec::encode_request(message_id, request);
    auto decoded_request = IpcCodec::decode_request(wire_request);
    if (!decoded_request || decoded_request->first != message_id) return std::nullopt;

    const auto response = dispatcher.dispatch(decoded_request->second);
    auto decoded_response = IpcCodec::decode_reply(
        IpcCodec::encode_reply(message_id, response));
    if (!decoded_response || decoded_response->first != message_id) return std::nullopt;
    return std::move(decoded_response->second);
}

std::string descriptor_json() {
    return R"({"schema":"chronon.composition","version":1,"id":"job-ipc-test",
        "category":"test","width":64,"height":64,
        "fps":{"numerator":30,"denominator":1},"duration":2})";
}

} // namespace

TEST_CASE("daemon job lifecycle and health round-trip through simulated FlatBuffers IPC") {
    using namespace chronon3d;
    using namespace chronon3d::ipc;

    Config config;
    config.set_backend_preference(graph::BackendPreference::Software);
    auto engine = std::make_unique<RenderEngine>(std::move(config));
    CompositionRegistry registry;
    engine->set_composition_registry(&registry);
    IpcCommandDispatcher dispatcher(std::move(engine));
    dispatcher.warmup();

    const auto health = simulated_request(dispatcher, 1, IpcHealthRequest{});
    REQUIRE(health);
    REQUIRE(std::holds_alternative<IpcHealthResult>(*health));
    CHECK(std::get<IpcHealthResult>(*health).status == IpcStatus_Ok);
    CHECK(std::get<IpcHealthResult>(*health).message.find("\"healthy\":true") !=
          std::string::npos);

    const std::string job_id = "job-ipc-test";
    const auto prepare = simulated_request(dispatcher, 2,
        IpcPrepareJob{job_id, descriptor_json()});
    REQUIRE(prepare);
    REQUIRE(std::holds_alternative<IpcJobResult>(*prepare));
    INFO(std::get<IpcJobResult>(*prepare).message);
    CHECK(std::get<IpcJobResult>(*prepare).status == IpcStatus_Ok);
    CHECK(std::get<IpcJobResult>(*prepare).state == JobState_Prepared);

    const auto prepared_status = simulated_request(dispatcher, 3,
        IpcGetJobStatus{job_id});
    REQUIRE(prepared_status);
    REQUIRE(std::holds_alternative<IpcJobResult>(*prepared_status));
    CHECK(std::get<IpcJobResult>(*prepared_status).state == JobState_Prepared);

    const auto rendered = simulated_request(dispatcher, 4,
        IpcRenderJob{job_id, 0});
    REQUIRE(rendered);
    REQUIRE(std::holds_alternative<IpcJobResult>(*rendered));
    CHECK(std::get<IpcJobResult>(*rendered).status == IpcStatus_Ok);
    CHECK(std::get<IpcJobResult>(*rendered).state == JobState_Completed);
    CHECK(std::get<IpcJobResult>(*rendered).last_frame == 0);

    const auto completed_status = simulated_request(dispatcher, 5,
        IpcGetJobStatus{job_id});
    REQUIRE(completed_status);
    CHECK(std::get<IpcJobResult>(*completed_status).state == JobState_Completed);
    CHECK(std::get<IpcJobResult>(*completed_status).last_frame == 0);
    CHECK(std::get<IpcJobResult>(*completed_status).render_ms >= 0.0f);

    const auto duplicate_bad_prepare = simulated_request(dispatcher, 13,
        IpcPrepareJob{job_id, "{}"});
    REQUIRE(duplicate_bad_prepare);
    CHECK(std::get<IpcJobResult>(*duplicate_bad_prepare).status == IpcStatus_Error);
    CHECK(std::get<IpcJobResult>(*duplicate_bad_prepare).state == JobState_Completed);
    const auto status_after_duplicate = simulated_request(dispatcher, 14,
        IpcGetJobStatus{job_id});
    REQUIRE(status_after_duplicate);
    CHECK(std::get<IpcJobResult>(*status_after_duplicate).state == JobState_Completed);

    const auto released = simulated_request(dispatcher, 6,
        IpcReleaseJob{job_id});
    REQUIRE(released);
    REQUIRE(std::holds_alternative<IpcJobResult>(*released));
    CHECK(std::get<IpcJobResult>(*released).state == JobState_Released);

    const auto released_status = simulated_request(dispatcher, 7,
        IpcGetJobStatus{job_id});
    REQUIRE(released_status);
    CHECK(std::get<IpcJobResult>(*released_status).state == JobState_Released);

    const auto render_after_release = simulated_request(dispatcher, 8,
        IpcRenderJob{job_id, 1});
    REQUIRE(render_after_release);
    CHECK(std::get<IpcJobResult>(*render_after_release).status == IpcStatus_NotPrepared);

    const auto bad_prepare = simulated_request(dispatcher, 11,
        IpcPrepareJob{"failed-job", "{}"});
    REQUIRE(bad_prepare);
    REQUIRE(std::holds_alternative<IpcJobResult>(*bad_prepare));
    CHECK(std::get<IpcJobResult>(*bad_prepare).status == IpcStatus_Error);
    CHECK(std::get<IpcJobResult>(*bad_prepare).state == JobState_Failed);
    const auto failed_status = simulated_request(dispatcher, 12,
        IpcGetJobStatus{"failed-job"});
    REQUIRE(failed_status);
    CHECK(std::get<IpcJobResult>(*failed_status).state == JobState_Failed);

    for (std::uint32_t i = 0; i < 256; ++i) {
        const auto failure = simulated_request(dispatcher, 100 + i,
            IpcPrepareJob{"failed-history-" + std::to_string(i), "{}"});
        REQUIRE(failure);
        REQUIRE(std::holds_alternative<IpcJobResult>(*failure));
        CHECK(std::get<IpcJobResult>(*failure).state == JobState_Failed);
    }
    const auto expired_status = simulated_request(dispatcher, 400,
        IpcGetJobStatus{"failed-job"});
    REQUIRE(expired_status);
    CHECK(std::get<IpcJobResult>(*expired_status).status == IpcStatus_NotFound);
    const auto retained_status = simulated_request(dispatcher, 401,
        IpcGetJobStatus{"failed-history-255"});
    REQUIRE(retained_status);
    CHECK(std::get<IpcJobResult>(*retained_status).state == JobState_Failed);

    const auto shutdown = simulated_request(dispatcher, 402, IpcShutdown{});
    REQUIRE(shutdown);
    CHECK(std::get<IpcShutdownResult>(*shutdown).status == IpcStatus_Shutdown);
    const auto unhealthy = simulated_request(dispatcher, 403, IpcHealthRequest{});
    REQUIRE(unhealthy);
    CHECK(std::get<IpcHealthResult>(*unhealthy).status == IpcStatus_Error);
    CHECK(std::get<IpcHealthResult>(*unhealthy).message.find("\"healthy\":false") !=
          std::string::npos);
}
