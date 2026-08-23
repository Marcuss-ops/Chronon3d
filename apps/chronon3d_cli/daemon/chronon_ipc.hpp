#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// chronon_ipc.hpp — Unix-domain socket IPC for the persistent Chronon3d
// render daemon (RenderingGen → Chronon).
//
// The daemon keeps a warm RenderEngine (framebuffer pools, font engines,
// glyph atlases, image caches, node caches, and — on GPU hosts — the device,
// pipeline cache and VRAM cache) alive across hundreds of jobs.  A client
// (RenderingGen) speaks a small length-prefixed binary protocol over a
// UNIX-domain stream socket:
//
//   Request frame = MAGIC(u32) | COMMAND(u32) | PAYLOAD_LEN(u32) | PAYLOAD
//   Reply   frame = MAGIC(u32) | STATUS(u32)  | MESSAGE_LEN(u32) | MESSAGE
//
// All multi-byte integers are big-endian (network byte order) so the wire
// format is independent of host endianness.
//
// Command payloads:
//   PREFETCH_ASSET  <asset-path>              warm the asset cache
//   PREPARE_PLAN    <composition-id>          compile + plan once
//   RENDER_OVERLAY  "<frame> [output-path]"   render + save a frame
//   RENDER_JOB      <json>                    render a chronon.render-plan.v1
//                                            file: {"plan_path", "assets_root",
//                                            "output", "backend", "report"}; replies JSON
//                                            {"status":"ok","output":"..."}
//   STATUS          (empty)                   engine statistics
//   SHUTDOWN        (empty)                   stop serving
//
// This header is deliberately self-contained (C++ standard library + POSIX
// sockets only — no spdlog, no Vulkan, no Chronon3d headers) so both the
// in-repo daemon and an out-of-repo RenderingGen client can share the same
// wire contract without pulling in the full dependency graph.
// ═══════════════════════════════════════════════════════════════════════════

#include <cerrno>
#include <atomic>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <mutex>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
// Non-Linux platforms that lack MSG_NOSIGNAL fall back to plain send();
// SIGPIPE suppression there is the caller's responsibility.
#define MSG_NOSIGNAL 0
#endif

namespace chronon3d::cli::ipc {

// ── Wire constants ─────────────────────────────────────────────────────────

inline constexpr std::uint32_t kProtocolMagic  = 0x43484e33u;  // "CHN3"
inline constexpr std::uint32_t kProtocolVersion = 1u;
inline constexpr std::size_t   kHeaderBytes     = 12u;         // 3 × u32
inline constexpr std::size_t   kMaxPayloadBytes = 64u * 1024u * 1024u;  // 64 MiB

enum class Command : std::uint32_t {
    PrefetchAsset = 1,   ///< payload: asset path → warm asset cache
    PreparePlan   = 2,   ///< payload: composition id → compile + plan once
    RenderOverlay = 3,   ///< payload: "<frame> [output]" → render + save
    Status        = 4,   ///< no payload → engine statistics
    Shutdown      = 5,   ///< no payload → stop serving
    RenderJob     = 6,   ///< payload: JSON {plan_path, assets_root, output}
};

enum class Status : std::uint32_t {
    Ok         = 0,
    Error      = 1,
    NotFound   = 2,
    BadRequest = 3,
    Shutdown   = 4,      ///< acknowledged shutdown
};

// ── Framing codec (pure functions — shared by daemon + client) ─────────────

struct Request {
    Command     cmd{Command::Status};
    std::string payload;
};

struct Reply {
    Status      status{Status::Ok};
    std::string message;
};

namespace detail {

inline std::uint32_t read_be32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8)  |
            static_cast<std::uint32_t>(p[3]);
}

inline void write_be32(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>((v >> 24) & 0xffu);
    p[1] = static_cast<std::uint8_t>((v >> 16) & 0xffu);
    p[2] = static_cast<std::uint8_t>((v >> 8)  & 0xffu);
    p[3] = static_cast<std::uint8_t>( v        & 0xffu);
}

