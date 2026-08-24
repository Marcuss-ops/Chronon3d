// ---------------------------------------------------------------------------
// src/ipc/ipc_codec.hpp — FlatBuffers IPC codec
// ---------------------------------------------------------------------------

#pragma once

#include "ipc_transport.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace chronon3d::ipc {

// ── Typed request/reply types (Ipc prefix avoids FlatBuffers name collision) ─

struct IpcCreateComposition {
    std::string composition_id;
    std::string descriptor_json;
};

struct IpcRenderFrame {
    std::string composition_id;
    std::uint32_t frame_index{0};
    std::string output_path;
    std::vector<std::pair<std::string, std::string>> parameters;
};

struct IpcStatusRequest {};
struct IpcShutdown {};

using IpcRequest = std::variant<
    IpcCreateComposition,
    IpcRenderFrame,
    IpcStatusRequest,
    IpcShutdown>;

struct IpcCreateCompositionResult {
    std::uint8_t status{0};
    std::string message;
};

struct IpcRenderFrameResult {
    std::uint8_t status{0};
    std::string message;
    std::string output_path;
    float render_ms{0.0f};
};

struct IpcStatusResult {
    std::uint8_t status{0};
    std::string message;
};

struct IpcShutdownResult {
    std::uint8_t status{0};
    std::string message;
};

using IpcResponse = std::variant<
    IpcCreateCompositionResult,
    IpcRenderFrameResult,
    IpcStatusResult,
    IpcShutdownResult>;

/// Encode/decode FlatBuffers ↔ WireFrame.
class IpcCodec {
public:
    [[nodiscard]] static WireFrame encode_request(
        std::uint64_t message_id, const IpcRequest& request);

    [[nodiscard]] static WireFrame encode_reply(
        std::uint64_t message_id, const IpcResponse& response);

    [[nodiscard]] static std::optional<std::pair<std::uint64_t, IpcRequest>>
    decode_request(const WireFrame& frame);

    [[nodiscard]] static std::optional<std::pair<std::uint64_t, IpcResponse>>
    decode_reply(const WireFrame& frame);
};

} // namespace chronon3d::ipc