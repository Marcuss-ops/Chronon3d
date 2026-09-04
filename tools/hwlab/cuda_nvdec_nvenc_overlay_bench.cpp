// Native CUDA video benchmark: NVDEC -> CUDA NV12 composite -> NVENC.
// Overlay layers are small RGBA device buffers uploaded once.  The video
// frame never enters host memory after decode.
#include <cuda.h>
#include <nvrtc.h>
#ifdef CHRONON3D_ENABLE_RAW_NVENC
#include <nvEncodeAPI.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>



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

// Ceiling benchmark modes
int run_demux_only(const char* input) {
  auto start = Clock::now();
  AVFormatContext* in{}; av_ok(avformat_open_input(&in, input, nullptr, nullptr), "open");
  av_ok(avformat_find_stream_info(in, nullptr), "find_stream");
  int si = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  AVPacket* pkt = av_packet_alloc();
  uint64_t count = 0;
  while (av_read_frame(in, pkt) >= 0) {
    if (pkt->stream_index == si) count++;
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  avformat_close_input(&in);
  auto end = Clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "[CEILING DEMUX-ONLY] frames=" << count << " total=" << ms << " ms (" << (1000.0 * count / ms) << " FPS)\n";
  return 0;
}

int run_decode_only_warm(const char* input) {
  CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev, 0), "cuDeviceGet");
  cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
  cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
  cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");

  AVFormatContext* in{}; av_ok(avformat_open_input(&in, input, nullptr, nullptr), "open");
  av_ok(avformat_find_stream_info(in, nullptr), "find_stream");
  int si = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  const AVCodec* dec = avcodec_find_decoder_by_name("h264_cuvid");
  if (!dec) dec = avcodec_find_decoder(in->streams[si]->codecpar->codec_id);
  AVCodecContext* dc = avcodec_alloc_context3(dec);
  av_ok(avcodec_parameters_to_context(dc, in->streams[si]->codecpar), "params");
  dc->get_format = hw_format;
  dc->extra_hw_frames = 8;
  AVBufferRef* hwdev{}; av_ok(av_hwdevice_ctx_create(&hwdev, AV_HWDEVICE_TYPE_CUDA, "0", nullptr, AV_CUDA_USE_PRIMARY_CONTEXT), "hwdev");
  dc->hw_device_ctx = av_buffer_ref(hwdev);
  av_ok(avcodec_open2(dc, dec, nullptr), "open2");

  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  uint64_t count = 0;

  // WARM DECODE MEASUREMENT ONLY
  auto start = Clock::now();
  while (av_read_frame(in, pkt) >= 0) {
    if (pkt->stream_index == si) {
      av_ok(avcodec_send_packet(dc, pkt), "send");
      while (avcodec_receive_frame(dc, frame) >= 0) count++;
    }
    av_packet_unref(pkt);
  }
  avcodec_send_packet(dc, nullptr);
  while (avcodec_receive_frame(dc, frame) >= 0) count++;
  auto end = Clock::now();

  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dc);
  avformat_close_input(&in);
  av_buffer_unref(&hwdev);
  cuDevicePrimaryCtxRelease(dev);

  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "[CEILING NVDEC-ONLY-WARM] decoder=" << dec->name << " frames=" << count << " warm_throughput=" << ms << " ms (" << (1000.0 * count / ms) << " FPS)\n";
  return 0;
}

