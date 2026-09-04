#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "memfd_shared_memory_transport.hpp"

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <system_error>

#if defined(__linux__)
#include <linux/memfd.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace chronon3d::ipc {

#if defined(__linux__)
namespace {

struct SharedRegion {
    sem_t request_ready{};
    sem_t response_ready{};
    std::uint32_t request_len{0};
    std::uint32_t response_len{0};
    std::uint32_t client_closed{0};
    std::uint32_t server_closed{0};
    alignas(64) std::uint8_t request[SharedMemoryTransport::kMaxFrameLen]{};
    alignas(64) std::uint8_t response[SharedMemoryTransport::kMaxFrameLen]{};
};

[[noreturn]] void throw_errno(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

int wait_sem(sem_t* sem) noexcept {
    while (::sem_wait(sem) != 0) {
        if (errno != EINTR) return errno;
    }
    return 0;
}

struct ControlAddress {
    sockaddr_un value{};
    socklen_t length{0};
};

ControlAddress make_control_address(std::string_view logical_address) {
    if (logical_address.empty()) {
        throw std::system_error(EINVAL, std::generic_category(), "ipc::memfd empty address");
    }

    ControlAddress out;
    out.value.sun_family = AF_UNIX;

    std::string name = "chronon3d.memfd." +
        std::to_string(static_cast<unsigned long>(::getuid())) + ".";
    name.reserve(name.size() + logical_address.size());
    for (const char c : logical_address) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        name.push_back(safe ? c : '_');
    }

    constexpr std::size_t kAbstractPrefixBytes = 1;
    const std::size_t max_name = sizeof(out.value.sun_path) - kAbstractPrefixBytes;
    if (name.size() > max_name) name.resize(max_name);

    out.value.sun_path[0] = '\0';
    std::memcpy(out.value.sun_path + 1, name.data(), name.size());
    out.length = static_cast<socklen_t>(
        offsetof(sockaddr_un, sun_path) + kAbstractPrefixBytes + name.size());
    return out;
}

bool send_fd(int socket_fd, int fd) noexcept {
    char marker = 'M';
    iovec io{};
    io.iov_base = &marker;
    io.iov_len = 1;

    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))]{};
    msghdr message{};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);

    cmsghdr* cmsg = CMSG_FIRSTHDR(&message);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));

    for (;;) {
        if (::sendmsg(socket_fd, &message, MSG_NOSIGNAL) >= 0) return true;
        if (errno != EINTR) return false;
    }
}

int receive_fd(int socket_fd) noexcept {
    char marker = 0;
    iovec io{};
    io.iov_base = &marker;
    io.iov_len = 1;

    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))]{};
    msghdr message{};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);

    for (;;) {
        const ssize_t received = ::recvmsg(socket_fd, &message, 0);
        if (received > 0) break;
        if (received == 0) return -1;
        if (errno != EINTR) return -1;
    }

    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&message); cmsg; cmsg = CMSG_NXTHDR(&message, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len >= CMSG_LEN(sizeof(int))) {
            int fd = -1;
            std::memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));
            return fd;
        }
    }
    return -1;
}

SharedRegion* map_region(int fd) {
    void* mapped = ::mmap(nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) throw_errno("ipc::memfd mmap");
    return static_cast<SharedRegion*>(mapped);
}

void unmap_region(SharedRegion*& region) noexcept {
    if (!region) return;
    ::munmap(region, sizeof(SharedRegion));
    region = nullptr;
}

} // namespace
#endif

struct SharedMemoryTransport::Impl {
    std::mutex state_mutex;
    std::mutex request_mutex;
    std::atomic<bool> closed{true};
    std::atomic<bool> serving{false};
    std::string address;

#if defined(__linux__)
    int control_fd{-1};
    int peer_fd{-1};
    int memfd{-1};
    SharedRegion* region{nullptr};
    bool owner{false};
#endif
};

SharedMemoryTransport::SharedMemoryTransport()
    : m_impl(std::make_unique<Impl>()) {}

SharedMemoryTransport::~SharedMemoryTransport() {
    close();
}

void SharedMemoryTransport::listen(std::string_view logical_address) {
    close();
#if defined(__linux__)
    const ControlAddress address = make_control_address(logical_address);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) throw_errno("ipc::memfd socket");
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address.value), address.length) != 0) {
        const int error = errno;
        ::close(fd);
        throw std::system_error(error, std::generic_category(), "ipc::memfd bind");
    }
    if (::listen(fd, 1) != 0) {
        const int error = errno;
        ::close(fd);
        throw std::system_error(error, std::generic_category(), "ipc::memfd listen");
    }

    std::lock_guard lock(m_impl->state_mutex);
    m_impl->control_fd = fd;
    m_impl->owner = true;
    m_impl->address.assign(logical_address);
    m_impl->closed.store(false, std::memory_order_release);