/// Read exactly `n` bytes from a stream socket.  Handles EINTR and partial
/// reads.  Returns false on EOF (clean disconnect) or transport error.
inline bool read_exact(int fd, std::uint8_t* buf, std::size_t n) noexcept {
    std::size_t done = 0;
    while (done < n) {
        const ssize_t r = ::recv(fd, buf + done, n - done, 0);
        if (r > 0) {
            done += static_cast<std::size_t>(r);
            continue;
        }
        if (r == 0) return false;              // EOF
        if (errno == EINTR) continue;          // retry after signal
        return false;
    }
    return true;
}

/// Write exactly `n` bytes to a stream socket.  Handles EINTR and partial
/// writes.  Returns false on transport error.
inline bool write_exact(int fd, const std::uint8_t* buf, std::size_t n) noexcept {
    std::size_t done = 0;
    while (done < n) {
        const ssize_t w = ::send(fd, buf + done, n - done, MSG_NOSIGNAL);
        if (w > 0) {
            done += static_cast<std::size_t>(w);
            continue;
        }
        if (errno == EINTR) continue;
        return false;
    }
    return true;
}

} // namespace detail

/// Serialize a request into a wire frame (magic + command + payload-len + payload).
inline std::vector<std::uint8_t> encode_request(Command cmd,
                                                std::string_view payload = {}) {
    std::vector<std::uint8_t> out(kHeaderBytes + payload.size());
    detail::write_be32(out.data() + 0, kProtocolMagic);
    detail::write_be32(out.data() + 4, static_cast<std::uint32_t>(cmd));
    detail::write_be32(out.data() + 8, static_cast<std::uint32_t>(payload.size()));
    if (!payload.empty()) {
        std::memcpy(out.data() + kHeaderBytes, payload.data(), payload.size());
    }
    return out;
}

/// Parse + validate a request frame.  Returns nullopt on malformed input.
inline std::optional<Request> decode_request(const std::uint8_t* data,
                                             std::size_t len) noexcept {
    if (data == nullptr || len < kHeaderBytes) return std::nullopt;
    if (detail::read_be32(data) != kProtocolMagic) return std::nullopt;
    const std::uint32_t cmd  = detail::read_be32(data + 4);
    const std::uint32_t plen = detail::read_be32(data + 8);
    if (plen > kMaxPayloadBytes || len != kHeaderBytes + plen) return std::nullopt;
    Request req;
    req.cmd = static_cast<Command>(cmd);
    req.payload.assign(reinterpret_cast<const char*>(data + kHeaderBytes), plen);
    return req;
}

/// Serialize a reply into a wire frame (magic + status + message-len + message).
inline std::vector<std::uint8_t> encode_reply(Status status,
                                              std::string_view message = {}) {
    std::vector<std::uint8_t> out(kHeaderBytes + message.size());
    detail::write_be32(out.data() + 0, kProtocolMagic);
    detail::write_be32(out.data() + 4, static_cast<std::uint32_t>(status));
    detail::write_be32(out.data() + 8, static_cast<std::uint32_t>(message.size()));
    if (!message.empty()) {
        std::memcpy(out.data() + kHeaderBytes, message.data(), message.size());
    }
    return out;
}

/// Parse + validate a reply frame.  Returns nullopt on malformed input.
inline std::optional<Reply> decode_reply(const std::uint8_t* data,
                                         std::size_t len) noexcept {
    if (data == nullptr || len < kHeaderBytes) return std::nullopt;
    if (detail::read_be32(data) != kProtocolMagic) return std::nullopt;
    const std::uint32_t status = detail::read_be32(data + 4);
    const std::uint32_t mlen   = detail::read_be32(data + 8);
    if (mlen > kMaxPayloadBytes || len != kHeaderBytes + mlen) return std::nullopt;
    Reply rep;
    rep.status = static_cast<Status>(status);
    rep.message.assign(reinterpret_cast<const char*>(data + kHeaderBytes), mlen);
    return rep;
}

// ── Socket transport ───────────────────────────────────────────────────────

