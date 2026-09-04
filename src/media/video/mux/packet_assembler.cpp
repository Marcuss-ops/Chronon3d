#include <chronon3d/media/video/packet_assembler.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/hash.h>
#include <libavutil/mem.h>
}

namespace chronon3d::media {
namespace {

constexpr int kMuxAvioBufferSize = 64 * 1024;
constexpr std::size_t kChecksumReadBufferSize = 64 * 1024;
constexpr std::size_t kSha256HexChars = 64;

bool finish_sha256_hex(AVHashContext* hash, std::string& checksum) {
    if (!hash || av_hash_get_size(hash) != 32) return false;
    std::array<char, kSha256HexChars + 1> digest{};
    av_hash_final_hex(hash, reinterpret_cast<uint8_t*>(digest.data()), static_cast<int>(digest.size()));
    checksum.assign(digest.data(), kSha256HexChars);
    return true;
}

bool hash_final_file(const std::string& path, std::string& checksum) {
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
            av_hash_update(hash,
                           reinterpret_cast<const std::uint8_t*>(buffer.data()),
                           static_cast<std::size_t>(read));
        }
    }
    if (!input.eof()) return false;
    return finish_sha256_hex(hash, checksum);
}

struct InputFile {
    InputFile() = default;
    AVFormatContext* format{nullptr};
    int video_index{-1};
    int audio_index{-1};

    ~InputFile() {
        if (format) avformat_close_input(&format);
    }
    InputFile(const InputFile&) = delete;
    InputFile& operator=(const InputFile&) = delete;
};

bool same_bytes(const std::uint8_t* a, int a_size,
                const std::uint8_t* b, int b_size) {
    return a_size == b_size && (a_size == 0 ||
        (a != nullptr && b != nullptr && std::memcmp(a, b, static_cast<std::size_t>(a_size)) == 0));
}

bool compatible_codec_parameters(const AVCodecParameters* a,
                                 const AVCodecParameters* b,
                                 std::string& reason) {
    if (!a || !b) { reason = "missing codec parameters"; return false; }
    if (a->codec_type != b->codec_type || a->codec_id != b->codec_id ||
        a->profile != b->profile || a->level != b->level) {
        reason = "codec/profile/level mismatch"; return false;
    }
    if (a->codec_type == AVMEDIA_TYPE_VIDEO &&
        (a->width != b->width || a->height != b->height || a->format != b->format)) {
        reason = "video resolution or pixel format mismatch"; return false;
    }
    if (a->codec_type == AVMEDIA_TYPE_AUDIO &&
        (         a->sample_rate != b->sample_rate || a->format != b->format ||
         a->channel_layout != b->channel_layout || a->channels != b->channels)) {
        reason = "audio sample rate, format, or channel layout mismatch"; return false;
    }
    if (!same_bytes(a->extradata, a->extradata_size, b->extradata, b->extradata_size)) {
        reason = "codec extradata mismatch"; return false;
    }
    return true;
}

bool find_streams(InputFile& input, std::string& reason) {
    if (avformat_find_stream_info(input.format, nullptr) < 0) {
        reason = "failed to read stream information";
        return false;
    }
    for (unsigned i = 0; i < input.format->nb_streams; ++i) {
        const auto type = input.format->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && input.video_index < 0) input.video_index = static_cast<int>(i);
        if (type == AVMEDIA_TYPE_AUDIO && input.audio_index < 0) input.audio_index = static_cast<int>(i);
    }
    if (input.video_index < 0) { reason = "segment has no video stream"; return false; }
    return true;
}

std::int64_t stream_duration(const AVStream* stream) {
    if (!stream || stream->duration == AV_NOPTS_VALUE || stream->duration < 0) return 0;
    return stream->duration;
}

std::int64_t normalize_timestamp(std::int64_t value, std::int64_t first,
                                  std::int64_t offset) {
    if (value == AV_NOPTS_VALUE) return value;
    return value - first + offset;
}

} // namespace

struct MuxAvioHashWriter {
    AVIOContext* sink{nullptr};
    AVHashContext* hash{nullptr};
    std::int64_t position{0};
    std::int64_t append_end{0};
    bool incremental_valid{true};

