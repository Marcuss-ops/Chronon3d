// ---------------------------------------------------------------------------
// src/ipc/unix_socket_transport.hpp
//
// Unix-domain socket transport implementing IpcTransport / IpcClientTransport.
//
// Wire format: a length-prefixed frame:
//   [4 bytes: frame_len (big-endian u32)] [frame_len bytes: FlatBuffers]
//
// This is independent of the FlatBuffers codec — the transport only moves
// opaque bytes.  The codec layer (ipc_codec.hpp) serializes/deserializes
// FlatBuffers into these wire frames.
// ---------------------------------------------------------------------------

#pragma once

#include "ipc_transport.hpp"
#include <chronon3d/internal/testing/failure_injector.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace chronon3d::ipc {

namespace detail {

/// Read exactly `n` bytes from a stream socket.  Handles EINTR and partial
/// reads.  Returns false on EOF or transport error.
inline bool socket_read_exact(int fd, std::uint8_t* buf, std::size_t n) noexcept {
    if (chronon3d::testing::FailureInjector::should_fail(
            chronon3d::testing::FailurePoint::SocketRead)) return false;
    std::size_t done = 0;
    while (done < n) {
        const ssize_t r = ::recv(fd, buf + done, n - done, 0);
        if (r > 0) {
            done += static_cast<std::size_t>(r);
            continue;
        }
        if (r == 0) return false;       // EOF
        if (errno == EINTR) continue;
        return false;
    }
    return true;
}

/// Write exactly `n` bytes to a stream socket.
inline bool socket_write_exact(int fd, const std::uint8_t* buf, std::size_t n) noexcept {
    if (chronon3d::testing::FailureInjector::should_fail(
            chronon3d::testing::FailurePoint::SocketWrite)) return false;
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

} // namespace detail

/// Simple length-prefixed framing over a Unix-domain stream socket.
/// Each frame: [u32 BE len][len raw bytes].
class LengthPrefixFraming {
public:
    static constexpr std::size_t kHeaderBytes = 4;
    static constexpr std::size_t kMaxFrameLen = 64u * 1024u * 1024u;  // 64 MiB

    /// Read one framed message from fd.  Returns nullopt on EOF or error.
    static std::optional<WireFrame> read_frame(int fd) noexcept {
        std::uint8_t header[kHeaderBytes];
        if (!detail::socket_read_exact(fd, header, kHeaderBytes)) return std::nullopt;
        const std::uint32_t len = detail::read_be32(header);
        if (len == 0 || len > kMaxFrameLen) return std::nullopt;
        WireFrame frame(len);
        if (!detail::socket_read_exact(fd, frame.data(), len)) return std::nullopt;
        return frame;
    }

    /// Write one framed message to fd.  Returns false on error.
    static bool write_frame(int fd, const WireFrame& frame) noexcept {
        std::uint8_t header[kHeaderBytes];
        detail::write_be32(header, static_cast<std::uint32_t>(frame.size()));
        if (!detail::socket_write_exact(fd, header, kHeaderBytes)) return false;
        return detail::socket_write_exact(fd, frame.data(), frame.size());
    }
};

// ── Server ─────────────────────────────────────────────────────────────────

class UnixSocketTransport : public IpcTransport {
public:
    UnixSocketTransport() = default;
    ~UnixSocketTransport() override { close(); }

    void listen(std::string_view path) override {
        close();
        m_address = std::string(path);
        m_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_fd < 0) {
            m_address.clear();
            throw std::system_error(errno, std::generic_category(), "ipc::socket");
        }
        ::unlink(m_address.c_str());
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (m_address.size() >= sizeof(addr.sun_path)) {
            close();
            throw std::system_error(EOVERFLOW, std::generic_category(), "ipc::path");
        }
        std::memcpy(addr.sun_path, m_address.c_str(), m_address.size() + 1);
        if (::bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            const int e = errno;
            close();
            throw std::system_error(e, std::generic_category(), "ipc::bind");
        }
        if (::listen(m_fd, 8) != 0) {
            const int e = errno;
            close();
            throw std::system_error(e, std::generic_category(), "ipc::listen");
        }
    }

