#include <doctest/doctest.h>
#include "src/ipc/ipc_codec.hpp"
#include "src/ipc/unix_socket_transport.hpp"
#include <chronon3d/internal/testing/failure_injector.hpp>
#include <vector>
#include <random>

using namespace chronon3d::ipc;

TEST_SUITE("IPC.Codec") {

TEST_CASE("Round-trip all request types") {
    {
        IpcCreateComposition req;
        req.composition_id = "comp_test_1";
        req.descriptor_json = "{\"canvas\":{\"width\":1920,\"height\":1080}}";
        const auto encoded = IpcCodec::encode_request(101, req);
        const auto decoded = IpcCodec::decode_request(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->first == 101);
        REQUIRE(std::holds_alternative<IpcCreateComposition>(decoded->second));
        const auto& out = std::get<IpcCreateComposition>(decoded->second);
        CHECK(out.composition_id == req.composition_id);
        CHECK(out.descriptor_json == req.descriptor_json);
    }

    {
        IpcRenderFrame req;
        req.composition_id = "comp_test_2";
        req.frame_index = 42;
        req.parameters.emplace_back("opacity", "0.75");
        req.parameters.emplace_back("title", "Hello FlatBuffers");
        req.output_path = "/tmp/frame_42.png";
        const auto encoded = IpcCodec::encode_request(102, req);
        const auto decoded = IpcCodec::decode_request(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->first == 102);
        REQUIRE(std::holds_alternative<IpcRenderFrame>(decoded->second));
        const auto& out = std::get<IpcRenderFrame>(decoded->second);
        CHECK(out.composition_id == req.composition_id);
        CHECK(out.frame_index == 42);
        REQUIRE(out.parameters.size() == 2);
        CHECK(out.parameters[0].first == "opacity");
        CHECK(out.parameters[0].second == "0.75");
        CHECK(out.parameters[1].first == "title");
        CHECK(out.parameters[1].second == "Hello FlatBuffers");
        CHECK(out.output_path == "/tmp/frame_42.png");
    }

    {
        const auto encoded = IpcCodec::encode_request(103, IpcStatusRequest{});
        const auto decoded = IpcCodec::decode_request(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->first == 103);
        CHECK(std::holds_alternative<IpcStatusRequest>(decoded->second));
    }

    {
        const auto encoded = IpcCodec::encode_request(104, IpcShutdown{});
        const auto decoded = IpcCodec::decode_request(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->first == 104);
        CHECK(std::holds_alternative<IpcShutdown>(decoded->second));
    }
}

TEST_CASE("Round-trip all reply types using schema-owned status values") {
    {
        IpcCreateCompositionResult rep{IpcStatus_Ok, "Ready"};
        const auto encoded = IpcCodec::encode_reply(201, rep);
        const auto decoded = IpcCodec::decode_reply(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->first == 201);
        REQUIRE(std::holds_alternative<IpcCreateCompositionResult>(decoded->second));
        const auto& out = std::get<IpcCreateCompositionResult>(decoded->second);
        CHECK(out.status == IpcStatus_Ok);
        CHECK(out.message == "Ready");
    }

    {
        IpcRenderFrameResult rep{IpcStatus_Ok, "Rendered", "/tmp/out.png", 3.14f};
        const auto encoded = IpcCodec::encode_reply(202, rep);
        const auto decoded = IpcCodec::decode_reply(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->first == 202);
        REQUIRE(std::holds_alternative<IpcRenderFrameResult>(decoded->second));
        const auto& out = std::get<IpcRenderFrameResult>(decoded->second);
        CHECK(out.status == IpcStatus_Ok);
        CHECK(out.output_path == "/tmp/out.png");
        CHECK(out.render_ms == doctest::Approx(3.14f));
        CHECK(out.message == "Rendered");
    }

    {
        IpcStatusResult rep{IpcStatus_Ok, "{\"uptime\":123}"};
        const auto encoded = IpcCodec::encode_reply(203, rep);
        const auto decoded = IpcCodec::decode_reply(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->first == 203);
        REQUIRE(std::holds_alternative<IpcStatusResult>(decoded->second));
        const auto& out = std::get<IpcStatusResult>(decoded->second);
        CHECK(out.status == IpcStatus_Ok);
        CHECK(out.message == "{\"uptime\":123}");
    }

    {
        IpcShutdownResult rep{IpcStatus_Shutdown, "Bye"};
        const auto encoded = IpcCodec::encode_reply(204, rep);
        const auto decoded = IpcCodec::decode_reply(encoded);
        REQUIRE(decoded.has_value());
        CHECK(decoded->first == 204);
        REQUIRE(std::holds_alternative<IpcShutdownResult>(decoded->second));
        const auto& out = std::get<IpcShutdownResult>(decoded->second);
        CHECK(out.status == IpcStatus_Shutdown);
        CHECK(out.message == "Bye");
    }
}

TEST_CASE("IPC decoder rejects empty and truncated requests safely") {
    const auto valid = IpcCodec::encode_request(42, IpcStatusRequest{});
    CHECK(valid.size() > 0);

    for (std::size_t n = 0; n < valid.size(); ++n) {
        WireFrame truncated(valid.begin(), valid.begin() + n);
        CHECK_FALSE(IpcCodec::decode_request(truncated).has_value());
    }
}

TEST_CASE("IPC decoder rejects corrupted and random payloads without crashing") {
    std::mt19937 rng(1337);
    std::uniform_int_distribution<int> byte_dist(0, 255);
    std::uniform_int_distribution<std::size_t> len_dist(1, 512);

    for (int iter = 0; iter < 1000; ++iter) {
        const auto len = len_dist(rng);
        WireFrame fuzz(len);
        for (auto& b : fuzz) b = static_cast<std::uint8_t>(byte_dist(rng));
        CHECK_NOTHROW(IpcCodec::decode_request(fuzz));
        CHECK_NOTHROW(IpcCodec::decode_reply(fuzz));
    }
}

TEST_CASE("LengthPrefixFraming handles framing bounds and malformed sockets") {
    int sv[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    WireFrame valid_payload = {0x01, 0x02, 0x03, 0x04};
    CHECK(LengthPrefixFraming::write_frame(sv[0], valid_payload));
    auto read_back = LengthPrefixFraming::read_frame(sv[1]);
    REQUIRE(read_back.has_value());
    CHECK(*read_back == valid_payload);

    std::uint8_t zero_header[4] = {0, 0, 0, 0};
    REQUIRE(::write(sv[0], zero_header, 4) == 4);
    CHECK_FALSE(LengthPrefixFraming::read_frame(sv[1]).has_value());

    std::uint8_t giant_header[4] = {0x05, 0x00, 0x00, 0x00};
    REQUIRE(::write(sv[0], giant_header, 4) == 4);
    CHECK_FALSE(LengthPrefixFraming::read_frame(sv[1]).has_value());

    std::uint8_t partial_hdr[2] = {0x00, 0x01};
    REQUIRE(::write(sv[0], partial_hdr, 2) == 2);
    ::close(sv[0]);
    CHECK_FALSE(LengthPrefixFraming::read_frame(sv[1]).has_value());
    ::close(sv[1]);
}

TEST_CASE("LengthPrefixFraming fault injection fails one write and recovers") {
    using chronon3d::testing::FailureInjector;
    using chronon3d::testing::FailurePoint;
    int sv[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    FailureInjector::fail_next(FailurePoint::SocketWrite);
    CHECK_FALSE(LengthPrefixFraming::write_frame(sv[0], WireFrame{1, 2, 3}));
    CHECK(LengthPrefixFraming::write_frame(sv[0], WireFrame{4, 5, 6}));
    CHECK(LengthPrefixFraming::read_frame(sv[1]) == WireFrame{4, 5, 6});
    FailureInjector::reset();
    ::close(sv[0]);
    ::close(sv[1]);
}

TEST_CASE("LengthPrefixFraming fault injection fails one read and recovers") {
    using chronon3d::testing::FailureInjector;
    using chronon3d::testing::FailurePoint;
    int sv[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    REQUIRE(LengthPrefixFraming::write_frame(sv[0], WireFrame{1, 2, 3}));
    FailureInjector::fail_next(FailurePoint::SocketRead);
    CHECK_FALSE(LengthPrefixFraming::read_frame(sv[1]).has_value());
    FailureInjector::reset();
    ::close(sv[0]);
    ::close(sv[1]);
}

} // TEST_SUITE
