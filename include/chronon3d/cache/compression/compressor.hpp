// =============================================================================
// include/chronon3d/cache/compression/compressor.hpp
//
// Compression facade for persistent cache artifacts.  Every on-disk
// representation (compiled metadata, font glyphs, tile cache, asset
// manifests, shader metadata — see AGENTS.md CacheTaxonomy families)
// routes through this single interface.
//
// The interface is intentionally narrow (compress / decompress) so it
// can be implemented with any codec.  The canonical implementation is
// ZstdCompressor (src/cache/compression/zstd_compressor.cpp).
//
// Guards:
//   • Do NOT use on GPU hot path (VkImage, live surfaces, per-frame rings).
//   • Do NOT use for FrameGraph objects in RAM.
//   • Do NOT call zstd directly — always go through CacheCompressor.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace chronon3d::cache {

/// Canonical compression interface for persistent cache artifacts.
///
/// Thread-safe: instances are stateless (the underlying codec context is
/// created per-call or uses a reusable pool).  Callers may share a single
/// instance across threads.
class CacheCompressor {
public:
    virtual ~CacheCompressor() = default;

    /// Compress `src` into a new buffer.  Returns compressed bytes or
    /// throws std::runtime_error on failure.
    [[nodiscard]] virtual std::vector<std::uint8_t> compress(
        std::span<const std::uint8_t> src,
        int level = 3) const = 0;

    /// Overload accepting a raw-pointer + size pair for callers that
    /// already have a pointer.
    [[nodiscard]] std::vector<std::uint8_t> compress(const void* data,
                                                      std::size_t size,
                                                      int level = 3) const {
        const auto* p = static_cast<const std::uint8_t*>(data);
        return compress(std::span<const std::uint8_t>(p, size), level);
    }

    /// Decompress `src` back to the original bytes.  Returns decompressed
    /// bytes or throws std::runtime_error on failure.
    [[nodiscard]] virtual std::vector<std::uint8_t> decompress(
        std::span<const std::uint8_t> src) const = 0;

    /// Overload accepting a raw-pointer + size pair.
    [[nodiscard]] std::vector<std::uint8_t> decompress(
        const void* data, std::size_t size) const {
        const auto* p = static_cast<const std::uint8_t*>(data);
        return decompress(std::span<const std::uint8_t>(p, size));
    }
};

/// Return a reference to the process-wide singleton compressor instance.
/// The instance is created lazily on first call; all cache layers share
/// the same instance.
[[nodiscard]] CacheCompressor& cache_compressor();

}  // namespace chronon3d::cache