using RequestHandler = std::function<Reply(const Request&)>;

/// Blocking UNIX-domain socket server.  Binds + listens on a filesystem
/// path, then serves clients one connection at a time until a handler
/// returns `Status::Shutdown` (or a transport error occurs).
class UnixSocketServer {
public:
    UnixSocketServer() = default;
    ~UnixSocketServer() { close(); }
    UnixSocketServer(const UnixSocketServer&) = delete;
    UnixSocketServer& operator=(const UnixSocketServer&) = delete;

    /// Bind + listen on `path`.  Throws std::system_error on failure.
    /// A stale socket file at `path` from a previous run is unlinked first.
    void listen(std::string_view path) {
        close();
        m_path = std::string(path);
        m_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_fd < 0) {
            m_path.clear();
            throw std::system_error(errno, std::generic_category(), "ipc::socket");
        }
        ::unlink(m_path.c_str());  // remove stale socket, if any
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (m_path.size() >= sizeof(addr.sun_path)) {
            close();
            throw std::system_error(EOVERFLOW, std::generic_category(),
                                    "ipc::socket path too long");
        }
        std::memcpy(addr.sun_path, m_path.c_str(), m_path.size() + 1);
        if (::bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            const int e = errno;
            close();
            throw std::system_error(e, std::generic_category(), "ipc::bind");
        }
        if (::listen(m_fd, /*backlog=*/8) != 0) {
            const int e = errno;
            close();
            throw std::system_error(e, std::generic_category(), "ipc::listen");
        }
    }

    /// Blocking accept + serve loop.  Returns 0 on clean shutdown,
    /// or a positive errno value on transport error.
    int serve(const RequestHandler& handler) noexcept {
        for (;;) {
            const int client = ::accept(m_fd, nullptr, nullptr);
            if (client < 0) {
                if (errno == EINTR) continue;
                return errno;
            }
            bool shutdown = false;
            while (serve_once(client, handler, &shutdown)) {}
            ::close(client);
            if (shutdown) return 0;
        }
    }

    /// Concurrent ingress variant. Each client owns its transport thread;
    /// the handler decides its own execution/session serialization. This
    /// keeps socket acceptance independent from a long render request.
    int serve_concurrent(const RequestHandler& handler) noexcept {
        std::atomic<bool> shutdown_requested{false};
        struct ActiveClients {
            std::mutex mutex;
            std::vector<int> fds;
        };
        const auto active = std::make_shared<ActiveClients>();
        std::vector<std::thread> workers;
        while (!shutdown_requested.load(std::memory_order_acquire)) {
            pollfd ready{};
            ready.fd = m_fd;
            ready.events = POLLIN;
            const int polled = ::poll(&ready, 1, 100);
            if (polled < 0) {
                if (errno == EINTR) continue;
                return errno;
            }
            if (polled == 0) continue;
            const int client = ::accept(m_fd, nullptr, nullptr);
            if (client < 0) {
                if (errno == EINTR) continue;
                return errno;
            }
            {
                std::lock_guard lock(active->mutex);
                active->fds.push_back(client);
            }
            workers.emplace_back([client, &handler, &shutdown_requested, active]() {
                bool shutdown = false;
                while (serve_once(client, handler, &shutdown)) {}
                ::close(client);
                {
                    std::lock_guard lock(active->mutex);
                    active->fds.erase(std::remove(active->fds.begin(), active->fds.end(), client),
                                      active->fds.end());
                }
                if (shutdown) {
                    shutdown_requested.store(true, std::memory_order_release);
                    std::lock_guard lock(active->mutex);
                    for (const int fd : active->fds) {
                        if (fd != client) ::shutdown(fd, SHUT_RDWR);
                    }
                }
            });
        }
        for (auto& worker : workers) {
            if (worker.joinable()) worker.join();
        }
        return 0;
    }

    /// Bind path (empty until listen() succeeds).
    [[nodiscard]] const std::string& path() const noexcept { return m_path; }

    /// Close the socket and unlink the socket file.
    void close() noexcept {
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
        if (!m_path.empty()) {
            ::unlink(m_path.c_str());
            m_path.clear();
        }
    }