    int serve(const FrameHandler& handler) noexcept override {
        // Concurrency model (see ipc_transport.hpp): connections are served
        // concurrently by per-client threads, while handler dispatch is
        // serialized under a single mutex. The RenderEngine is not assumed
        // thread-safe, so two clients can never dispatch at the same time —
        // but an idle client holding its socket open can no longer block
        // every other client from being served (the historical single-thread
        // accept→serve_client→accept loop's head-of-line stall).
        std::atomic<bool> shutdown_requested{false};
        struct ActiveClients {
            std::mutex mutex;
            std::vector<int> fds;
        };
        const auto active = std::make_shared<ActiveClients>();
        std::mutex dispatch_mutex;
        std::vector<std::thread> workers;

        // Wake every client worker blocked in recv() so they exit and can be
        // joined before serve() returns (a std::thread must never outlive the
        // locals this lambda captures).
        const auto stop_all_workers = [&]() noexcept {
            shutdown_requested.store(true, std::memory_order_release);
            std::lock_guard lock(active->mutex);
            for (const int fd : active->fds) ::shutdown(fd, SHUT_RDWR);
        };

        while (!shutdown_requested.load(std::memory_order_acquire)) {
            pollfd ready{};
            ready.fd = m_fd;
            ready.events = POLLIN;
            const int polled = ::poll(&ready, 1, 100);
            if (polled < 0) {
                if (errno == EINTR) continue;
                stop_all_workers();
                for (auto& worker : workers) {
                    if (worker.joinable()) worker.join();
                }
                return errno;
            }
            if (polled == 0) continue;
            const int client = ::accept(m_fd, nullptr, nullptr);
            if (client < 0) {
                if (errno == EINTR) continue;
                stop_all_workers();
                for (auto& worker : workers) {
                    if (worker.joinable()) worker.join();
                }
                return errno;
            }
            {
                // Register the client under the same lock a Shutdown-signalling
                // worker uses to wake peers, so no accepted fd can be missed:
                // if Shutdown already raced ahead of this accept, close the fd
                // here rather than spawning a worker nobody will ever wake.
                std::lock_guard lock(active->mutex);
                if (shutdown_requested.load(std::memory_order_acquire)) {
                    ::shutdown(client, SHUT_RDWR);
                    ::close(client);
                    break;
                }
                active->fds.push_back(client);
            }
            try {
                workers.emplace_back([this, client, &handler, &shutdown_requested,
                                      active, &dispatch_mutex]() {
                    bool shutdown = false;
                    serve_client(client, handler, dispatch_mutex, &shutdown);
                    ::close(client);
                    {
                        std::lock_guard lock(active->mutex);
                        active->fds.erase(
                            std::remove(active->fds.begin(), active->fds.end(), client),
                            active->fds.end());
                    }
                    if (shutdown) {
                        // Deliver Shutdown has completed: stop accepting and
                        // wake every other client so serve() can return 0.
                        std::lock_guard lock(active->mutex);
                        for (const int fd : active->fds) {
                            if (fd != client) ::shutdown(fd, SHUT_RDWR);
                        }
                        shutdown_requested.store(true, std::memory_order_release);
                    }
                });
            } catch (const std::exception&) {
                // Thread creation/resource exhaustion: never return from a
                // noexcept serve() with joinable workers alive.
                ::close(client);
                stop_all_workers();
                for (auto& worker : workers) {
                    if (worker.joinable()) worker.join();
                }
                return EAGAIN;
            }
        }
        for (auto& worker : workers) {
            if (worker.joinable()) worker.join();
        }
        return 0;
    }

    void close() noexcept override {
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
        if (!m_address.empty()) {
            ::unlink(m_address.c_str());
            m_address.clear();
        }
    }

    [[nodiscard]] const std::string& address() const noexcept override { return m_address; }

private:
    static void serve_client(int fd, const FrameHandler& handler,
                             std::mutex& dispatch_mutex, bool* shutdown) noexcept {
        while (true) {
            auto frame = LengthPrefixFraming::read_frame(fd);
            if (!frame) break;
            ServerReply result;
            try {
                std::lock_guard lock(dispatch_mutex);
                result = handler(*frame);
            } catch (...) {
                // A throwing handler must never take down the daemon; treat
                // it as a transport error for this client.
                break;
            }
            if (result.shutdown) {
                // Deliver the Shutdown acknowledgment, then stop serving so
                // the accept loop can exit. Even if the write fails, the
                // daemon must terminate (never keep looping after Shutdown).
                (void)LengthPrefixFraming::write_frame(fd, result.reply);
                *shutdown = true;
                return;
            }
            if (result.reply.empty()) break;
            if (!LengthPrefixFraming::write_frame(fd, result.reply)) break;
        }
    }

    int m_fd{-1};
    std::string m_address;
};

// ── Client ─────────────────────────────────────────────────────────────────

class UnixSocketClientTransport : public IpcClientTransport {
public:
    UnixSocketClientTransport() = default;
    ~UnixSocketClientTransport() override { close(); }

    void connect(std::string_view path) override {
        close();
        m_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_fd < 0) {
            throw std::system_error(errno, std::generic_category(), "ipc::socket");
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (path.size() >= sizeof(addr.sun_path)) {
            close();
            throw std::system_error(EOVERFLOW, std::generic_category(), "ipc::path");
        }
        std::memcpy(addr.sun_path, path.data(), path.size());
        addr.sun_path[path.size()] = '\0';
        if (::connect(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            const int e = errno;
            close();
            throw std::system_error(e, std::generic_category(), "ipc::connect");
        }
    }

    std::optional<WireFrame> request(const WireFrame& frame) noexcept override {
        if (!LengthPrefixFraming::write_frame(m_fd, frame)) return std::nullopt;
        return LengthPrefixFraming::read_frame(m_fd);
    }

    void close() noexcept override {
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
    }

private:
    int m_fd{-1};
};

} // namespace chronon3d::ipc