#else
    (void)logical_address;
    throw std::system_error(ENOTSUP, std::generic_category(),
                            "SharedMemoryTransport requires Linux memfd support");
#endif
}

int SharedMemoryTransport::serve(const FrameHandler& handler) noexcept {
#if defined(__linux__)
    int listen_fd = -1;
    {
        std::lock_guard lock(m_impl->state_mutex);
        if (!m_impl->owner || m_impl->control_fd < 0) return EINVAL;
        listen_fd = m_impl->control_fd;
        m_impl->serving.store(true, std::memory_order_release);
    }

    int peer = -1;
    for (;;) {
        peer = ::accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (peer >= 0) break;
        if (errno == EINTR) continue;
        m_impl->serving.store(false, std::memory_order_release);
        return m_impl->closed.load(std::memory_order_acquire) ? 0 : errno;
    }

    const int memory_fd = ::memfd_create("chronon3d-ipc", MFD_CLOEXEC);
    if (memory_fd < 0) {
        const int error = errno;
        ::close(peer);
        m_impl->serving.store(false, std::memory_order_release);
        return error;
    }
    if (::ftruncate(memory_fd, static_cast<off_t>(sizeof(SharedRegion))) != 0) {
        const int error = errno;
        ::close(memory_fd);
        ::close(peer);
        m_impl->serving.store(false, std::memory_order_release);
        return error;
    }

    void* mapped = ::mmap(nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE,
                          MAP_SHARED, memory_fd, 0);
    if (mapped == MAP_FAILED) {
        const int error = errno;
        ::close(memory_fd);
        ::close(peer);
        m_impl->serving.store(false, std::memory_order_release);
        return error;
    }
    auto* region = static_cast<SharedRegion*>(mapped);
    std::memset(region, 0, sizeof(SharedRegion));
    if (::sem_init(&region->request_ready, 1, 0) != 0) {
        const int error = errno;
        ::munmap(region, sizeof(SharedRegion));
        ::close(memory_fd);
        ::close(peer);
        m_impl->serving.store(false, std::memory_order_release);
        return error;
    }
    if (::sem_init(&region->response_ready, 1, 0) != 0) {
        const int error = errno;
        ::sem_destroy(&region->request_ready);
        ::munmap(region, sizeof(SharedRegion));
        ::close(memory_fd);
        ::close(peer);
        m_impl->serving.store(false, std::memory_order_release);
        return error;
    }

    {
        std::lock_guard lock(m_impl->state_mutex);
        m_impl->peer_fd = peer;
        m_impl->memfd = memory_fd;
        m_impl->region = region;
    }

    if (!send_fd(peer, memory_fd)) {
        const int error = errno ? errno : EIO;
        {
            std::lock_guard lock(m_impl->state_mutex);
            m_impl->peer_fd = -1;
            m_impl->memfd = -1;
            m_impl->region = nullptr;
        }
        ::sem_destroy(&region->request_ready);
        ::sem_destroy(&region->response_ready);
        ::munmap(region, sizeof(SharedRegion));
        ::close(memory_fd);
        ::close(peer);
        m_impl->serving.store(false, std::memory_order_release);
        return error;
    }

    int result = 0;
    bool client_closed = false;
    while (!m_impl->closed.load(std::memory_order_acquire)) {
        const int wait_result = wait_sem(&region->request_ready);
        if (wait_result != 0) {
            result = wait_result;
            break;
        }
        if (m_impl->closed.load(std::memory_order_acquire) || region->server_closed != 0) break;
        if (region->client_closed != 0) {
            client_closed = true;
            break;
        }

        const std::uint32_t length = region->request_len;
        if (length == 0 || length > kMaxFrameLen) {
            result = EMSGSIZE;
            break;
        }

        WireFrame request_frame(region->request, region->request + length);
        WireFrame reply = handler(request_frame);
        if (reply.empty()) {
            region->response_len = 0;
            region->server_closed = 1;
            ::sem_post(&region->response_ready);
            break;
        }
        if (reply.size() > kMaxFrameLen) {
            result = EMSGSIZE;
            region->response_len = 0;
            region->server_closed = 1;
            ::sem_post(&region->response_ready);
            break;
        }

        std::memcpy(region->response, reply.data(), reply.size());
        region->response_len = static_cast<std::uint32_t>(reply.size());
        if (::sem_post(&region->response_ready) != 0) {
            result = errno;
            break;
        }
    }

    {
        std::lock_guard lock(m_impl->state_mutex);
        if (m_impl->region == region) m_impl->region = nullptr;
        if (m_impl->memfd == memory_fd) m_impl->memfd = -1;
        if (m_impl->peer_fd == peer) m_impl->peer_fd = -1;
    }
    if (client_closed) {
        ::sem_destroy(&region->request_ready);
        ::sem_destroy(&region->response_ready);
    }
    ::munmap(region, sizeof(SharedRegion));
    ::close(memory_fd);
    ::close(peer);
    m_impl->serving.store(false, std::memory_order_release);
    return result;
#else
    (void)handler;
    return ENOTSUP;
#endif
}

