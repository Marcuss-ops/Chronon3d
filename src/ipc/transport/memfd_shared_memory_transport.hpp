#pragma once

#include "../ipc_transport.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::ipc {

/// Linux shared-memory IPC transport backed by an anonymous memfd.
///
/// A Unix-domain control socket is used only for the connection handshake and
/// SCM_RIGHTS descriptor transfer. Request/reply bytes live in the mapped
/// memfd; process-shared unnamed semaphores inside that mapping provide the
/// bounded one-client synchronization. No globally named shared-memory or
/// semaphore objects are created.
class SharedMemoryTransport final : public IpcTransport, public IpcClientTransport {
public:
    static constexpr std::size_t kMaxFrameLen = 4u * 1024u * 1024u;

    SharedMemoryTransport();
    ~SharedMemoryTransport() override;

    SharedMemoryTransport(const SharedMemoryTransport&) = delete;
    SharedMemoryTransport& operator=(const SharedMemoryTransport&) = delete;
    SharedMemoryTransport(SharedMemoryTransport&&) = delete;
    SharedMemoryTransport& operator=(SharedMemoryTransport&&) = delete;

    void listen(std::string_view address) override;
    [[nodiscard]] int serve(const FrameHandler& handler) noexcept override;
    void close() noexcept override;
    [[nodiscard]] const std::string& address() const noexcept override;

    void connect(std::string_view address) override;
    [[nodiscard]] std::optional<WireFrame> request(const WireFrame& frame) noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace chronon3d::ipc
