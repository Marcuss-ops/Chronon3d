#pragma once

#include <chronon3d/media/video/packet_assembler.hpp>

extern "C" {
#include <libavformat/version.h>
#include <libavutil/hash.h>
}

#include <cstdint>
#include <string>

namespace chronon3d::media {

/// Private AVIO sink used by MuxSession. It owns the optional incremental
/// digest and records seek/backpatch activity without leaking that policy into
/// packet assembly or stream lifecycle code.
struct MuxAvioHashWriter {
    AVIOContext* sink{nullptr};
    AVHashContext* hash{nullptr};
    std::int64_t position{0};
    std::int64_t append_end{0};
    bool incremental_valid{false};
    bool checksum_requested{false};
    MuxHashMetrics metrics{};

    MuxAvioHashWriter() = default;
    MuxAvioHashWriter(const MuxAvioHashWriter&) = delete;
    MuxAvioHashWriter& operator=(const MuxAvioHashWriter&) = delete;
    ~MuxAvioHashWriter();

    [[nodiscard]] bool open(const std::string& path,
                             bool request_checksum) noexcept;

    // FFmpeg 7+ marks the write callback buffer const; older supported
    // libavformat ABIs use a mutable pointer. The sink never mutates it.
#if LIBAVFORMAT_VERSION_MAJOR >= 61
    static int write_packet(void* opaque, const std::uint8_t* buffer,
                            int buffer_size) noexcept;
#else
    static int write_packet(void* opaque, std::uint8_t* buffer,
                            int buffer_size) noexcept;
#endif
    static std::int64_t seek(void* opaque, std::int64_t offset,
                             int whence) noexcept;

    [[nodiscard]] bool close_and_checksum(const std::string& path,
                                          std::string& checksum,
                                          bool& used_reread) noexcept;
    [[nodiscard]] MuxHashMetrics hash_metrics() const noexcept;
};

} // namespace chronon3d::media
