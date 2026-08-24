// =============================================================================
// src/cache/compression/zstd_compressor.cpp
//
// Canonical zstd implementation of CacheCompressor.
//
// Uses the simple one-shot ZSTD_compress / ZSTD_decompress API (no streaming,
// no dictionaries, no reusable contexts — see design notes below).
//
// Design notes:
//   • One-shot API: cache artifacts are small/medium payloads (tens of KB
//     to a few MB) compressed atomically.  Streaming is overkill here.
//   • No reusable CCtx/DCtx: the simple API keeps the codec stateless,
//     trivially thread-safe, and the per-call overhead of re-creating
//     an internal context is negligible at our payload sizes.
//   • Default compression level 3: good speed/ratio balance for metadata.
//     Higher levels (6+) can be requested for offline / archive bundles
//     where compression time is less critical.
//   • ZSTD_getFrameContentSize() is used on decompress to size the output
//     buffer exactly — no heuristic guesswork.
// =============================================================================

#include <chronon3d/cache/compression/compressor.hpp>

#include <zstd.h>
#include <zstd_errors.h>

#include <mutex>
#include <stdexcept>
#include <string>

namespace chronon3d::cache {
namespace {

// ── Error helpers ───────────────────────────────────────────────────────

[[noreturn]] void throw_zstd_error(const char* op, std::size_t code) {
    const char* name = ZSTD_getErrorName(code);
    throw std::runtime_error(
        std::string(op) + " failed: " + name +
        " (code " + std::to_string(code) + ")");
}

void check_zstd(const char* op, std::size_t code) {
    if (ZSTD_isError(code)) {
        throw_zstd_error(op, code);
    }
}

// ── ZstdCompressor ─────────────────────────────────────────────────────

class ZstdCompressor final : public CacheCompressor {
public:
    [[nodiscard]] std::vector<std::uint8_t> compress(
        std::span<const std::uint8_t> src, int level) const override {
        const auto src_size = src.size();
        const auto bound    = ZSTD_compressBound(src_size);
        std::vector<std::uint8_t> dst(bound);

        const std::size_t actual = ZSTD_compress(
            dst.data(), dst.size(), src.data(), src_size, level);
        check_zstd("ZSTD_compress", actual);

        dst.resize(actual);
        dst.shrink_to_fit();
        return dst;
    }

    [[nodiscard]] std::vector<std::uint8_t> decompress(
        std::span<const std::uint8_t> src) const override {
        const auto content_size = ZSTD_getFrameContentSize(
            src.data(), src.size());

        if (content_size == ZSTD_CONTENTSIZE_ERROR) {
            throw std::runtime_error(
                "ZSTD_decompress: invalid or corrupt frame header");
        }
        if (content_size == ZSTD_CONTENTSIZE_UNKNOWN) {
            // The frame doesn't embed the decompressed size — unusual for
            // one-shot compression, but handle gracefully with a heuristic.
            // Start with 2× the compressed size, which is a safe upper
            // bound for most zstd payloads.
            return decompress_unknown_size(src);
        }

        std::vector<std::uint8_t> dst(static_cast<std::size_t>(content_size));

        const std::size_t actual = ZSTD_decompress(
            dst.data(), dst.size(), src.data(), src.size());
        check_zstd("ZSTD_decompress", actual);

        dst.resize(actual);
        dst.shrink_to_fit();
        return dst;
    }

private:
    // Fallback path for frames without embedded content size (rare).
    [[nodiscard]] std::vector<std::uint8_t> decompress_unknown_size(
        std::span<const std::uint8_t> src) const {
        // Start at 2× compressed size, grow until success.
        std::size_t cap = src.size() * 2;
        if (cap < 256) cap = 256;

        for (int attempt = 0; attempt < 4; ++attempt) {
            std::vector<std::uint8_t> dst(cap);
            const std::size_t rc = ZSTD_decompress(
                dst.data(), dst.size(), src.data(), src.size());
            if (!ZSTD_isError(rc)) {
                dst.resize(rc);
                dst.shrink_to_fit();
                return dst;
            }
            // If the error is "destination too small", grow.
            if (ZSTD_getErrorCode(rc) ==
                ZSTD_error_dstSize_tooSmall) {
                cap *= 4;
                continue;
            }
            check_zstd("ZSTD_decompress", rc);  // throws
        }
        throw_zstd_error("ZSTD_decompress",
                          ZSTD_error_dstSize_tooSmall);
    }
};

// ── Singleton access (lazy, thread-safe) ──────────────────────────────

std::once_flag s_init_flag;
CacheCompressor* s_instance = nullptr;

}  // namespace

CacheCompressor& cache_compressor() {
    std::call_once(s_init_flag, [] {
        static ZstdCompressor instance;
        s_instance = &instance;
    });
    return *s_instance;
}

}  // namespace chronon3d::cache