int run_gpu_overlay_only(int count, int W, int H, const char* wm_path, int ww, int wh, const char* sub_path, int sw, int sh) {
  CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev, 0), "cuDeviceGet");
  cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
  cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
  cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");
  CUstream stream{}; cu_ok(cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING), "stream");

  nvrtcProgram prog{}; nv_ok(nvrtcCreateProgram(&prog, kKernel, "composite.cu", 0, nullptr, nullptr), "createProg");
  const char* opts[] = {"--gpu-architecture=compute_75"};
  nv_ok(nvrtcCompileProgram(prog, 1, opts), "compileProg");
  size_t ptx_size{}; nv_ok(nvrtcGetPTXSize(prog, &ptx_size), "ptxSize");
  std::vector<char> ptx(ptx_size); nv_ok(nvrtcGetPTX(prog, ptx.data()), "getPTX"); nvrtcDestroyProgram(&prog);
  CUmodule mod{}; CUfunction kernel{}; cu_ok(cuModuleLoadData(&mod, ptx.data()), "loadData");
  cu_ok(cuModuleGetFunction(&kernel, mod, "fused_nv12_composite"), "getFunc");

  const auto wm_host = read_rgba(wm_path, ww, wh), sub_host = read_rgba(sub_path, sw, sh);
  Layer wm{.ptr=0, .w=ww, .h=wh, .x=100, .y=100, .opacity=0.75f};
  Layer sub{.ptr=0, .w=sw, .h=sh, .x=200, .y=800, .opacity=1.0f};
  cu_ok(cuMemAlloc(&wm.ptr, wm_host.size()), "cuMemAlloc(wm)"); cu_ok(cuMemAlloc(&sub.ptr, sub_host.size()), "cuMemAlloc(sub)");
  cu_ok(cuMemcpyHtoD(wm.ptr, wm_host.data(), wm_host.size()), "cuMemcpyHtoD(wm)"); cu_ok(cuMemcpyHtoD(sub.ptr, sub_host.data(), sub_host.size()), "cuMemcpyHtoD(sub)");

  struct GpuOverlayItemHost { const unsigned char* ptr; int w, h, x, y; float opacity; };
  std::vector<GpuOverlayItemHost> layer_items = {
    {reinterpret_cast<const unsigned char*>(wm.ptr), wm.w, wm.h, wm.x, wm.y, wm.opacity},
    {reinterpret_cast<const unsigned char*>(sub.ptr), sub.w, sub.h, sub.x, sub.y, sub.opacity}
  };
  CUdeviceptr gpu_layers_table{};
  cu_ok(cuMemAlloc(&gpu_layers_table, layer_items.size() * sizeof(GpuOverlayItemHost)), "cuMemAlloc(layer_items)");
  cu_ok(cuMemcpyHtoD(gpu_layers_table, layer_items.data(), layer_items.size() * sizeof(GpuOverlayItemHost)), "cuMemcpyHtoD(layer_items)");
  int layer_count = static_cast<int>(layer_items.size());

  CUdeviceptr src_y{}, src_uv{}, dst_y{}, dst_uv{};
  size_t pitch = (W + 31) & ~31;
  cu_ok(cuMemAlloc(&src_y, pitch * H), "alloc"); cu_ok(cuMemAlloc(&src_uv, pitch * H / 2), "alloc");
  cu_ok(cuMemAlloc(&dst_y, pitch * H), "alloc"); cu_ok(cuMemAlloc(&dst_uv, pitch * H / 2), "alloc");

  CUevent ev_start{}, ev_end{};
  cu_ok(cuEventCreate(&ev_start, CU_EVENT_DEFAULT), "create");
  cu_ok(cuEventCreate(&ev_end, CU_EVENT_DEFAULT), "create");

  int ipitch = (int)pitch;
  void* args[] = {&dst_y, &dst_uv, &ipitch, &ipitch, &src_y, &src_uv, &ipitch, &ipitch, &gpu_layers_table, &layer_count, &W, &H};

  auto wall_start = Clock::now();
  cu_ok(cuEventRecord(ev_start, stream), "record start");
  for (int i = 0; i < count; ++i) {
    cu_ok(cuLaunchKernel(kernel, (W + 31) / 32, (H + 31) / 32, 1, 32, 16, 1, 0, stream, args, nullptr), "launch");
  }
  cu_ok(cuEventRecord(ev_end, stream), "record end");
  cu_ok(cuEventSynchronize(ev_end), "sync");
  auto wall_end = Clock::now();

  float gpu_ms = 0.0f;
  cu_ok(cuEventElapsedTime(&gpu_ms, ev_start, ev_end), "elapsed");
  double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

  cuEventDestroy(ev_start); cuEventDestroy(ev_end);
  cuMemFree(src_y); cuMemFree(src_uv); cuMemFree(dst_y); cuMemFree(dst_uv);
  cuMemFree(wm.ptr); cuMemFree(sub.ptr); cuMemFree(gpu_layers_table);
  cuModuleUnload(mod); cuStreamDestroy(stream); cuDevicePrimaryCtxRelease(dev);

  std::cout << "[CEILING GPU-OVERLAY-ONLY] frames=" << count << " pure_gpu=" << gpu_ms << " ms (" << (1000.0 * count / gpu_ms) << " FPS) wall=" << wall_ms << " ms\n";
  return 0;
}

int run_decode_only(const char* input) {
  auto start = Clock::now();
  CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev, 0), "cuDeviceGet");
  cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
  cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
  cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");

  AVFormatContext* in{}; av_ok(avformat_open_input(&in, input, nullptr, nullptr), "open");
  av_ok(avformat_find_stream_info(in, nullptr), "find_stream");
  int si = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  const AVCodec* dec = avcodec_find_decoder_by_name("h264_cuvid");
  if (!dec) dec = avcodec_find_decoder(in->streams[si]->codecpar->codec_id);
  AVCodecContext* dc = avcodec_alloc_context3(dec);
  av_ok(avcodec_parameters_to_context(dc, in->streams[si]->codecpar), "params");
  dc->get_format = hw_format;
  dc->extra_hw_frames = 8;
  AVBufferRef* hwdev{}; av_ok(av_hwdevice_ctx_create(&hwdev, AV_HWDEVICE_TYPE_CUDA, "0", nullptr, AV_CUDA_USE_PRIMARY_CONTEXT), "hwdev");
  dc->hw_device_ctx = av_buffer_ref(hwdev);
  av_ok(avcodec_open2(dc, dec, nullptr), "open2");

  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  uint64_t count = 0;
  while (av_read_frame(in, pkt) >= 0) {
    if (pkt->stream_index == si) {
      av_ok(avcodec_send_packet(dc, pkt), "send");
      while (avcodec_receive_frame(dc, frame) >= 0) count++;
    }
    av_packet_unref(pkt);
  }
  avcodec_send_packet(dc, nullptr);
  while (avcodec_receive_frame(dc, frame) >= 0) count++;

  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dc);
  avformat_close_input(&in);
  av_buffer_unref(&hwdev);
  cuDevicePrimaryCtxRelease(dev);

  auto end = Clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "[CEILING NVDEC-ONLY] decoder=" << dec->name << " frames=" << count << " total=" << ms << " ms (" << (1000.0 * count / ms) << " FPS)\n";
  return 0;
}