    MuxAvioHashWriter() = default;
    MuxAvioHashWriter(const MuxAvioHashWriter&) = delete;
    MuxAvioHashWriter& operator=(const MuxAvioHashWriter&) = delete;

    ~MuxAvioHashWriter() {
        if (sink) avio_closep(&sink);
        if (hash) av_hash_freep(&hash);
    }

    [[nodiscard]] bool open(const std::string& path) noexcept {
        if (av_hash_alloc(&hash, "sha256") < 0 || !hash) return false;
        av_hash_init(hash);
        if (avio_open(&sink, path.c_str(), AVIO_FLAG_WRITE) < 0 || !sink) return false;
        const auto initial = avio_tell(sink);
        position = initial >= 0 ? initial : 0;
        append_end = position;
        incremental_valid = true;
        return true;
    }

    static int write_packet(void* opaque, std::uint8_t* buffer, int buffer_size) noexcept {
        auto* self = static_cast<MuxAvioHashWriter*>(opaque);
        if (!self || !self->sink || !buffer || buffer_size < 0) return AVERROR(EINVAL);
        if (buffer_size == 0) return 0;

        const bool append_write = self->position == self->append_end;
        if (!append_write) self->incremental_valid = false;

        avio_write(self->sink, buffer, buffer_size);
        if (self->sink->error < 0) return self->sink->error;

        if (self->incremental_valid) {
            av_hash_update(self->hash, buffer, static_cast<std::size_t>(buffer_size));
        }
        self->position += buffer_size;
        self->append_end = std::max(self->append_end, self->position);
        return buffer_size;
    }

    static std::int64_t seek(void* opaque, std::int64_t offset, int whence) noexcept {
        auto* self = static_cast<MuxAvioHashWriter*>(opaque);
        if (!self || !self->sink) return AVERROR(EINVAL);
        if (whence & AVSEEK_SIZE) return avio_size(self->sink);

        const int seek_whence = whence & ~AVSEEK_FORCE;
        const auto position = avio_seek(self->sink, offset, seek_whence);
        if (position >= 0) self->position = position;
        return position;
    }

    [[nodiscard]] bool close_and_checksum(const std::string& path,
                                          std::string& checksum,
                                          bool& used_reread) noexcept {
        try {
            if (!sink || !hash) return false;
            avio_flush(sink);
            if (sink->error < 0) return false;

            const bool needs_reread = !incremental_valid;
            std::string streaming_checksum;
            if (!needs_reread && !finish_sha256_hex(hash, streaming_checksum)) return false;

            const int close_result = avio_closep(&sink);
            if (close_result < 0) return false;

            used_reread = needs_reread;
            if (needs_reread) return hash_final_file(path, checksum);
            checksum = std::move(streaming_checksum);
            return true;
        } catch (...) {
            return false;
        }
    }
};

