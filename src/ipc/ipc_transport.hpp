// ---------------------------------------------------------------------------
// src/ipc/ipc_transport.hpp — Abstract transport layer for daemon IPC
//
// ADR-024: The IPC boundary separates transport (how bytes move) from codec
// (what format) from dispatch (what to do).  Transport implementations
// (UnixSocketTransport, later SharedMemoryTransport) implement this
// interface so the daemon server loop never depends on the physical medium.
// ---------------------------------------------------------------------------

#pragma once

#include <chronon3d/core/config.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::ipc {

// ── Wire-frame abstraction ─────────────────────────────────────────────────

/// An opaque block of bytes ready to send/receive on the transport.
/// Transport-level only — never parsed by the transport layer.
using WireFrame = std::vector<std::uint8_t>;

/// Server handler result: the reply wire-frame plus a flag telling the
/// transport that this reply is a terminal Shutdown acknowledgment after
/// which the server must stop accepting/serving clients.
///
/// A reply that is empty (but not a shutdown) tells the transport to close
/// this client's connection without sending anything.
struct ServerReply {
    WireFrame reply;
    bool shutdown{false};
};

/// Callback invoked by the server when a complete frame arrives from a
/// client.  Returns the reply wire-frame (or empty vector on error) plus the
/// shutdown signal.
using FrameHandler = std::function<ServerReply(const WireFrame&)>;

// ── Transport interface ────────────────────────────────────────────────────

class IpcTransport {
public:
    virtual ~IpcTransport() = default;

    /// Bind + listen on the given address.  For Unix sockets this is a
    /// filesystem path; for shared memory it could be a segment name.
    /// Throws std::system_error on failure.
    virtual void listen(std::string_view address) = 0;

    /// Accept + serve loop.  Connections are served concurrently (each client
    /// owns a serving thread) while handler dispatch is serialized, so an
    /// idle connection can never block other clients.  Serving continues
    /// until a handler returns ServerReply::shutdown == true (the reply is
    /// delivered first), the transport is closed, or a transport error
    /// occurs.  Returns 0 on clean exit (including Shutdown), or a positive
    /// errno on failure.
    [[nodiscard]] virtual int serve(const FrameHandler& handler) noexcept = 0;

    /// Close the transport and release resources.  No-op if already closed.
    virtual void close() noexcept = 0;

    /// Transport address (empty until listen() succeeds).
    [[nodiscard]] virtual const std::string& address() const noexcept = 0;
};

// ── Client-side transport ──────────────────────────────────────────────────

/// Client-side transport for sending requests and receiving replies.
/// Used by the out-of-tree RenderingGen client and integration tests.
class IpcClientTransport {
public:
    virtual ~IpcClientTransport() = default;

    /// Connect to a daemon at `address`.  Throws std::system_error on failure.
    virtual void connect(std::string_view address) = 0;

    /// Send a request frame and return the reply frame (or nullopt on error).
    [[nodiscard]] virtual std::optional<WireFrame> request(const WireFrame& frame) noexcept = 0;

    /// Close the connection.
    virtual void close() noexcept = 0;
};

} // namespace chronon3d::ipc