int run_gpu_only(int count, int W, int H) {
  auto start = Clock::now();
  CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev, 0), "cuDeviceGet");
  cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
  cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
  cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");
  CUstream stream{}; cu_ok(cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING), "stream");

  nvrtcProgram prog{}; nv_ok(nvrtcCreateProgram(&prog, kKernel, "composite.cu", 0, nullptr, nullptr), "createProg");
  const char* opts[] = {"--gpu-architecture=compute_75"};
  nv_ok(nvrtcCompileProgram(prog, 1, opts), "compileProg");
  size_t ptx_size{}; nv_ok(nvrtcGetPTXSize(prog, &ptx_size), "ptxSize");
  std::vector<char> ptx(ptx_size); nv_ok(nvrtcGetPTX(prog, ptx.data()), "getPTX"); nvrtcDestroyProgram(&prog);
  CUmodule mod{}; CUfunction kernel{}; cu_ok(cuModuleLoadData(&mod, ptx.data()), "loadData");
  cu_ok(cuModuleGetFunction(&kernel, mod, "fused_nv12_composite"), "getFunc");

  CUdeviceptr src_y{}, src_uv{}, dst_y{}, dst_uv{};
  size_t pitch = (W + 31) & ~31;
  cu_ok(cuMemAlloc(&src_y, pitch * H), "alloc"); cu_ok(cuMemAlloc(&src_uv, pitch * H / 2), "alloc");
  cu_ok(cuMemAlloc(&dst_y, pitch * H), "alloc"); cu_ok(cuMemAlloc(&dst_uv, pitch * H / 2), "alloc");

  CUevent ev_start{}, ev_end{};
  cu_ok(cuEventCreate(&ev_start, CU_EVENT_DEFAULT), "create");
  cu_ok(cuEventCreate(&ev_end, CU_EVENT_DEFAULT), "create");

  CUdeviceptr dummy_table{0}; int layer_count = 0; int ipitch = (int)pitch;
  void* args[] = {&dst_y, &dst_uv, &ipitch, &ipitch, &src_y, &src_uv, &ipitch, &ipitch, &dummy_table, &layer_count, &W, &H};

  cu_ok(cuEventRecord(ev_start, stream), "record start");
  for (int i = 0; i < count; ++i) {
    cu_ok(cuLaunchKernel(kernel, (W + 31) / 32, (H + 31) / 32, 1, 32, 16, 1, 0, stream, args, nullptr), "launch");
  }
  cu_ok(cuEventRecord(ev_end, stream), "record end");
  cu_ok(cuEventSynchronize(ev_end), "sync");

  float gpu_ms = 0.0f;
  cu_ok(cuEventElapsedTime(&gpu_ms, ev_start, ev_end), "elapsed");
  auto end = Clock::now();
  double wall_ms = std::chrono::duration<double, std::milli>(end - start).count();

  cuEventDestroy(ev_start); cuEventDestroy(ev_end);
  cuMemFree(src_y); cuMemFree(src_uv); cuMemFree(dst_y); cuMemFree(dst_uv);
  cuModuleUnload(mod); cuStreamDestroy(stream); cuDevicePrimaryCtxRelease(dev);

  std::cout << "[CEILING GPU-ONLY] frames=" << count << " pure_gpu=" << gpu_ms << " ms (" << (1000.0 * count / gpu_ms) << " FPS) wall=" << wall_ms << " ms\n";
  return 0;
}

int run_encode_only_warm(int count, int W, int H, const char* output) {
  CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev, 0), "cuDeviceGet");
  cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
  cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
  cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");

  AVFormatContext* out{}; avformat_alloc_output_context2(&out, nullptr, nullptr, output);
  const AVCodec* enc_codec = avcodec_find_encoder_by_name("h264_nvenc");
  AVStream* st = avformat_new_stream(out, enc_codec);
  AVCodecContext* ec = avcodec_alloc_context3(enc_codec);
  ec->width = W; ec->height = H; ec->time_base = {1, 24}; ec->framerate = {24, 1}; ec->pix_fmt = AV_PIX_FMT_CUDA;
  ec->bit_rate = 0; ec->thread_count = 1; ec->thread_type = 0; ec->max_b_frames = 0; ec->flags |= AV_CODEC_FLAG_LOW_DELAY;
  av_opt_set(ec->priv_data, "preset", "p1", 0); av_opt_set_int(ec->priv_data, "qp", 23, 0);
  av_opt_set_int(ec->priv_data, "surfaces", 8, 0); av_opt_set_int(ec->priv_data, "zerolatency", 1, 0);

  AVBufferRef* hwdev{}; av_ok(av_hwdevice_ctx_create(&hwdev, AV_HWDEVICE_TYPE_CUDA, "0", nullptr, AV_CUDA_USE_PRIMARY_CONTEXT), "hwdev");
  AVBufferRef* frames = av_hwframe_ctx_alloc(hwdev);
  auto* fctx = (AVHWFramesContext*)frames->data;
  fctx->format = AV_PIX_FMT_CUDA; fctx->sw_format = AV_PIX_FMT_NV12; fctx->width = W; fctx->height = H; fctx->initial_pool_size = 16;
  av_ok(av_hwframe_ctx_init(frames), "init");
  ec->hw_frames_ctx = av_buffer_ref(frames);
  av_ok(avcodec_open2(ec, enc_codec, nullptr), "open");
  av_ok(avcodec_parameters_from_context(st->codecpar, ec), "params");
  st->time_base = ec->time_base;

  if (!(out->oformat->flags & AVFMT_NOFILE)) av_ok(avio_open(&out->pb, output, AVIO_FLAG_WRITE), "avio");
  av_ok(avformat_write_header(out, nullptr), "header");

  std::vector<AVFrame*> pool(16, nullptr);
  for (auto& f : pool) {
    f = av_frame_alloc();
    f->format = AV_PIX_FMT_CUDA; f->width = W; f->height = H; f->hw_frames_ctx = av_buffer_ref(frames);
    av_ok(av_hwframe_get_buffer(frames, f, 0), "get_buffer");
  }

  AVPacket* pkt = av_packet_alloc();
  uint64_t encoded_packet_count = 0;

  // WARM SYNC ENCODE MEASUREMENT ONLY
  auto start = Clock::now();
  for (int i = 0; i < count; ++i) {
    AVFrame* f = pool[i % pool.size()];
    f->pts = i;
    write_packets(ec, out, pkt, f, encoded_packet_count);
  }
  write_packets(ec, out, pkt, nullptr, encoded_packet_count);
  av_write_trailer(out);
  auto end = Clock::now();

  if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
  for (auto& f : pool) av_frame_free(&f);
  av_packet_free(&pkt); avcodec_free_context(&ec); avformat_free_context(out);
  av_buffer_unref(&frames); av_buffer_unref(&hwdev);
  cuDevicePrimaryCtxRelease(dev);

  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "[CEILING NVENC-ONLY-WARM-SYNC] frames=" << count << " warm_throughput=" << ms << " ms (" << (1000.0 * count / ms) << " FPS)\n";
  return 0;
}