SegmentAssemblyResult assemble_segments(const SegmentAssemblyRequest& request) {
    SegmentAssemblyResult result;
    if (request.input_paths.empty() || request.output_path.empty()) {
        result.reason = "segment assembly requires inputs and output path";
        return result;
    }

    std::vector<std::unique_ptr<InputFile>> inputs;
    inputs.reserve(request.input_paths.size());
    for (const auto& path : request.input_paths) {
        auto input = std::make_unique<InputFile>();
        if (avformat_open_input(&input->format, path.c_str(), nullptr, nullptr) < 0) {
            result.reason = "failed to open segment: " + path;
            return result;
        }
        if (!find_streams(*input, result.reason)) return result;
        inputs.push_back(std::move(input));
    }

    const auto* reference_video = inputs.front()->format->streams[inputs.front()->video_index];
    const auto* reference_audio = inputs.front()->audio_index >= 0
        ? inputs.front()->format->streams[inputs.front()->audio_index] : nullptr;
    for (std::size_t i = 1; i < inputs.size(); ++i) {
        const auto* video = inputs[i]->format->streams[inputs[i]->video_index];
        if (!compatible_codec_parameters(reference_video->codecpar, video->codecpar, result.reason)) return result;
        if (video->time_base.num != reference_video->time_base.num ||
            video->time_base.den != reference_video->time_base.den) {
            result.reason = "video time base mismatch";
            return result;
        }
        const auto* audio = inputs[i]->audio_index >= 0
            ? inputs[i]->format->streams[inputs[i]->audio_index] : nullptr;
        if ((reference_audio == nullptr) != (audio == nullptr)) {
            result.reason = "audio stream presence mismatch";
            return result;
        }
        if (reference_audio && !compatible_codec_parameters(reference_audio->codecpar,
                                                              audio->codecpar, result.reason)) return result;
        if (reference_audio && (audio->time_base.num != reference_audio->time_base.num ||
                                audio->time_base.den != reference_audio->time_base.den)) {
            result.reason = "audio time base mismatch";
            return result;
        }
    }

    AVCodecContext* video_codec = avcodec_alloc_context3(nullptr);
    if (!video_codec || avcodec_parameters_to_context(video_codec, reference_video->codecpar) < 0) {
        avcodec_free_context(&video_codec);
        result.reason = "failed to create video mux context";
        return result;
    }
    video_codec->time_base = reference_video->time_base;
    struct CodecContextDeleter { void operator()(AVCodecContext* value) const { if (value) avcodec_free_context(&value); } };
    std::unique_ptr<AVCodecContext, CodecContextDeleter> video_guard(video_codec);

    struct CodecParametersDeleter { void operator()(AVCodecParameters* value) const { if (value) avcodec_parameters_free(&value); } };
    std::unique_ptr<AVCodecParameters, CodecParametersDeleter> audio_params(nullptr);
    std::optional<AudioStreamConfig> audio_config;
    if (reference_audio) {
        auto* params = avcodec_parameters_alloc();
        if (!params || avcodec_parameters_copy(params, reference_audio->codecpar) < 0) {
            if (params) avcodec_parameters_free(&params);
            result.reason = "failed to create audio mux parameters";
            return result;
        }
        audio_params.reset(params);
        audio_config = AudioStreamConfig{audio_params.get(), reference_audio->time_base};
    }

    std::error_code ec;
    if (const auto parent = std::filesystem::path(request.output_path).parent_path();
        !parent.empty()) std::filesystem::create_directories(parent, ec);

    MuxSession mux;
    if (!mux.open(MuxOpenConfig{request.output_path, video_codec, audio_config}, result.reason)) return result;

    std::int64_t video_offset = 0;
    std::int64_t audio_offset = 0;
    for (const auto& input : inputs) {
        auto* video_stream = input->format->streams[input->video_index];
        auto* audio_stream = input->audio_index >= 0 ? input->format->streams[input->audio_index] : nullptr;
        std::int64_t first_video_dts = AV_NOPTS_VALUE;
        std::int64_t first_audio_dts = AV_NOPTS_VALUE;
        bool saw_video = false;
        bool saw_audio = false;
        AVPacket* raw = av_packet_alloc();
        if (!raw) { result.reason = "failed to allocate input packet"; return result; }
        while (av_read_frame(input->format, raw) >= 0) {
            if (raw->stream_index == input->video_index) {
                if (!saw_video) {
                    saw_video = true;
                    first_video_dts = raw->dts != AV_NOPTS_VALUE ? raw->dts : raw->pts;
                    if (&input != &inputs.front() && !(raw->flags & AV_PKT_FLAG_KEY)) {
                        av_packet_free(&raw);
                        result.reason = "segment does not begin at a video keyframe";
                        return result;
                    }
                }
                AVPacket* copy = av_packet_alloc();
                if (!copy || av_packet_ref(copy, raw) < 0) {
                    av_packet_free(&copy); av_packet_unref(raw); av_packet_free(&raw);
                    result.reason = "failed to copy video packet"; return result;
                }
                copy->pts = normalize_timestamp(copy->pts, first_video_dts, video_offset);
                copy->dts = normalize_timestamp(copy->dts, first_video_dts, video_offset);
                copy->stream_index = 0;
                auto owned = std::shared_ptr<AVPacket>(copy, [](AVPacket* packet) { av_packet_free(&packet); });
                if (!mux.submit_video(EncodedPacket{owned, video_stream->time_base,
                                                     (copy->flags & AV_PKT_FLAG_KEY) != 0})) {
                    av_packet_unref(raw); av_packet_free(&raw);
                    result.reason = "failed to write video packet"; return result;
                }
            } else if (audio_stream && raw->stream_index == input->audio_index) {
                if (!saw_audio) {
                    saw_audio = true;
                    first_audio_dts = raw->dts != AV_NOPTS_VALUE ? raw->dts : raw->pts;
                }
                AVPacket* copy = av_packet_alloc();
                if (!copy || av_packet_ref(copy, raw) < 0) {
                    av_packet_free(&copy); av_packet_unref(raw); av_packet_free(&raw);
                    result.reason = "failed to copy audio packet"; return result;
                }
                copy->pts = normalize_timestamp(copy->pts, first_audio_dts, audio_offset);
                copy->dts = normalize_timestamp(copy->dts, first_audio_dts, audio_offset);
                copy->stream_index = 1;
                auto owned = std::shared_ptr<AVPacket>(copy, [](AVPacket* packet) { av_packet_free(&packet); });
                if (!mux.submit_audio(EncodedPacket{owned, audio_stream->time_base, false})) {
                    av_packet_unref(raw); av_packet_free(&raw);
                    result.reason = "failed to write audio packet"; return result;
                }
            }
            av_packet_unref(raw);
        }
        av_packet_free(&raw);
        const auto video_duration = stream_duration(video_stream);
        video_offset += video_duration > 0 ? video_duration : 0;
        if (audio_stream) {
            const auto audio_duration = stream_duration(audio_stream);
            audio_offset += audio_duration > 0 ? audio_duration : 0;
        }
    }
    if (!mux.finalize()) {
        result.reason = "failed to finalize assembled output";
        return result;
    }
    result.output_checksum = std::string(mux.output_checksum());
    result.checksum_used_reread = mux.checksum_used_reread();
    result.success = true;
    return result;
}

