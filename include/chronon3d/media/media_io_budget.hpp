#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <utility>

namespace chronon3d::media {

struct MediaIoBudgetConfig {
    std::uint64_t max_prefetch_bytes{512ULL * 1024ULL * 1024ULL};
    std::uint32_t max_concurrent_reads{8};
};

enum class MediaIoPriority : std::uint8_t {
    Required = 0,
    Prefetch,
};

struct MediaIoBudgetStats {
    std::uint64_t reserved_prefetch_bytes{0};
    std::uint64_t peak_prefetch_bytes{0};
    std::uint32_t active_reads{0};
    std::uint32_t peak_active_reads{0};
    std::uint32_t required_waiters{0};
};

/// Process-wide media I/O pressure authority. Local decoder queue limits stay
/// useful for latency, but speculative buffering and read concurrency are
/// ultimately bounded here in bytes across every decoder session.
class MediaIoBudget {
public:
    class Reservation {
    public:
        Reservation() = default;
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;
        Reservation(Reservation&& other) noexcept { move_from(std::move(other)); }
        Reservation& operator=(Reservation&& other) noexcept {
            if (this != &other) {
                reset();
                move_from(std::move(other));
            }
            return *this;
        }
        ~Reservation() { reset(); }

        [[nodiscard]] explicit operator bool() const noexcept { return owner_ != nullptr; }
        [[nodiscard]] std::uint64_t bytes() const noexcept { return bytes_; }
        void reset() noexcept {
            if (owner_) owner_->release_bytes(bytes_);
            owner_ = nullptr;
            bytes_ = 0;
        }

    private:
        friend class MediaIoBudget;
        Reservation(MediaIoBudget* owner, std::uint64_t bytes) noexcept
            : owner_(owner), bytes_(bytes) {}
        void move_from(Reservation&& other) noexcept {
            owner_ = std::exchange(other.owner_, nullptr);
            bytes_ = std::exchange(other.bytes_, 0);
        }
        MediaIoBudget* owner_{nullptr};
        std::uint64_t bytes_{0};
    };

    class ReadPermit {
    public:
        ReadPermit() = default;
        ReadPermit(const ReadPermit&) = delete;
        ReadPermit& operator=(const ReadPermit&) = delete;
        ReadPermit(ReadPermit&& other) noexcept { move_from(std::move(other)); }
        ReadPermit& operator=(ReadPermit&& other) noexcept {
            if (this != &other) {
                reset();
                move_from(std::move(other));
            }
            return *this;
        }
        ~ReadPermit() { reset(); }

        [[nodiscard]] explicit operator bool() const noexcept { return owner_ != nullptr; }
        void reset() noexcept {
            if (owner_) owner_->release_read();
            owner_ = nullptr;
        }

    private:
        friend class MediaIoBudget;
        explicit ReadPermit(MediaIoBudget* owner) noexcept : owner_(owner) {}
        void move_from(ReadPermit&& other) noexcept {
            owner_ = std::exchange(other.owner_, nullptr);
        }
        MediaIoBudget* owner_{nullptr};
    };

    explicit MediaIoBudget(MediaIoBudgetConfig config = {}) noexcept
        : config_(sanitize(config)) {}

    void set_config(MediaIoBudgetConfig config) noexcept {
        std::lock_guard lock(mutex_);
        config_ = sanitize(config);
        cv_.notify_all();
    }

    [[nodiscard]] MediaIoBudgetConfig config() const noexcept {
        std::lock_guard lock(mutex_);
        return config_;
    }

    /// Non-blocking reservation for speculative prefetch. Required readers are
    /// never delayed by new speculative reservations.
    [[nodiscard]] Reservation try_reserve_prefetch(std::uint64_t bytes) noexcept {
        std::lock_guard lock(mutex_);
        if (required_waiters_ != 0 || !can_reserve_locked(bytes)) return {};
        reserved_prefetch_bytes_ += bytes;
        peak_prefetch_bytes_ = std::max(peak_prefetch_bytes_, reserved_prefetch_bytes_);
        return Reservation(this, bytes);
    }

    /// Required work is not speculative and therefore does not consume the
    /// prefetch byte pool. It does participate in read concurrency below.
    [[nodiscard]] ReadPermit acquire_required_read() {
        std::unique_lock lock(mutex_);
        ++required_waiters_;
        cv_.wait(lock, [this] { return active_reads_ < config_.max_concurrent_reads; });
        --required_waiters_;
        ++active_reads_;
        peak_active_reads_ = std::max(peak_active_reads_, active_reads_);
        cv_.notify_all();
        return ReadPermit(this);
    }

    [[nodiscard]] ReadPermit try_acquire_prefetch_read() noexcept {
        std::lock_guard lock(mutex_);
        if (required_waiters_ != 0 || active_reads_ >= config_.max_concurrent_reads) return {};
        ++active_reads_;
        peak_active_reads_ = std::max(peak_active_reads_, active_reads_);
        return ReadPermit(this);
    }

    void wait_for_change(std::chrono::milliseconds interval) {
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, interval);
    }

    [[nodiscard]] MediaIoBudgetStats stats() const noexcept {
        std::lock_guard lock(mutex_);
        return MediaIoBudgetStats{
            reserved_prefetch_bytes_, peak_prefetch_bytes_, active_reads_,
            peak_active_reads_, required_waiters_};
    }

private:
    static MediaIoBudgetConfig sanitize(MediaIoBudgetConfig config) noexcept {
        if (config.max_prefetch_bytes == 0) config.max_prefetch_bytes = 1;
        if (config.max_concurrent_reads == 0) config.max_concurrent_reads = 1;
        return config;
    }

    [[nodiscard]] bool can_reserve_locked(std::uint64_t bytes) const noexcept {
        if (bytes > config_.max_prefetch_bytes) return false;
        return bytes <= config_.max_prefetch_bytes - reserved_prefetch_bytes_;
    }

    void release_bytes(std::uint64_t bytes) noexcept {
        std::lock_guard lock(mutex_);
        reserved_prefetch_bytes_ = bytes <= reserved_prefetch_bytes_
            ? reserved_prefetch_bytes_ - bytes : 0;
        cv_.notify_all();
    }

    void release_read() noexcept {
        std::lock_guard lock(mutex_);
        if (active_reads_ != 0) --active_reads_;
        cv_.notify_all();
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    MediaIoBudgetConfig config_{};
    std::uint64_t reserved_prefetch_bytes_{0};
    std::uint64_t peak_prefetch_bytes_{0};
    std::uint32_t active_reads_{0};
    std::uint32_t peak_active_reads_{0};
    std::uint32_t required_waiters_{0};
};

/// Canonical process authority used until a higher-level runtime explicitly
/// owns/injects a MediaIoBudget instance.
inline MediaIoBudget& global_media_io_budget() {
    static MediaIoBudget budget{};
    return budget;
}

} // namespace chronon3d::media
