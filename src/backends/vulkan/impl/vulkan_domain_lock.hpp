#pragma once

// Lock-domain vocabulary and RAII lock type, included inside Impl.
enum class LockDomain : std::uint8_t { Surface, Submission, Dual };

class DomainLock {
public:
    explicit DomainLock(Impl& impl, LockDomain domain);

    ~DomainLock();

    DomainLock(const DomainLock&) = delete;
    DomainLock& operator=(const DomainLock&) = delete;
    DomainLock(DomainLock&&) = delete;
    DomainLock& operator=(DomainLock&&) = delete;

private:
    Impl& impl_;
    LockDomain domain_;
    std::chrono::steady_clock::time_point wait_start_{};
    std::unique_lock<std::mutex> first_;
    // Owns nothing unless the domain is Dual (defer_lock).
    std::unique_lock<std::mutex> second_;
    std::chrono::steady_clock::time_point hold_start_{};
};