int run_encode_only_async(int count, int W, int H, const char* output) {
  CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev, 0), "cuDeviceGet");
  cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
  cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
  cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");

  AVFormatContext* out{}; avformat_alloc_output_context2(&out, nullptr, nullptr, output);
  const AVCodec* enc_codec = avcodec_find_encoder_by_name("h264_nvenc");
  AVStream* st = avformat_new_stream(out, enc_codec);
  AVCodecContext* ec = avcodec_alloc_context3(enc_codec);
  ec->width = W; ec->height = H; ec->time_base = {1, 24}; ec->framerate = {24, 1}; ec->pix_fmt = AV_PIX_FMT_CUDA;
  ec->bit_rate = 0; ec->thread_count = 1; ec->thread_type = 0; ec->max_b_frames = 0; ec->flags |= AV_CODEC_FLAG_LOW_DELAY;
  av_opt_set(ec->priv_data, "preset", "p1", 0); av_opt_set_int(ec->priv_data, "qp", 23, 0);
  av_opt_set_int(ec->priv_data, "surfaces", 8, 0); av_opt_set_int(ec->priv_data, "zerolatency", 1, 0);

  AVBufferRef* hwdev{}; av_ok(av_hwdevice_ctx_create(&hwdev, AV_HWDEVICE_TYPE_CUDA, "0", nullptr, AV_CUDA_USE_PRIMARY_CONTEXT), "hwdev");
  AVBufferRef* frames = av_hwframe_ctx_alloc(hwdev);
  auto* fctx = (AVHWFramesContext*)frames->data;
  fctx->format = AV_PIX_FMT_CUDA; fctx->sw_format = AV_PIX_FMT_NV12; fctx->width = W; fctx->height = H; fctx->initial_pool_size = 16;
  av_ok(av_hwframe_ctx_init(frames), "init");
  ec->hw_frames_ctx = av_buffer_ref(frames);
  av_ok(avcodec_open2(ec, enc_codec, nullptr), "open");
  av_ok(avcodec_parameters_from_context(st->codecpar, ec), "params");
  st->time_base = ec->time_base;

  if (!(out->oformat->flags & AVFMT_NOFILE)) av_ok(avio_open(&out->pb, output, AVIO_FLAG_WRITE), "avio");
  av_ok(avformat_write_header(out, nullptr), "header");

  const size_t kRingSlots = 16;
  FrameSlotRing slot_ring(kRingSlots, frames, W, H);

  struct AsyncItem { FrameSlot* slot{nullptr}; uint64_t frame_id{0}; bool is_flush{false}; };
  std::queue<AsyncItem> q;
  std::mutex q_mtx;
  std::condition_variable q_cv;
  uint64_t encoded_packet_count = 0;

  std::thread drain_thread([&]() {
    AVPacket* local_pkt = av_packet_alloc();
    while (true) {
      AsyncItem item{};
      {
        std::unique_lock<std::mutex> lock(q_mtx);
        q_cv.wait(lock, [&]() { return !q.empty(); });
        item = q.front();
        q.pop();
      }
      if (item.is_flush) {
        write_packets(ec, out, local_pkt, nullptr, encoded_packet_count);
        break;
      }
      item.slot->frame->pts = static_cast<int64_t>(item.frame_id);
      write_packets(ec, out, local_pkt, item.slot->frame, encoded_packet_count);
      slot_ring.release_slot(item.slot);
    }
    av_packet_free(&local_pkt);
  });

  // WARM ASYNC ENCODE MEASUREMENT ONLY
  auto start = Clock::now();
  for (int i = 0; i < count; ++i) {
    FrameSlot* slot = slot_ring.acquire_free_slot();
    slot_ring.mark_ready(slot);
    {
      std::lock_guard<std::mutex> lock(q_mtx);
      q.push({slot, static_cast<uint64_t>(i), false});
    }
    q_cv.notify_one();
  }

  {
    std::lock_guard<std::mutex> lock(q_mtx);
    q.push({nullptr, 0, true});
  }
  q_cv.notify_one();
  if (drain_thread.joinable()) drain_thread.join();
  av_write_trailer(out);
  auto end = Clock::now();

  if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
  avcodec_free_context(&ec); avformat_free_context(out);
  av_buffer_unref(&frames); av_buffer_unref(&hwdev);
  cuDevicePrimaryCtxRelease(dev);

  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "[CEILING NVENC-ONLY-WARM-ASYNC] frames=" << count << " warm_throughput=" << ms << " ms (" << (1000.0 * count / ms) << " FPS)\n";
  return 0;
}