void MuxSession::delete_writer(MuxAvioHashWriter* writer) noexcept {
    delete writer;
}

MuxSession::~MuxSession() {
    if (!format_) return;
    if (format_->pb && format_->oformat && !(format_->oformat->flags & AVFMT_NOFILE)) {
        if (writer_) {
            avio_flush(format_->pb);
            avio_context_free(&format_->pb);
        } else {
            avio_closep(&format_->pb);
        }
    }
    writer_.reset();
    avformat_free_context(format_);
}

bool MuxSession::open(const MuxOpenConfig& config, std::string& reason) {
    const auto started = std::chrono::steady_clock::now();
    if (config.output_path.empty() || !config.video_codec) {
        reason = "mux open requires output path and video codec";
        return false;
    }
    output_path_ = config.output_path;
    output_checksum_.clear();
    checksum_used_reread_ = false;
    finalize_attempted_ = false;
    finalized_ = false;
    if (avformat_alloc_output_context2(&format_, nullptr, nullptr,
                                       config.output_path.c_str()) < 0 || !format_) {
        reason = "failed to allocate output format context";
        return false;
    }
    video_stream_ = avformat_new_stream(format_, nullptr);
    if (!video_stream_ || avcodec_parameters_from_context(
            video_stream_->codecpar, config.video_codec) < 0) {
        reason = "failed to create output video stream";
        return false;
    }
    video_stream_->time_base = config.video_codec->time_base;
    if (config.audio) {
        const auto& audio = *config.audio;
        if (!audio.params) {
            reason = "audio stream requires codec parameters";
            return false;
        }
        audio_stream_ = avformat_new_stream(format_, nullptr);
        if (!audio_stream_) {
            reason = "avformat_new_stream failed for audio";
            return false;
        }
        if (avcodec_parameters_copy(audio_stream_->codecpar, audio.params) < 0) {
            reason = "avcodec_parameters_copy failed for audio";
            return false;
        }
        audio_stream_->time_base = audio.time_base;
    }
    if (!write_header(config.output_path, reason)) return false;
    open_header_ms_ = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return true;
}

