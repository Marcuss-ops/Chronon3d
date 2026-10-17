#include "packet_assembler_hash_writer.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/hash.h>
#include <libavutil/mem.h>
}

namespace chronon3d::media {
namespace {

constexpr std::size_t kChecksumReadBufferSize = 64 * 1024;
constexpr std::size_t kSha256HexChars = 64;

bool finish_sha256_hex(AVHashContext* hash, std::string& checksum) {
    if (!hash || av_hash_get_size(hash) != 32) return false;
    std::array<char, kSha256HexChars + 1> digest{};
    av_hash_final_hex(hash, reinterpret_cast<uint8_t*>(digest.data()),
                      static_cast<int>(digest.size()));
    checksum.assign(digest.data(), kSha256HexChars);
    return true;
}

// The output's parent directory is part of the caller's workspace contract
// (RenderingGen creates `output/` before the render). It is created here too,
// idempotently, because the workspace can legitimately disappear between
// prepare and render (the worker's stale-cleanup sweeps any workspace whose
// lease has lapsed), and avio_open then fails with ENOENT — surfacing as
// "failed to open output sink" on a job whose render was otherwise fine.
// create_directories is a no-op when the directory already exists, so the
// normal path is unaffected.
void ensure_parent_directory(const std::string& path) noexcept {
    std::error_code ec;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
}

bool hash_final_file(const std::string& path, std::string& checksum,
                     std::uint64_t* bytes_read = nullptr) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;

    AVHashContext* hash = nullptr;
    if (av_hash_alloc(&hash, "sha256") < 0 || !hash) return false;
    const auto free_hash = [](AVHashContext* value) {
        if (value) av_hash_freep(&value);
    };
    std::unique_ptr<AVHashContext, decltype(free_hash)> guard(hash, free_hash);
    av_hash_init(hash);

    std::array<char, kChecksumReadBufferSize> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = input.gcount();
        if (read > 0) {
            if (bytes_read) *bytes_read += static_cast<std::uint64_t>(read);
            av_hash_update(hash,
                           reinterpret_cast<const std::uint8_t*>(buffer.data()),
                           static_cast<std::size_t>(read));
        }
    }
    if (!input.eof()) return false;
    return finish_sha256_hex(hash, checksum);
}

} // namespace

MuxAvioHashWriter::~MuxAvioHashWriter() {
    if (sink) avio_closep(&sink);
    if (hash) av_hash_freep(&hash);
}

bool MuxAvioHashWriter::open(const std::string& path,
                             bool request_checksum) noexcept {
    checksum_requested = request_checksum;
    if (checksum_requested) {
        if (av_hash_alloc(&hash, "sha256") < 0 || !hash) return false;
        av_hash_init(hash);
    }
    ensure_parent_directory(path);
    const int open_result = avio_open(&sink, path.c_str(), AVIO_FLAG_WRITE);
    if (open_result < 0 || !sink) {
        // Name the real cause. The previous message collapsed ENOENT, EACCES,
        // ENOSPC and EMFILE into one indistinguishable string, which is why a
        // missing output directory could only be diagnosed by re-running the
        // render by hand.
        char error[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(open_result < 0 ? open_result : AVERROR_UNKNOWN, error,
                    sizeof(error));
        spdlog::error("[packet-assembler] avio_open failed for '{}': {}", path,
                      error);
        if (sink) avio_closep(&sink);
        return false;
    }
    const auto initial = avio_tell(sink);
    position = initial >= 0 ? initial : 0;
    append_end = position;
    incremental_valid = checksum_requested;
    return true;
}

int MuxAvioHashWriter::write_packet(void* opaque,
#if LIBAVFORMAT_VERSION_MAJOR >= 61
                                    const std::uint8_t* buffer,
#else
                                    std::uint8_t* buffer,
#endif
                                    int buffer_size) noexcept {
    auto* self = static_cast<MuxAvioHashWriter*>(opaque);
    if (!self || !self->sink || !buffer || buffer_size < 0) return AVERROR(EINVAL);
    if (buffer_size == 0) return 0;

    const bool append_write = self->position == self->append_end;
    if (!append_write) {
        const auto overwritten = self->position < self->append_end
            ? std::min<std::int64_t>(
                self->append_end - self->position,
                static_cast<std::int64_t>(buffer_size))
            : 0;
        self->metrics.output_overwrite_bytes +=
            static_cast<std::uint64_t>(std::max<std::int64_t>(0, overwritten));
        if (self->incremental_valid) {
            self->incremental_valid = false;
            ++self->metrics.incremental_hash_invalidations;
        }
    }

    avio_write(self->sink, buffer, buffer_size);
    if (self->sink->error < 0) return self->sink->error;
    if (self->incremental_valid) {
        av_hash_update(self->hash, buffer, static_cast<std::size_t>(buffer_size));
    }
    self->metrics.output_write_bytes += static_cast<std::uint64_t>(buffer_size);
    self->position += buffer_size;
    self->append_end = std::max(self->append_end, self->position);
    return buffer_size;
}

std::int64_t MuxAvioHashWriter::seek(void* opaque, std::int64_t offset,
                                     int whence) noexcept {
    auto* self = static_cast<MuxAvioHashWriter*>(opaque);
    if (!self || !self->sink) return AVERROR(EINVAL);
    if (whence & AVSEEK_SIZE) return avio_size(self->sink);

    const int seek_whence = whence & ~AVSEEK_FORCE;
    ++self->metrics.output_seek_count;
    const auto position = avio_seek(self->sink, offset, seek_whence);
    if (position >= 0) self->position = position;
    return position;
}

bool MuxAvioHashWriter::close_and_checksum(const std::string& path,
                                           std::string& checksum,
                                           bool& used_reread) noexcept {
    try {
        if (!sink) return false;
        avio_flush(sink);
        if (sink->error < 0) return false;

        const bool needs_reread = checksum_requested && !incremental_valid;
        std::string streaming_checksum;
        if (checksum_requested && !needs_reread &&
            !finish_sha256_hex(hash, streaming_checksum)) {
            return false;
        }

        const int close_result = avio_closep(&sink);
        if (close_result < 0) return false;
        if (!checksum_requested) {
            checksum.clear();
            used_reread = false;
            return true;
        }

        used_reread = needs_reread;
        if (needs_reread) {
            spdlog::warn(
                "[packet-assembler] incremental sha256 invalid for '{}' — "
                "falling back to full-file re-read", path);
            return hash_final_file(path, checksum, &metrics.final_reread_bytes);
        }
        checksum = std::move(streaming_checksum);
        return true;
    } catch (...) {
        spdlog::error(
            "[packet-assembler] close_and_checksum failed with an "
            "unknown exception for '{}'", path);
        return false;
    }
}

MuxHashMetrics MuxAvioHashWriter::hash_metrics() const noexcept {
    return metrics;
}

void MuxSession::delete_writer(MuxAvioHashWriter* writer) noexcept {
    delete writer;
}

} // namespace chronon3d::media