int run_encode_only(int count, int W, int H, const char* output) {
  auto start = Clock::now();
  CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev, 0), "cuDeviceGet");
  cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
  cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
  cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");

  AVFormatContext* out{}; avformat_alloc_output_context2(&out, nullptr, nullptr, output);
  const AVCodec* enc_codec = avcodec_find_encoder_by_name("h264_nvenc");
  AVStream* st = avformat_new_stream(out, enc_codec);
  AVCodecContext* ec = avcodec_alloc_context3(enc_codec);
  ec->width = W; ec->height = H; ec->time_base = {1, 24}; ec->framerate = {24, 1}; ec->pix_fmt = AV_PIX_FMT_CUDA;
  ec->bit_rate = 0; ec->thread_count = 1; ec->thread_type = 0; ec->max_b_frames = 0; ec->flags |= AV_CODEC_FLAG_LOW_DELAY;
  av_opt_set(ec->priv_data, "preset", "p1", 0); av_opt_set_int(ec->priv_data, "qp", 23, 0);
  av_opt_set_int(ec->priv_data, "surfaces", 8, 0); av_opt_set_int(ec->priv_data, "zerolatency", 1, 0);

  AVBufferRef* hwdev{}; av_ok(av_hwdevice_ctx_create(&hwdev, AV_HWDEVICE_TYPE_CUDA, "0", nullptr, AV_CUDA_USE_PRIMARY_CONTEXT), "hwdev");
  AVBufferRef* frames = av_hwframe_ctx_alloc(hwdev);
  auto* fctx = (AVHWFramesContext*)frames->data;
  fctx->format = AV_PIX_FMT_CUDA; fctx->sw_format = AV_PIX_FMT_NV12; fctx->width = W; fctx->height = H; fctx->initial_pool_size = 8;
  av_ok(av_hwframe_ctx_init(frames), "init");
  ec->hw_frames_ctx = av_buffer_ref(frames);
  av_ok(avcodec_open2(ec, enc_codec, nullptr), "open");
  av_ok(avcodec_parameters_from_context(st->codecpar, ec), "params");
  st->time_base = ec->time_base;

  if (!(out->oformat->flags & AVFMT_NOFILE)) av_ok(avio_open(&out->pb, output, AVIO_FLAG_WRITE), "avio");
  av_ok(avformat_write_header(out, nullptr), "header");

  AVFrame* frame = av_frame_alloc();
  frame->format = AV_PIX_FMT_CUDA; frame->width = W; frame->height = H; frame->hw_frames_ctx = av_buffer_ref(frames);
  av_ok(av_hwframe_get_buffer(frames, frame, 0), "get_buffer");

  AVPacket* pkt = av_packet_alloc();
  uint64_t encoded_packet_count = 0;
  for (int i = 0; i < count; ++i) {
    frame->pts = i;
    write_packets(ec, out, pkt, frame, encoded_packet_count);
  }
  write_packets(ec, out, pkt, nullptr, encoded_packet_count);
  av_write_trailer(out);

  if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
  av_frame_free(&frame); av_packet_free(&pkt); avcodec_free_context(&ec); avformat_free_context(out);
  av_buffer_unref(&frames); av_buffer_unref(&hwdev);
  cuDevicePrimaryCtxRelease(dev);

  auto end = Clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  std::cout << "[CEILING NVENC-ONLY] frames=" << count << " total=" << ms << " ms (" << (1000.0 * count / ms) << " FPS)\n";
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  av_log_set_level(AV_LOG_ERROR);
  if (argc >= 2 && std::string(argv[1]) == "--demux-only") {
    return run_demux_only(argv[2]);
  }
  if (argc >= 2 && std::string(argv[1]) == "--decode-only-warm") {
    return run_decode_only_warm(argv[2]);
  }
  if (argc >= 2 && std::string(argv[1]) == "--decode-only") {
    return run_decode_only(argv[2]);
  }
  if (argc >= 2 && std::string(argv[1]) == "--gpu-overlay-only") {
    return run_gpu_overlay_only(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), argv[5], std::atoi(argv[6]), std::atoi(argv[7]), argv[8], std::atoi(argv[9]), std::atoi(argv[10]));
  }
  if (argc >= 2 && std::string(argv[1]) == "--gpu-only") {
    return run_gpu_only(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]));
  }
  if (argc >= 2 && std::string(argv[1]) == "--encode-only-warm") {
    return run_encode_only_warm(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), argv[5]);
  }
  if (argc >= 2 && std::string(argv[1]) == "--encode-only-async") {
    return run_encode_only_async(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), argv[5]);
  }
  if (argc >= 2 && std::string(argv[1]) == "--encode-only") {
    return run_encode_only(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), argv[5]);
  }

  if (argc != 13) {
    std::fprintf(stderr, "usage: %s input.mp4 output.mp4 wm.rgba wm_w wm_h wm_x wm_y sub.rgba sub_w sub_h sub_x sub_y\n", argv[0]);
    return 2;
  }
  try {
    auto process_start = Clock::now();
    auto startup_start = Clock::now();

    const char* input = argv[1]; const char* output = argv[2];
    const int ww = std::atoi(argv[4]), wh = std::atoi(argv[5]), wx = std::atoi(argv[6]), wy = std::atoi(argv[7]);
    const int sw = std::atoi(argv[9]), sh = std::atoi(argv[10]), sx = std::atoi(argv[11]), sy = std::atoi(argv[12]);
    CUdevice dev{}; CUcontext ctx{}; cu_ok(cuInit(0), "cuInit"); cu_ok(cuDeviceGet(&dev,0), "cuDeviceGet");
    cu_ok(cuDevicePrimaryCtxSetFlags(dev, CU_CTX_SCHED_BLOCKING_SYNC), "cuDevicePrimaryCtxSetFlags");
    cu_ok(cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
    cu_ok(cuCtxSetCurrent(ctx), "cuCtxSetCurrent");
    CUstream stream{}; cu_ok(cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING), "cuStreamCreate");
    const auto wm_host = read_rgba(argv[3], ww, wh), sub_host = read_rgba(argv[8], sw, sh);
    Layer wm{.ptr=0, .w=ww, .h=wh, .x=wx, .y=wy, .opacity=0.75f};
    Layer sub{.ptr=0, .w=sw, .h=sh, .x=sx, .y=sy, .opacity=1.0f};
    cu_ok(cuMemAlloc(&wm.ptr, wm_host.size()), "cuMemAlloc(wm)"); cu_ok(cuMemAlloc(&sub.ptr, sub_host.size()), "cuMemAlloc(sub)");
    cu_ok(cuMemcpyHtoD(wm.ptr, wm_host.data(), wm_host.size()), "cuMemcpyHtoD(wm)"); cu_ok(cuMemcpyHtoD(sub.ptr, sub_host.data(), sub_host.size()), "cuMemcpyHtoD(sub)");

    struct GpuOverlayItemHost {
      const unsigned char* ptr;
      int w;
      int h;
      int x;
      int y;
      float opacity;
    };
    std::vector<GpuOverlayItemHost> layer_items = {
      {reinterpret_cast<const unsigned char*>(wm.ptr), wm.w, wm.h, wm.x, wm.y, wm.opacity},
      {reinterpret_cast<const unsigned char*>(sub.ptr), sub.w, sub.h, sub.x, sub.y, sub.opacity}
    };
    CUdeviceptr gpu_layers_table{};
    cu_ok(cuMemAlloc(&gpu_layers_table, layer_items.size() * sizeof(GpuOverlayItemHost)), "cuMemAlloc(layer_items)");
    cu_ok(cuMemcpyHtoD(gpu_layers_table, layer_items.data(), layer_items.size() * sizeof(GpuOverlayItemHost)), "cuMemcpyHtoD(layer_items)");
    int layer_count = static_cast<int>(layer_items.size());

    std::vector<char> ptx;
    const char* ptx_cache = std::getenv("CHRONON_CUDA_PTX_CACHE");
    if (ptx_cache && *ptx_cache) {
      std::ifstream cached(ptx_cache, std::ios::binary | std::ios::ate);
      if (cached) {
        const auto size = cached.tellg();
        if (size > 0) { ptx.resize(static_cast<std::size_t>(size)); cached.seekg(0); cached.read(ptx.data(), size); }
      }
    }
    if (ptx.empty()) {
      nvrtcProgram prog{}; nv_ok(nvrtcCreateProgram(&prog,kKernel,"composite.cu",0,nullptr,nullptr),"nvrtcCreateProgram");
      const char* opts[] = {"--gpu-architecture=compute_75"};
      const nvrtcResult compile_result = nvrtcCompileProgram(prog,1,opts);
      if (compile_result != NVRTC_SUCCESS) {
        size_t log_size{}; nvrtcGetProgramLogSize(prog,&log_size); std::string log(log_size,'\0');
        if (log_size) nvrtcGetProgramLog(prog,log.data());
        nvrtcDestroyProgram(&prog); fail(std::string("nvrtcCompileProgram: ") + nvrtcGetErrorString(compile_result) + "\n" + log);
      }
      size_t ptx_size{}; nv_ok(nvrtcGetPTXSize(prog,&ptx_size),"nvrtcGetPTXSize"); ptx.resize(ptx_size); nv_ok(nvrtcGetPTX(prog,ptx.data()),"nvrtcGetPTX"); nvrtcDestroyProgram(&prog);
      if (ptx_cache && *ptx_cache) { std::ofstream cached(ptx_cache, std::ios::binary | std::ios::trunc); if (cached) cached.write(ptx.data(), static_cast<std::streamsize>(ptx.size())); }
    }
    CUmodule mod{}; CUfunction kernel{}; cu_ok(cuModuleLoadData(&mod,ptx.data()),"cuModuleLoadData"); cu_ok(cuModuleGetFunction(&kernel,mod,"fused_nv12_composite"),"cuModuleGetFunction");

    AVFormatContext* in{}; av_ok(avformat_open_input(&in,input,nullptr,nullptr),"avformat_open_input"); av_ok(avformat_find_stream_info(in,nullptr),"avformat_find_stream_info");
    const int si = av_find_best_stream(in,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0); if(si<0) fail("video stream missing");
    const char* decoder_name = std::getenv("CHRONON_CUDA_DECODER");
    const AVCodec* dec = (decoder_name && *decoder_name) ? avcodec_find_decoder_by_name(decoder_name) : avcodec_find_decoder_by_name("h264_cuvid");
    if (!dec) dec = avcodec_find_decoder(in->streams[si]->codecpar->codec_id);
    if (!dec) fail("decoder missing");
    AVCodecContext* dc = avcodec_alloc_context3(dec); if(!dc) fail("decoder alloc"); av_ok(avcodec_parameters_to_context(dc,in->streams[si]->codecpar),"decoder parameters"); dc->thread_count = 1; dc->thread_type = 0;
    dc->get_format = hw_format;
    dc->extra_hw_frames = 8;
    AVBufferRef* hwdev{}; av_ok(av_hwdevice_ctx_create(&hwdev,AV_HWDEVICE_TYPE_CUDA,"0",nullptr,AV_CUDA_USE_PRIMARY_CONTEXT),"av_hwdevice_ctx_create"); dc->hw_device_ctx=av_buffer_ref(hwdev); av_ok(avcodec_open2(dc,dec,nullptr),"avcodec_open2");
    const int W=dc->width, H=dc->height; if(W<=0||H<=0) fail("invalid dimensions");

    AVFormatContext* out{}; avformat_alloc_output_context2(&out,nullptr,nullptr,output); if(!out) fail("output alloc");
    const AVCodec* enc_codec=avcodec_find_encoder_by_name("h264_nvenc"); if(!enc_codec) fail("h264_nvenc missing"); AVStream* st=avformat_new_stream(out,enc_codec); if(!st) fail("stream alloc");
    const int nvenc_surfaces = std::max(1, std::atoi(std::getenv("CHRONON_NVENC_SURFACES") ?: "2"));
    AVCodecContext* ec=avcodec_alloc_context3(enc_codec); if(!ec) fail("encoder alloc"); ec->width=W; ec->height=H; ec->time_base={1,24}; ec->framerate={24,1}; ec->pix_fmt=AV_PIX_FMT_CUDA; ec->bit_rate=0; ec->thread_count = 1; ec->thread_type = 0; ec->max_b_frames = 0; ec->flags |= AV_CODEC_FLAG_LOW_DELAY; av_opt_set(ec->priv_data,"preset","p1",0); av_opt_set_int(ec->priv_data,"qp",23,0); av_opt_set_int(ec->priv_data,"surfaces",nvenc_surfaces,0); av_opt_set_int(ec->priv_data,"zerolatency",1,0); av_opt_set_int(ec->priv_data,"b_adapt",0,0);
    AVBufferRef* frames=av_hwframe_ctx_alloc(hwdev); if(!frames) fail("frames alloc"); auto* fctx=(AVHWFramesContext*)frames->data; fctx->format=AV_PIX_FMT_CUDA; fctx->sw_format=AV_PIX_FMT_NV12; fctx->width=W; fctx->height=H; fctx->initial_pool_size=16; av_ok(av_hwframe_ctx_init(frames),"av_hwframe_ctx_init"); ec->hw_frames_ctx=av_buffer_ref(frames); av_ok(avcodec_open2(ec,enc_codec,nullptr),"encoder open"); av_ok(avcodec_parameters_from_context(st->codecpar,ec),"codec parameters"); st->time_base=ec->time_base;
    if(!(out->oformat->flags&AVFMT_NOFILE)) av_ok(avio_open(&out->pb,output,AVIO_FLAG_WRITE),"avio_open"); av_ok(avformat_write_header(out,nullptr),"avformat_write_header");

    const size_t kRingSlots = 16;
    FrameSlotRing slot_ring(kRingSlots, frames, W, H);

    struct QueueItem {
      FrameSlot* slot{nullptr};
      uint64_t frame_id{0};
      bool is_flush{false};
    };

    std::queue<QueueItem> encode_q;
    std::mutex q_mtx;
    std::condition_variable q_cv;
    uint64_t encoded_packet_count = 0;

    std::thread drain_thread([&]() {
      AVPacket* local_pkt = av_packet_alloc();
      while (true) {
        QueueItem item{};
        {
          std::unique_lock<std::mutex> lock(q_mtx);
          q_cv.wait(lock, [&]() { return !encode_q.empty(); });
          item = encode_q.front();
          encode_q.pop();
        }
        if (item.is_flush) {
          write_packets(ec, out, local_pkt, nullptr, encoded_packet_count);
          break;
        }

        auto t_ev0 = Clock::now();
        cuEventSynchronize(item.slot->gpu_ready_ev);
        auto t_ev1 = Clock::now();
        g_timings.gpu_event_wait_ms += std::chrono::duration<double, std::milli>(t_ev1 - t_ev0).count();

        float gpu_item_ms = 0.0f;
        cuEventElapsedTime(&gpu_item_ms, item.slot->gpu_start_timing_ev, item.slot->gpu_end_timing_ev);
        g_timings.gpu_execute_ms += gpu_item_ms;

        item.slot->frame->pts = static_cast<int64_t>(item.frame_id);
        write_packets(ec, out, local_pkt, item.slot->frame, encoded_packet_count);

        // Explicit handshake: release slot back to producer ring
        slot_ring.release_slot(item.slot);
      }
      av_packet_free(&local_pkt);
    });

    auto startup_end = Clock::now();
    g_timings.startup_ms = std::chrono::duration<double, std::milli>(startup_end - startup_start).count();

    // WARM PIPELINE START
    auto pipeline_start = Clock::now();

    uint64_t submitted_frame_id = 0;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* decoded = av_frame_alloc();
    if (!pkt || !decoded) fail("frame/packet alloc");

    auto process = [&](AVFrame* src) {
      FrameSlot* slot = slot_ring.acquire_free_slot();
      auto t_k0 = Clock::now();

      int dst_yp = slot->frame->linesize[0], dst_uvp = slot->frame->linesize[1];
      int src_yp = src->linesize[0], src_uvp = src->linesize[1];
      int kernel_w = W, kernel_h = H;
      void* args[] = {
        &slot->frame->data[0], &slot->frame->data[1], &dst_yp, &dst_uvp,
        &src->data[0], &src->data[1], &src_yp, &src_uvp,
        &gpu_layers_table, &layer_count,
        &kernel_w, &kernel_h
      };

      cu_ok(cuEventRecord(slot->gpu_start_timing_ev, stream), "cuEventRecord start");
      cu_ok(cuLaunchKernel(kernel, (W+31)/32, (H+31)/32, 1, 32, 16, 1, 0, stream, args, nullptr), "cuLaunchKernel fused");
      cu_ok(cuEventRecord(slot->gpu_end_timing_ev, stream), "cuEventRecord end");
      cu_ok(cuEventRecord(slot->gpu_ready_ev, stream), "cuEventRecord ready");

      auto t_k1 = Clock::now();
      g_timings.gpu_submit_ms += std::chrono::duration<double, std::milli>(t_k1 - t_k0).count();

      const uint64_t cur_frame_id = submitted_frame_id++;
      slot_ring.mark_ready(slot);

      {
        std::lock_guard<std::mutex> lock(q_mtx);
        encode_q.push({slot, cur_frame_id, false});
        if (encode_q.size() > g_timings.queue_high_watermark) {
          g_timings.queue_high_watermark = encode_q.size();
        }
      }
      q_cv.notify_one();
    };

    while (true) {
      auto t_dm0 = Clock::now();
      int r = av_read_frame(in, pkt);
      auto t_dm1 = Clock::now();
      g_timings.demux_ms += std::chrono::duration<double, std::milli>(t_dm1 - t_dm0).count();
      if (r < 0) break;

      if (pkt->stream_index == si) {
        auto t_dec_send0 = Clock::now();
        av_ok(avcodec_send_packet(dc, pkt), "avcodec_send_packet");
        auto t_dec_send1 = Clock::now();
        g_timings.decode_send_ms += std::chrono::duration<double, std::milli>(t_dec_send1 - t_dec_send0).count();

        while (true) {
          auto t_dec_rec0 = Clock::now();
          int rec_r = avcodec_receive_frame(dc, decoded);
          auto t_dec_rec1 = Clock::now();
          g_timings.decode_receive_ms += std::chrono::duration<double, std::milli>(t_dec_rec1 - t_dec_rec0).count();
          if (rec_r < 0) break;
          process(decoded);
        }
      }
      av_packet_unref(pkt);
    }

    auto t_flush0 = Clock::now();
    avcodec_send_packet(dc, nullptr);
    while (true) {
      auto t_dec_rec0 = Clock::now();
      int rec_r = avcodec_receive_frame(dc, decoded);
      auto t_dec_rec1 = Clock::now();
      g_timings.decode_receive_ms += std::chrono::duration<double, std::milli>(t_dec_rec1 - t_dec_rec0).count();
      if (rec_r < 0) break;
      process(decoded);
    }
    auto t_flush1 = Clock::now();
    g_timings.decode_send_ms += std::chrono::duration<double, std::milli>(t_flush1 - t_flush0).count();

    {
      std::lock_guard<std::mutex> lock(q_mtx);
      encode_q.push({nullptr, 0, true});
    }
    q_cv.notify_one();
    if (drain_thread.joinable()) drain_thread.join();

    auto t_tr0 = Clock::now();
    av_write_trailer(out);
    auto t_tr1 = Clock::now();
    g_timings.trailer_ms = std::chrono::duration<double, std::milli>(t_tr1 - t_tr0).count();

    auto pipeline_end = Clock::now();
    g_timings.pipeline_wall_ms = std::chrono::duration<double, std::milli>(pipeline_end - pipeline_start).count();

    // TEARDOWN START
    auto teardown_start = Clock::now();
    if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
    av_frame_free(&decoded);
    av_packet_free(&pkt);
    avcodec_free_context(&ec);
    avcodec_free_context(&dc);
    avformat_close_input(&in);
    avformat_free_context(out);
    av_buffer_unref(&frames);
    av_buffer_unref(&hwdev);
    cuMemFree(wm.ptr);
    cuMemFree(sub.ptr);
    cuMemFree(gpu_layers_table);
    cuModuleUnload(mod);
    cuStreamDestroy(stream);
    cuDevicePrimaryCtxRelease(dev);
    auto teardown_end = Clock::now();
    g_timings.teardown_ms = std::chrono::duration<double, std::milli>(teardown_end - teardown_start).count();

    auto process_end = Clock::now();
    g_timings.process_wall_ms = std::chrono::duration<double, std::milli>(process_end - process_start).count();

    const double frames_n = static_cast<double>(submitted_frame_id);
    std::cout << "\n================ COMPLETE BENCHMARK TIMING BREAKDOWN (" << submitted_frame_id << " frames) ================\n"
              << "  [1] Startup & Context Init:   " << g_timings.startup_ms << " ms\n"
              << "  [2] Demux (av_read_frame):    " << g_timings.demux_ms << " ms  (" << (g_timings.demux_ms / frames_n) << " ms/f)\n"
              << "  [3] Decode Send Packet:       " << g_timings.decode_send_ms << " ms  (" << (g_timings.decode_send_ms / frames_n) << " ms/f)\n"
              << "  [4] Decode Receive Frame:     " << g_timings.decode_receive_ms << " ms  (" << (g_timings.decode_receive_ms / frames_n) << " ms/f)\n"
              << "  [5] GPU Kernel CPU Submit:    " << g_timings.gpu_submit_ms << " ms  (" << (g_timings.gpu_submit_ms / frames_n) << " ms/f)\n"
              << "  [6] Pure GPU Execution (Ev):  " << g_timings.gpu_execute_ms << " ms  (" << (g_timings.gpu_execute_ms / frames_n) << " ms/f)\n"
              << "  [7] GPU Event Wait (Drain):   " << g_timings.gpu_event_wait_ms << " ms  (" << (g_timings.gpu_event_wait_ms / frames_n) << " ms/f)\n"
              << "  [8] Encode Send Frame:        " << g_timings.encode_send_ms << " ms  (" << (g_timings.encode_send_ms / frames_n) << " ms/f)\n"
              << "  [9] Encode Receive Packet:    " << g_timings.encode_receive_ms << " ms  (" << (g_timings.encode_receive_ms / frames_n) << " ms/f)\n"
              << " [10] Mux Write Interleaved:    " << g_timings.mux_write_ms << " ms  (" << (g_timings.mux_write_ms / frames_n) << " ms/f)\n"
              << " [11] Queue Backpressure Wait:  " << g_timings.queue_wait_ms << " ms\n"
              << " [12] Trailer Write:            " << g_timings.trailer_ms << " ms\n"
              << " [13] Teardown & Free:          " << g_timings.teardown_ms << " ms\n"
              << " ---------------------------------------------------------------------------------\n"
              << "  >>> WARM PIPELINE WALL TIME:  " << g_timings.pipeline_wall_ms << " ms  ==> " << (1000.0 * frames_n / g_timings.pipeline_wall_ms) << " FPS\n"
              << "  >>> COLD PROCESS WALL TIME:   " << g_timings.process_wall_ms << " ms  ==> " << (1000.0 * frames_n / g_timings.process_wall_ms) << " FPS\n"
              << "  >>> Queue High-Watermark:     " << g_timings.queue_high_watermark << " / " << kRingSlots << " slots\n"
              << "==================================================================================\n\n";

    std::cout << "CUDA_NVDEC_NVENC_OVERLAY_PASS decoder=" << dec->name << " surfaces=" << nvenc_surfaces << " output=" << output << " frames=" << submitted_frame_id << "\n";
    return 0;
  } catch(const std::exception& e){ std::cerr << "CUDA_NVDEC_NVENC_OVERLAY_FAIL: " << e.what() << "\n"; return 1; }
}