bool MuxSession::write_header(const std::string& output_path,
                               std::string& reason) {
    if (!format_) {
        reason = "write_header called without a format context";
        return false;
    }
    if (!(format_->oformat->flags & AVFMT_NOFILE)) {
        writer_ = std::unique_ptr<MuxAvioHashWriter, void(*)(MuxAvioHashWriter*)>(
            new MuxAvioHashWriter(), delete_writer);
        if (!writer_->open(output_path)) {
            reason = "failed to open checksum-aware output";
            writer_.reset();
            return false;
        }
        auto* buffer = static_cast<unsigned char*>(av_malloc(kMuxAvioBufferSize));
        if (!buffer) {
            reason = "failed to allocate mux AVIO buffer";
            writer_.reset();
            return false;
        }
        format_->pb = avio_alloc_context(buffer, kMuxAvioBufferSize, 1, writer_.get(),
                                         nullptr, &MuxAvioHashWriter::write_packet,
                                         &MuxAvioHashWriter::seek);
        if (!format_->pb) {
            av_free(buffer);
            reason = "failed to allocate checksum-aware AVIO context";
            writer_.reset();
            return false;
        }
        format_->pb->seekable = writer_->sink->seekable;
        format_->flags |= AVFMT_FLAG_CUSTOM_IO;
    }
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "+faststart", 0);
    const int ret = avformat_write_header(format_, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        reason = "failed to write mux header";
        return false;
    }
    return true;
}

bool MuxSession::submit_video(EncodedPacket encoded) noexcept {
    if (!format_ || !video_stream_ || !encoded.packet) return false;
    const auto started = std::chrono::steady_clock::now();
    auto& packet = *encoded.packet;
    av_packet_rescale_ts(&packet, encoded.time_base, video_stream_->time_base);
    packet.stream_index = video_stream_->index;
    const bool ok = av_interleaved_write_frame(format_, &packet) >= 0;
    packet_write_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return ok;
}

bool MuxSession::submit_audio(EncodedPacket encoded) noexcept {
    if (!format_ || !audio_stream_ || !encoded.packet) return false;
    const auto started = std::chrono::steady_clock::now();
    auto& packet = *encoded.packet;
    av_packet_rescale_ts(&packet, encoded.time_base, audio_stream_->time_base);
    packet.stream_index = audio_stream_->index;
    const bool ok = av_interleaved_write_frame(format_, &packet) >= 0;
    audio_write_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return ok;
}

bool MuxSession::finalize() noexcept {
    if (!format_) return false;
    if (finalize_attempted_) return finalized_;
    finalize_attempted_ = true;

    const auto started = std::chrono::steady_clock::now();
    const bool trailer_ok = av_write_trailer(format_) >= 0;
    trailer_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();

    bool checksum_ok = true;
    if (writer_) {
        if (format_->pb) {
            avio_flush(format_->pb);
            avio_context_free(&format_->pb);
        }
        checksum_ok = writer_->close_and_checksum(output_path_, output_checksum_,
                                                   checksum_used_reread_);
    }
    finalized_ = trailer_ok && checksum_ok;
    return finalized_;
}

bool PacketAssembler::submit(AVPacket& packet, AVRational source_time_base,
                             AVStream* target, bool default_duration) const noexcept {
    if (!format_ || !target) return false;
    if (default_duration && packet.duration <= 0) packet.duration = 1;
    av_packet_rescale_ts(&packet, source_time_base, target->time_base);
    packet.stream_index = target->index;
    return av_interleaved_write_frame(format_, &packet) >= 0;
}

bool PacketAssembler::submit_video(AVPacket& packet,
                                   AVRational source_time_base) const noexcept {
    return submit(packet, source_time_base, video_stream_, true);
}

bool PacketAssembler::submit_copied_video(
    AVPacket& packet, AVRational source_time_base) const noexcept {
    return submit(packet, source_time_base, video_stream_, true);
}

bool PacketAssembler::submit_audio(AVPacket& packet,
                                   AVRational source_time_base) const noexcept {
    return submit(packet, source_time_base, audio_stream_, false);
}

bool PacketAssembler::finalize() const noexcept {
    return format_ && av_write_trailer(format_) >= 0;
}

} // namespace chronon3d::media