private:
    /// Read one frame, dispatch it, write the reply.  Returns false on EOF
    /// or transport error; sets *shutdown when the handler returned Shutdown.
    static bool serve_once(int client_fd, const RequestHandler& handler,
                           bool* shutdown) noexcept {
        std::uint8_t header[kHeaderBytes];
        if (!detail::read_exact(client_fd, header, kHeaderBytes)) return false;
        if (detail::read_be32(header) != kProtocolMagic) {
            const auto rep = encode_reply(Status::BadRequest, "bad magic");
            detail::write_exact(client_fd, rep.data(), rep.size());
            return true;
        }
        const std::uint32_t cmd  = detail::read_be32(header + 4);
        const std::uint32_t plen = detail::read_be32(header + 8);
        if (plen > kMaxPayloadBytes) {
            const auto rep = encode_reply(Status::BadRequest, "payload too large");
            detail::write_exact(client_fd, rep.data(), rep.size());
            return true;
        }
        std::string payload(plen, '\0');
        if (plen != 0 &&
            !detail::read_exact(client_fd,
                                reinterpret_cast<std::uint8_t*>(payload.data()),
                                plen)) {
            return false;
        }
        Request req{static_cast<Command>(cmd), std::move(payload)};
        const Reply rep = handler(req);
        const auto bytes = encode_reply(rep.status, rep.message);
        if (!detail::write_exact(client_fd, bytes.data(), bytes.size())) return false;
        // On shutdown, stop reading further frames from this client so the
        // serve() loop can close the connection and exit immediately.
        if (rep.status == Status::Shutdown) {
            *shutdown = true;
            return false;
        }
        return true;
    }

    int         m_fd{-1};
    std::string m_path;
};

/// Blocking UNIX-domain socket client (RenderingGen side / tests).
class UnixSocketClient {
public:
    UnixSocketClient() = default;
    ~UnixSocketClient() { close(); }
    UnixSocketClient(const UnixSocketClient&) = delete;
    UnixSocketClient& operator=(const UnixSocketClient&) = delete;

    /// Connect to a server socket at `path`.  Throws std::system_error on failure.
    void connect(std::string_view path) {
        close();
        m_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_fd < 0) {
            throw std::system_error(errno, std::generic_category(), "ipc::socket");
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (path.size() >= sizeof(addr.sun_path)) {
            close();
            throw std::system_error(EOVERFLOW, std::generic_category(),
                                    "ipc::socket path too long");
        }
        std::memcpy(addr.sun_path, path.data(), path.size());
        addr.sun_path[path.size()] = '\0';
        if (::connect(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            const int e = errno;
            close();
            throw std::system_error(e, std::generic_category(), "ipc::connect");
        }
    }

    /// Send a request and read the matching reply.  Returns nullopt on
    /// transport failure.
    std::optional<Reply> request(Command cmd, std::string_view payload = {}) noexcept {
        const auto frame = encode_request(cmd, payload);
        if (!detail::write_exact(m_fd, frame.data(), frame.size())) return std::nullopt;
        std::uint8_t header[kHeaderBytes];
        if (!detail::read_exact(m_fd, header, kHeaderBytes)) return std::nullopt;
        if (detail::read_be32(header) != kProtocolMagic) return std::nullopt;
        const std::uint32_t status = detail::read_be32(header + 4);
        const std::uint32_t mlen   = detail::read_be32(header + 8);
        if (mlen > kMaxPayloadBytes) return std::nullopt;
        std::string message(mlen, '\0');
        if (mlen != 0 &&
            !detail::read_exact(m_fd,
                                reinterpret_cast<std::uint8_t*>(message.data()),
                                mlen)) {
            return std::nullopt;
        }
        return Reply{static_cast<Status>(status), std::move(message)};
    }

    void close() noexcept {
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
    }

private:
    int m_fd{-1};
};

} // namespace chronon3d::cli::ipc
