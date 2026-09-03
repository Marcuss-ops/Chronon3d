#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace chronon3d::runtime {

enum class OutputSinkMode : std::uint8_t {
    AppendOnly,
    Seekable,
};

struct OutputFinalizeResult {
    std::uint64_t bytes{0};
    std::uint64_t hash{0};
    OutputSinkMode mode{OutputSinkMode::AppendOnly};
};

/// Canonical byte-output boundary. Hashing is deliberately finalized here so
/// render/export callers never maintain a second hashing stream.
///
/// Append-only sinks hash each successfully-written byte exactly once as it is
/// appended. Seekable sinks may overwrite prior bytes, so finalize() asks the
/// sink implementation to hash its final canonical content after all seeks.
class OutputSink {
public:
    virtual ~OutputSink() = default;

    OutputSink(const OutputSink&) = delete;
    OutputSink& operator=(const OutputSink&) = delete;

    [[nodiscard]] OutputSinkMode mode() const noexcept { return mode_; }
    [[nodiscard]] std::uint64_t bytes_written() const noexcept { return bytes_written_; }

    bool write(std::span<const std::byte> bytes) {
        if (finalized_) {
            throw std::logic_error("OutputSink::write after finalize");
        }
        if (!write_impl(bytes)) return false;
        bytes_written_ += static_cast<std::uint64_t>(bytes.size());
        if (mode_ == OutputSinkMode::AppendOnly) {
            append_hash_ = hash_bytes(append_hash_, bytes);
        }
        return true;
    }

    bool seek(std::uint64_t absolute_offset) {
        if (finalized_) {
            throw std::logic_error("OutputSink::seek after finalize");
        }
        if (mode_ != OutputSinkMode::Seekable) return false;
        return seek_impl(absolute_offset);
    }

    [[nodiscard]] OutputFinalizeResult finalize() {
        if (finalized_) return finalized_result_;
        if (!finalize_impl()) {
            throw std::runtime_error("OutputSink finalization failed");
        }

        const std::uint64_t final_bytes = final_size_impl();
        const std::uint64_t final_hash =
            mode_ == OutputSinkMode::AppendOnly
                ? append_hash_
                : hash_final_content_impl();
        finalized_result_ = OutputFinalizeResult{final_bytes, final_hash, mode_};
        finalized_ = true;
        return finalized_result_;
    }

protected:
    explicit OutputSink(OutputSinkMode mode) noexcept : mode_(mode) {}

    virtual bool write_impl(std::span<const std::byte> bytes) = 0;
    virtual bool seek_impl(std::uint64_t) { return false; }
    virtual bool finalize_impl() { return true; }
    virtual std::uint64_t final_size_impl() const noexcept { return bytes_written_; }
    virtual std::uint64_t hash_final_content_impl() const {
        throw std::logic_error(
            "seekable OutputSink must provide final-content hashing");
    }

    [[nodiscard]] static constexpr std::uint64_t hash_offset_basis() noexcept {
        return 14695981039346656037ull;
    }

    [[nodiscard]] static std::uint64_t hash_bytes(
        std::uint64_t seed,
        std::span<const std::byte> bytes) noexcept {
        constexpr std::uint64_t kPrime = 1099511628211ull;
        auto hash = seed;
        for (const auto byte : bytes) {
            hash ^= static_cast<std::uint8_t>(byte);
            hash *= kPrime;
        }
        return hash;
    }

private:
    OutputSinkMode mode_{OutputSinkMode::AppendOnly};
    std::uint64_t bytes_written_{0};
    std::uint64_t append_hash_{hash_offset_basis()};
    bool finalized_{false};
    OutputFinalizeResult finalized_result_{};
};

} // namespace chronon3d::runtime
