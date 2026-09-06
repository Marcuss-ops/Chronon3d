namespace {

struct GpuOverlayLayer {
  CUdeviceptr ptr{0};
  int w{0};
  int h{0};
  int x{0};
  int y{0};
  float opacity{1.0f};
};

constexpr const char* kKernel = R"CUDA(
struct GpuOverlayItem {
  const unsigned char* ptr;
  int w;
  int h;
  int x;
  int y;
  float opacity;
};

extern "C" __global__ void fused_nv12_composite(
    unsigned char* yout, unsigned char* uvout, int dst_yp, int dst_uvp,
    const unsigned char* ysrc, const unsigned char* uvsrc, int src_yp, int src_uvp,
    const GpuOverlayItem* layers, int layer_count,
    int width, int height) {
  const int bx = (int)(blockIdx.x * blockDim.x + threadIdx.x) * 2;
  const int by = (int)(blockIdx.y * blockDim.y + threadIdx.y) * 2;
  if (bx >= width || by >= height) return;
  const int x1 = min(bx + 1, width - 1);
  const int y1 = min(by + 1, height - 1);
  const int uvx = bx & ~1;
  const int uvy = by >> 1;
  const unsigned char base_uv_u = uvsrc[uvy * src_uvp + uvx];
  const unsigned char base_uv_v = uvsrc[uvy * src_uvp + uvx + 1];
  const float bu = ((float)base_uv_u - 128.0f) / 224.0f;
  const float bv = ((float)base_uv_v - 128.0f) / 224.0f;
  float rgb[4][3];
  int px[4] = {bx, x1, bx, x1};
  int py[4] = {by, by, y1, y1};
  for (int i = 0; i < 4; ++i) {
    const float yy = ((float)ysrc[py[i] * src_yp + px[i]] - 16.0f) / 219.0f;
    float r = yy + 1.5748f * bv;
    float g = yy - 0.1873f * bu - 0.4681f * bv;
    float b = yy + 1.8556f * bu;
    for (int li = 0; li < layer_count; ++li) {
      const auto& l = layers[li];
      const int lx = px[i] - l.x, ly = py[i] - l.y;
      if (!l.ptr || lx < 0 || ly < 0 || lx >= l.w || ly >= l.h) continue;
      const unsigned char* p = l.ptr + ((long long)ly * l.w + lx) * 4;
      const float a = ((float)p[3] / 255.0f) * l.opacity;
      r = (float)p[0] / 255.0f * a + r * (1.0f - a);
      g = (float)p[1] / 255.0f * a + g * (1.0f - a);
      b = (float)p[2] / 255.0f * a + b * (1.0f - a);
    }
    r = fminf(fmaxf(r, 0.0f), 1.0f);
    g = fminf(fmaxf(g, 0.0f), 1.0f);
    b = fminf(fmaxf(b, 0.0f), 1.0f);
    rgb[i][0] = r; rgb[i][1] = g; rgb[i][2] = b;
    yout[py[i] * dst_yp + px[i]] = (unsigned char)fminf(fmaxf(16.0f + 219.0f *
        (0.2126f * r + 0.7152f * g + 0.0722f * b), 0.0f), 255.0f);
  }
  const float ar = (rgb[0][0] + rgb[1][0] + rgb[2][0] + rgb[3][0]) * 0.25f;
  const float ag = (rgb[0][1] + rgb[1][1] + rgb[2][1] + rgb[3][1]) * 0.25f;
  const float ab = (rgb[0][2] + rgb[1][2] + rgb[2][2] + rgb[3][2]) * 0.25f;
  const float u = -0.1146f * ar - 0.3854f * ag + 0.5000f * ab;
  const float v =  0.5000f * ar - 0.4542f * ag - 0.0458f * ab;
  uvout[uvy * dst_uvp + uvx] = (unsigned char)fminf(fmaxf(128.0f + 224.0f * u, 0.0f), 255.0f);
  uvout[uvy * dst_uvp + uvx + 1] = (unsigned char)fminf(fmaxf(128.0f + 224.0f * v, 0.0f), 255.0f);
}
)CUDA";