void SharedMemoryTransport::connect(std::string_view logical_address) {
    close();
#if defined(__linux__)
    const ControlAddress address = make_control_address(logical_address);
    const int socket_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (socket_fd < 0) throw_errno("ipc::memfd client socket");
    if (::connect(socket_fd, reinterpret_cast<const sockaddr*>(&address.value), address.length) != 0) {
        const int error = errno;
        ::close(socket_fd);
        throw std::system_error(error, std::generic_category(), "ipc::memfd connect");
    }

    const int memory_fd = receive_fd(socket_fd);
    if (memory_fd < 0) {
        const int error = errno ? errno : EPROTO;
        ::close(socket_fd);
        throw std::system_error(error, std::generic_category(), "ipc::memfd receive SCM_RIGHTS");
    }

    SharedRegion* region = nullptr;
    try {
        region = map_region(memory_fd);
    } catch (...) {
        ::close(memory_fd);
        ::close(socket_fd);
        throw;
    }

    std::lock_guard lock(m_impl->state_mutex);
    m_impl->control_fd = socket_fd;
    m_impl->memfd = memory_fd;
    m_impl->region = region;
    m_impl->owner = false;
    m_impl->address.assign(logical_address);
    m_impl->closed.store(false, std::memory_order_release);
#else
    (void)logical_address;
    throw std::system_error(ENOTSUP, std::generic_category(),
                            "SharedMemoryTransport requires Linux memfd support");
#endif
}

std::optional<WireFrame> SharedMemoryTransport::request(const WireFrame& frame) noexcept {
#if defined(__linux__)
    if (frame.empty() || frame.size() > kMaxFrameLen) return std::nullopt;
    std::lock_guard request_lock(m_impl->request_mutex);

    SharedRegion* region = nullptr;
    {
        std::lock_guard lock(m_impl->state_mutex);
        if (m_impl->owner || !m_impl->region || m_impl->closed.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        region = m_impl->region;
    }
    if (region->server_closed != 0 || region->client_closed != 0) return std::nullopt;

    std::memcpy(region->request, frame.data(), frame.size());
    region->request_len = static_cast<std::uint32_t>(frame.size());
    region->response_len = 0;
    if (::sem_post(&region->request_ready) != 0) return std::nullopt;
    if (wait_sem(&region->response_ready) != 0) return std::nullopt;

    const std::uint32_t length = region->response_len;
    if (length == 0 || length > kMaxFrameLen) return std::nullopt;
    return WireFrame(region->response, region->response + length);
#else
    (void)frame;
    return std::nullopt;
#endif
}

void SharedMemoryTransport::close() noexcept {
    if (!m_impl) return;
    m_impl->closed.store(true, std::memory_order_release);
#if defined(__linux__)
    std::lock_guard request_lock(m_impl->request_mutex);
    std::lock_guard state_lock(m_impl->state_mutex);

    if (m_impl->region) {
        if (m_impl->owner) {
            m_impl->region->server_closed = 1;
            ::sem_post(&m_impl->region->request_ready);
            ::sem_post(&m_impl->region->response_ready);
        } else {
            m_impl->region->client_closed = 1;
            ::sem_post(&m_impl->region->request_ready);
        }
    }

    if (m_impl->control_fd >= 0) {
        ::shutdown(m_impl->control_fd, SHUT_RDWR);
        ::close(m_impl->control_fd);
        m_impl->control_fd = -1;
    }

    if (!m_impl->serving.load(std::memory_order_acquire)) {
        if (m_impl->peer_fd >= 0) {
            ::close(m_impl->peer_fd);
            m_impl->peer_fd = -1;
        }
        unmap_region(m_impl->region);
        if (m_impl->memfd >= 0) {
            ::close(m_impl->memfd);
            m_impl->memfd = -1;
        }
    }
    m_impl->owner = false;
#endif
    m_impl->address.clear();
}

const std::string& SharedMemoryTransport::address() const noexcept {
    return m_impl->address;
}

} // namespace chronon3d::ipc
