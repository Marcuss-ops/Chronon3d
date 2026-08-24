#pragma once

#include "ipc_transport.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <semaphore.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace chronon3d::ipc {

/// POSIX shared-memory transport for one daemon/client pair.
///
/// The transport carries opaque WireFrames. FlatBuffers validation remains in
/// IpcCodec. The segment is deliberately bounded so malformed peers cannot
/// request unbounded allocation.
class SharedMemoryTransport final : public IpcTransport, public IpcClientTransport {
public:
    static constexpr std::size_t kMaxFrameLen = 4u * 1024u * 1024u;

    SharedMemoryTransport() = default;
    ~SharedMemoryTransport() override { close(); }
    SharedMemoryTransport(const SharedMemoryTransport&) = delete;
    SharedMemoryTransport& operator=(const SharedMemoryTransport&) = delete;

    void listen(std::string_view address) override {
        close();
        m_names = Names{normalize(address)};
        m_owner = true;
        m_fd = ::shm_open(m_names.shm.c_str(), O_CREAT | O_RDWR, 0600);
        if (m_fd < 0) fail_errno("shm_open");
        if (::ftruncate(m_fd, static_cast<off_t>(sizeof(Region))) != 0) fail_errno("ftruncate");
        map_region();
        m_request = open_sem(m_names.request, 0);
        m_response = open_sem(m_names.response, 0);
        m_address = std::string(address);
    }

    int serve(const FrameHandler& handler) noexcept override {
        if (!m_owner || !m_region || !m_request || !m_response) return EINVAL;
        for (;;) {
            if (wait_sem(m_request) != 0) return errno;
            if (m_closed.load(std::memory_order_acquire)) return 0;
            const std::uint32_t length = m_region->request_len;
            if (length == 0 || length > kMaxFrameLen) return EMSGSIZE;
            WireFrame request(m_region->request, m_region->request + length);
            WireFrame reply = handler(request);
            if (reply.empty()) return 0;
            if (reply.size() > kMaxFrameLen) return EMSGSIZE;
            std::memcpy(m_region->response, reply.data(), reply.size());
            m_region->response_len = static_cast<std::uint32_t>(reply.size());
            ::sem_post(m_response);
        }
    }

    void close() noexcept override {
        m_closed.store(true, std::memory_order_release);
        if (m_request) ::sem_post(m_request);
        if (m_request) { ::sem_close(m_request); m_request = nullptr; }
        if (m_response) { ::sem_close(m_response); m_response = nullptr; }
        unmap_region();
        if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
        if (m_owner && !m_names.shm.empty()) {
            ::shm_unlink(m_names.shm.c_str());
            ::sem_unlink(m_names.request.c_str());
            ::sem_unlink(m_names.response.c_str());
        }
        m_owner = false;
        m_address.clear();
        m_names = {};
    }

    [[nodiscard]] const std::string& address() const noexcept override { return m_address; }

    void connect(std::string_view address) override {
        close();
        m_names = Names{normalize(address)};
        m_fd = ::shm_open(m_names.shm.c_str(), O_RDWR, 0600);
        if (m_fd < 0) fail_errno("shm_open client");
        map_region();
        m_request = open_sem(m_names.request, 0);
        m_response = open_sem(m_names.response, 0);
        m_address = std::string(address);
    }

    [[nodiscard]] std::optional<WireFrame> request(const WireFrame& frame) noexcept override {
        if (m_owner || !m_region || !m_request || !m_response || frame.empty() || frame.size() > kMaxFrameLen) return std::nullopt;
        std::memcpy(m_region->request, frame.data(), frame.size());
        m_region->request_len = static_cast<std::uint32_t>(frame.size());
        if (::sem_post(m_request) != 0 || wait_sem(m_response) != 0) return std::nullopt;
        const std::uint32_t length = m_region->response_len;
        if (length == 0 || length > kMaxFrameLen) return std::nullopt;
        return WireFrame(m_region->response, m_region->response + length);
    }

private:
    struct Region {
        std::uint32_t request_len{0};
        std::uint32_t response_len{0};
        std::uint8_t request[kMaxFrameLen]{};
        std::uint8_t response[kMaxFrameLen]{};
    };
    struct Names {
        std::string shm;
        std::string request;
        std::string response;
        Names() = default;
        explicit Names(std::string base)
            : shm(base), request(base + "_req"), response(base + "_resp") {}
    };

    static std::string normalize(std::string_view address) {
        std::string base = "/chronon3d_";
        for (char c : address) base += (c == '/' || c == '\\') ? '_' : c;
        if (base.size() > 200) base.resize(200);
        return base;
    }
    static sem_t* open_sem(const std::string& name, unsigned value) {
        sem_t* sem = ::sem_open(name.c_str(), O_CREAT, 0600, value);
        if (sem == SEM_FAILED) throw std::system_error(errno, std::generic_category(), "sem_open");
        return sem;
    }
    static int wait_sem(sem_t* sem) noexcept {
        while (::sem_wait(sem) != 0) if (errno != EINTR) return errno;
        return 0;
    }
    [[noreturn]] static void fail_errno(const char* what) { throw std::system_error(errno, std::generic_category(), what); }
    void map_region() {
        void* mapped = ::mmap(nullptr, sizeof(Region), PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
        if (mapped == MAP_FAILED) fail_errno("mmap");
        m_region = static_cast<Region*>(mapped);
        m_closed.store(false, std::memory_order_release);
    }
    void unmap_region() noexcept {
        if (m_region) { ::munmap(m_region, sizeof(Region)); m_region = nullptr; }
    }

    int m_fd{-1};
    Region* m_region{nullptr};
    sem_t* m_request{nullptr};
    sem_t* m_response{nullptr};
    Names m_names{};
    std::string m_address;
    bool m_owner{false};
    std::atomic<bool> m_closed{false};
};

} // namespace chronon3d::ipc