struct Layer { CUdeviceptr ptr{}; int w{}, h{}, x{}, y{}; float opacity{1.0f}; };

[[noreturn]] void fail(const std::string& s) { throw std::runtime_error(s); }
void cu_ok(CUresult r, const char* what) { if (r != CUDA_SUCCESS) fail(std::string(what) + " CUDA error " + std::to_string((int)r)); }
void av_ok(int r, const char* what) { if (r < 0) { char b[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(r,b,sizeof(b)); fail(std::string(what)+": "+b); } }
void nv_ok(nvrtcResult r, const char* what) { if (r != NVRTC_SUCCESS) fail(std::string(what)+": "+nvrtcGetErrorString(r)); }

#ifdef CHRONON3D_ENABLE_RAW_NVENC
void enc_ok(NVENCSTATUS r, const char* what) { if (r != NV_ENC_SUCCESS && r != NV_ENC_ERR_NEED_MORE_INPUT) fail(std::string(what)+": status="+std::to_string(static_cast<int>(r))); }

class RawNvenc {
 public:
  RawNvenc(CUcontext context, uint32_t width, uint32_t height, AVFormatContext* output)
      : width_(width), height_(height), output_(output) {
    api_.version = NV_ENCODE_API_FUNCTION_LIST_VER; enc_ok(NvEncodeAPICreateInstance(&api_), "NvEncodeAPICreateInstance");
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{}; open.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER; open.deviceType = NV_ENC_DEVICE_TYPE_CUDA; open.device = context; open.apiVersion = NVENCAPI_VERSION;
    enc_ok(api_.nvEncOpenEncodeSessionEx(&open, &encoder_), "nvEncOpenEncodeSessionEx");
    NV_ENC_PRESET_CONFIG preset{}; preset.version = NV_ENC_PRESET_CONFIG_VER; preset.presetCfg.version = NV_ENC_CONFIG_VER;
    NV_ENC_INITIALIZE_PARAMS init{}; init.version = NV_ENC_INITIALIZE_PARAMS_VER; init.encodeGUID = NV_ENC_CODEC_H264_GUID; init.presetGUID = NV_ENC_PRESET_P1_GUID; init.encodeWidth = width_; init.encodeHeight = height_; init.darWidth = width_; init.darHeight = height_; init.frameRateNum = 24; init.frameRateDen = 1; init.enablePTD = 1; init.enableEncodeAsync = 0;
    enc_ok(api_.nvEncGetEncodePresetConfigEx(encoder_, init.encodeGUID, init.presetGUID, NV_ENC_TUNING_INFO_LOW_LATENCY, &preset), "nvEncGetEncodePresetConfigEx");
    init.tuningInfo = NV_ENC_TUNING_INFO_LOW_LATENCY; init.encodeConfig = &preset.presetCfg; enc_ok(api_.nvEncInitializeEncoder(encoder_, &init), "nvEncInitializeEncoder");
    uint8_t sequence[4096]{}; uint32_t sequence_size{}; NV_ENC_SEQUENCE_PARAM_PAYLOAD sequence_params{}; sequence_params.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER; sequence_params.inBufferSize = sizeof(sequence); sequence_params.spsppsBuffer = sequence; sequence_params.outSPSPPSPayloadSize = &sequence_size; enc_ok(api_.nvEncGetSequenceParams(encoder_, &sequence_params), "nvEncGetSequenceParams");
    AVStream* stream = avformat_new_stream(output_, nullptr); if (!stream) fail("raw output stream alloc"); stream_ = stream; stream_->time_base = {1,24}; stream_->codecpar->codec_type = AVMEDIA_TYPE_VIDEO; stream_->codecpar->codec_id = AV_CODEC_ID_H264; stream_->codecpar->width = width_; stream_->codecpar->height = height_; stream_->codecpar->format = AV_PIX_FMT_YUV420P; stream_->codecpar->extradata = static_cast<uint8_t*>(av_mallocz(sequence_size + AV_INPUT_BUFFER_PADDING_SIZE)); if (!stream_->codecpar->extradata) fail("raw extradata alloc"); std::memcpy(stream_->codecpar->extradata, sequence, sequence_size); stream_->codecpar->extradata_size = sequence_size;
    NV_ENC_CREATE_BITSTREAM_BUFFER create_bs{}; create_bs.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER; enc_ok(api_.nvEncCreateBitstreamBuffer(encoder_, &create_bs), "nvEncCreateBitstreamBuffer"); bitstream_ = create_bs.bitstreamBuffer;
  }
  void register_buffer(CUdeviceptr ptr, uint32_t pitch) {
    NV_ENC_REGISTER_RESOURCE resource{}; resource.version = NV_ENC_REGISTER_RESOURCE_VER; resource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR; resource.width = width_; resource.height = height_; resource.pitch = pitch; resource.resourceToRegister = reinterpret_cast<void*>(ptr); resource.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12; resource.bufferUsage = NV_ENC_INPUT_IMAGE; resource.chromaOffsetIn[0] = pitch * height_; enc_ok(api_.nvEncRegisterResource(encoder_, &resource), "nvEncRegisterResource"); registered_ = resource.registeredResource;
  }
  void set_stream(CUstream stream) { enc_ok(api_.nvEncSetIOCudaStreams(encoder_, &stream, &stream), "nvEncSetIOCudaStreams"); }
  void encode(int64_t pts, uint32_t pitch) {
    NV_ENC_MAP_INPUT_RESOURCE map{}; map.version = NV_ENC_MAP_INPUT_RESOURCE_VER; map.registeredResource = registered_; enc_ok(api_.nvEncMapInputResource(encoder_, &map), "nvEncMapInputResource"); mapped_ = map.mappedResource;
    NV_ENC_PIC_PARAMS pic{}; pic.version = NV_ENC_PIC_PARAMS_VER; pic.inputWidth = width_; pic.inputHeight = height_; pic.inputPitch = pitch; pic.inputBuffer = mapped_; pic.outputBitstream = bitstream_; pic.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12; pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME; pic.inputTimeStamp = pts; enc_ok(api_.nvEncEncodePicture(encoder_, &pic), "nvEncEncodePicture");
    NV_ENC_LOCK_BITSTREAM lock{}; lock.version = NV_ENC_LOCK_BITSTREAM_VER; lock.outputBitstream = bitstream_; enc_ok(api_.nvEncLockBitstream(encoder_, &lock), "nvEncLockBitstream"); AVPacket* packet = av_packet_alloc(); if (!packet) fail("raw packet alloc"); av_new_packet(packet, static_cast<int>(lock.bitstreamSizeInBytes)); std::memcpy(packet->data, lock.bitstreamBufferPtr, lock.bitstreamSizeInBytes); packet->pts = packet->dts = pts; packet->duration = 1; packet->stream_index = stream_->index; av_packet_rescale_ts(packet, {1,24}, stream_->time_base); av_interleaved_write_frame(output_, packet); av_packet_free(&packet); enc_ok(api_.nvEncUnlockBitstream(encoder_, bitstream_), "nvEncUnlockBitstream"); enc_ok(api_.nvEncUnmapInputResource(encoder_, mapped_), "nvEncUnmapInputResource"); mapped_ = nullptr;
  }
  void shutdown() {
    if (!encoder_) return;
    if (mapped_) api_.nvEncUnmapInputResource(encoder_, mapped_);
    if (registered_) api_.nvEncUnregisterResource(encoder_, registered_);
    if (bitstream_) api_.nvEncDestroyBitstreamBuffer(encoder_, bitstream_);
    api_.nvEncDestroyEncoder(encoder_);
    encoder_ = nullptr;
    mapped_ = nullptr;
    registered_ = nullptr;
    bitstream_ = nullptr;
  }
  ~RawNvenc() { shutdown(); }
 private:
  uint32_t width_, height_; AVFormatContext* output_{}; AVStream* stream_{}; NV_ENCODE_API_FUNCTION_LIST api_{}; void* encoder_{}; NV_ENC_REGISTERED_PTR registered_{}; NV_ENC_INPUT_PTR mapped_{}; NV_ENC_OUTPUT_PTR bitstream_{};
};
#endif

std::vector<unsigned char> read_rgba(const char* path, int w, int h) {
  std::ifstream f(path, std::ios::binary);
  if (!f) fail(std::string("cannot open layer ") + path);
  std::vector<unsigned char> data((size_t)w * h * 4);
  f.read(reinterpret_cast<char*>(data.data()), (std::streamsize)data.size());
  if (f.gcount() != (std::streamsize)data.size()) fail(std::string("short layer ") + path);
  return data;
}

enum AVPixelFormat hw_format(AVCodecContext*, const enum AVPixelFormat* formats) {
  for (const auto* p = formats; *p != AV_PIX_FMT_NONE; ++p) if (*p == AV_PIX_FMT_CUDA) return *p;
  return AV_PIX_FMT_NONE;
}

void copy_plane(CUdeviceptr dst, int dp, const uint8_t* src, int sp, int w, int h, CUstream stream) {
  CUDA_MEMCPY2D c{}; c.srcMemoryType = CU_MEMORYTYPE_DEVICE; c.srcDevice = (CUdeviceptr)src;
  c.srcPitch = sp; c.dstMemoryType = CU_MEMORYTYPE_DEVICE; c.dstDevice = dst;
  c.dstPitch = dp; c.WidthInBytes = w; c.Height = h; cu_ok(cuMemcpy2DAsync(&c, stream), "cuMemcpy2DAsync");
}

struct BenchTimings {
  double startup_ms{0.0};

  double demux_ms{0.0};
  double decode_send_ms{0.0};
  double decode_receive_ms{0.0};

  double gpu_submit_ms{0.0};
  double gpu_execute_ms{0.0};
  double gpu_event_wait_ms{0.0};

  double encode_send_ms{0.0};
  double encode_receive_ms{0.0};

  double mux_write_ms{0.0};
  double trailer_ms{0.0};

  double queue_wait_ms{0.0};
  double teardown_ms{0.0};

  double pipeline_wall_ms{0.0};
  double process_wall_ms{0.0};

  uint64_t queue_high_watermark{0};
};

BenchTimings g_timings;

using Clock = std::chrono::high_resolution_clock;

void write_packets(AVCodecContext* enc, AVFormatContext* out, AVPacket* pkt, AVFrame* frame, uint64_t& encoded_packet_count) {
  auto t0 = Clock::now();
  av_ok(avcodec_send_frame(enc, frame), "avcodec_send_frame");
  auto t1 = Clock::now();
  g_timings.encode_send_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();

  for (;;) {
    auto t_rec0 = Clock::now();
    int r = avcodec_receive_packet(enc, pkt);
    auto t_rec1 = Clock::now();
    g_timings.encode_receive_ms += std::chrono::duration<double, std::milli>(t_rec1 - t_rec0).count();

    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
    av_ok(r, "avcodec_receive_packet");
    pkt->stream_index = 0;
    pkt->pts = pkt->dts = static_cast<int64_t>(encoded_packet_count++);
    pkt->duration = 1;
    av_packet_rescale_ts(pkt, enc->time_base, out->streams[0]->time_base);

    auto t_mux0 = Clock::now();
    av_ok(av_interleaved_write_frame(out, pkt), "av_interleaved_write_frame");
    auto t_mux1 = Clock::now();
    g_timings.mux_write_ms += std::chrono::duration<double, std::milli>(t_mux1 - t_mux0).count();

    av_packet_unref(pkt);
  }
}

// Race-free Frame Slot Ring with Explicit Handshake
enum class SlotState {
  Free,
  GpuWriting,
  ReadyForEncode,
  Encoding
};

struct FrameSlot {
  size_t slot_id{0};
  AVFrame* frame{nullptr};
  CUevent gpu_ready_ev{nullptr};
  CUevent gpu_start_timing_ev{nullptr};
  CUevent gpu_end_timing_ev{nullptr};
  std::atomic<SlotState> state{SlotState::Free};
};

class FrameSlotRing {
 public:
  FrameSlotRing(size_t capacity, AVBufferRef* frames_ctx, int width, int height)
      : slots_(capacity) {
    for (size_t i = 0; i < capacity; ++i) {
      slots_[i].slot_id = i;
      slots_[i].frame = av_frame_alloc();
      if (!slots_[i].frame) fail("av_frame_alloc");
      slots_[i].frame->format = AV_PIX_FMT_CUDA;
      slots_[i].frame->width = width;
      slots_[i].frame->height = height;
      slots_[i].frame->hw_frames_ctx = av_buffer_ref(frames_ctx);
      av_ok(av_hwframe_get_buffer(frames_ctx, slots_[i].frame, 0), "av_hwframe_get_buffer");
      cu_ok(cuEventCreate(&slots_[i].gpu_ready_ev, CU_EVENT_DISABLE_TIMING), "cuEventCreate ready");
      cu_ok(cuEventCreate(&slots_[i].gpu_start_timing_ev, CU_EVENT_DEFAULT), "cuEventCreate start");
      cu_ok(cuEventCreate(&slots_[i].gpu_end_timing_ev, CU_EVENT_DEFAULT), "cuEventCreate end");
      slots_[i].state.store(SlotState::Free, std::memory_order_relaxed);
    }
  }

  FrameSlot* acquire_free_slot() {
    auto t_wait0 = Clock::now();
    std::unique_lock<std::mutex> lock(mtx_);
    cv_free_.wait(lock, [this]() {
      for (auto& s : slots_) {
        if (s.state.load(std::memory_order_acquire) == SlotState::Free) return true;
      }
      return false;
    });
    auto t_wait1 = Clock::now();
    g_timings.queue_wait_ms += std::chrono::duration<double, std::milli>(t_wait1 - t_wait0).count();

    for (size_t i = 0; i < slots_.size(); ++i) {
      size_t idx = (next_producer_idx_ + i) % slots_.size();
      if (slots_[idx].state.load(std::memory_order_acquire) == SlotState::Free) {
        next_producer_idx_ = (idx + 1) % slots_.size();
        slots_[idx].state.store(SlotState::GpuWriting, std::memory_order_release);
        return &slots_[idx];
      }
    }
    fail("unreachable free slot search");
  }

  void mark_ready(FrameSlot* slot) {
    slot->state.store(SlotState::ReadyForEncode, std::memory_order_release);
  }

  void release_slot(FrameSlot* slot) {
    slot->state.store(SlotState::Free, std::memory_order_release);
    {
      std::lock_guard<std::mutex> lock(mtx_);
    }
    cv_free_.notify_one();
  }

  ~FrameSlotRing() {
    for (auto& s : slots_) {
      if (s.frame) av_frame_free(&s.frame);
      if (s.gpu_ready_ev) cuEventDestroy(s.gpu_ready_ev);
      if (s.gpu_start_timing_ev) cuEventDestroy(s.gpu_start_timing_ev);
      if (s.gpu_end_timing_ev) cuEventDestroy(s.gpu_end_timing_ev);
    }
  }

 private:
  std::vector<FrameSlot> slots_;
  size_t next_producer_idx_{0};
  std::mutex mtx_;
  std::condition_variable cv_free_;
};

} // namespace
