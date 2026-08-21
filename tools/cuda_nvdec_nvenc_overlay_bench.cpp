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
  double demux_decode_ms{0.0};
  double copy_ms{0.0};
  double composite_kernel_ms{0.0};
  double encode_send_ms{0.0};
  double mux_write_ms{0.0};
  double cuda_sync_ms{0.0};
  double total_wall_ms{0.0};
};

BenchTimings g_timings;

using Clock = std::chrono::high_resolution_clock;

void write_packets(AVCodecContext* enc, AVFormatContext* out, AVPacket* pkt, AVFrame* frame, int64_t& count) {
  auto t0 = Clock::now();
  av_ok(avcodec_send_frame(enc, frame), "avcodec_send_frame");
  auto t1 = Clock::now();
  g_timings.encode_send_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();

  for (;;) {
    auto t_rec0 = Clock::now();
    int r = avcodec_receive_packet(enc, pkt);
    auto t_rec1 = Clock::now();
    g_timings.encode_send_ms += std::chrono::duration<double, std::milli>(t_rec1 - t_rec0).count();

    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
    av_ok(r, "avcodec_receive_packet");
    pkt->stream_index = 0;
    pkt->pts = pkt->dts = count++;
    pkt->duration = 1;
    av_packet_rescale_ts(pkt, enc->time_base, out->streams[0]->time_base);

    auto t_mux0 = Clock::now();
    av_ok(av_interleaved_write_frame(out, pkt), "av_interleaved_write_frame");
    auto t_mux1 = Clock::now();
    g_timings.mux_write_ms += std::chrono::duration<double, std::milli>(t_mux1 - t_mux0).count();

    av_packet_unref(pkt);
  }
}

} // namespace

int main(int argc, char** argv) {
  av_log_set_level(AV_LOG_ERROR);
  if (argc != 13) {
    std::fprintf(stderr, "usage: %s input.mp4 output.mp4 wm.rgba wm_w wm_h wm_x wm_y sub.rgba sub_w sub_h sub_x sub_y\n", argv[0]);
    return 2;
  }
  try {
    auto total_start = Clock::now();
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
#ifdef CHRONON3D_ENABLE_RAW_NVENC
    if (std::getenv("CHRONON_NVENC_RAW")) {
      AVFormatContext* raw_out{}; avformat_alloc_output_context2(&raw_out,nullptr,nullptr,output); if(!raw_out) fail("raw output alloc"); RawNvenc raw(ctx,W,H,raw_out);
      if(!(raw_out->oformat->flags&AVFMT_NOFILE)) av_ok(avio_open(&raw_out->pb,output,AVIO_FLAG_WRITE),"raw avio_open"); av_ok(avformat_write_header(raw_out,nullptr),"raw avformat_write_header");
      CUdeviceptr raw_frame{}; size_t raw_pitch{}; cu_ok(cuMemAllocPitch(&raw_frame,&raw_pitch,W,H+H/2,16),"cuMemAllocPitch(raw frame)"); raw.register_buffer(raw_frame,static_cast<uint32_t>(raw_pitch));
      AVPacket* raw_pkt=av_packet_alloc(); AVFrame* raw_decoded=av_frame_alloc(); if(!raw_pkt || !raw_decoded) fail("raw frame/packet alloc"); int64_t raw_count=0;
      raw.set_stream(stream);
      auto raw_process=[&](AVFrame* src){ CUdeviceptr raw_y=raw_frame, raw_uv=raw_frame+raw_pitch*H; copy_plane(raw_y,static_cast<int>(raw_pitch),src->data[0],src->linesize[0],W,H,stream); copy_plane(raw_uv,static_cast<int>(raw_pitch),src->data[1],src->linesize[1],W,(H+1)/2,stream); float wop=0.75f; int kernel_w=W,kernel_h=H; void* args[]={&raw_y,&raw_uv,&raw_pitch,&raw_pitch,&wm.ptr,&wm.w,&wm.h,&wm.x,&wm.y,&wop,&sub.ptr,&sub.w,&sub.h,&sub.x,&sub.y,&kernel_w,&kernel_h}; cu_ok(cuLaunchKernel(kernel,(W+31)/32,(H+31)/32,1,32,16,1,0,stream,args,nullptr),"raw cuLaunchKernel"); raw.encode(raw_count++,static_cast<uint32_t>(raw_pitch)); };
      while(av_read_frame(in,raw_pkt)>=0){ if(raw_pkt->stream_index==si){ av_ok(avcodec_send_packet(dc,raw_pkt),"raw avcodec_send_packet"); while(avcodec_receive_frame(dc,raw_decoded)>=0) raw_process(raw_decoded); } av_packet_unref(raw_pkt); }
      avcodec_send_packet(dc,nullptr); while(avcodec_receive_frame(dc,raw_decoded)>=0) raw_process(raw_decoded); av_write_trailer(raw_out); raw.shutdown(); if(!(raw_out->oformat->flags&AVFMT_NOFILE)) avio_closep(&raw_out->pb); av_frame_free(&raw_decoded); av_packet_free(&raw_pkt); avformat_close_input(&in); avformat_free_context(raw_out); cuMemFree(raw_frame); avcodec_free_context(&dc); av_buffer_unref(&hwdev); cuMemFree(wm.ptr); cuMemFree(sub.ptr); cuModuleUnload(mod); cuStreamDestroy(stream); cuDevicePrimaryCtxRelease(dev); std::cout << "CUDA_NVDEC_RAW_NVENC_OVERLAY_PASS decoder=" << dec->name << " output=" << output << " frames=" << raw_count << "\n"; return 0;
    }
#endif
#ifndef CHRONON3D_ENABLE_RAW_NVENC
    if (std::getenv("CHRONON_NVENC_RAW")) fail("raw NVENC requested but this binary was built without nvEncodeAPI");
#endif
    AVFormatContext* out{}; avformat_alloc_output_context2(&out,nullptr,nullptr,output); if(!out) fail("output alloc");
    const AVCodec* enc_codec=avcodec_find_encoder_by_name("h264_nvenc"); if(!enc_codec) fail("h264_nvenc missing"); AVStream* st=avformat_new_stream(out,enc_codec); if(!st) fail("stream alloc");
    const int nvenc_surfaces = std::max(1, std::atoi(std::getenv("CHRONON_NVENC_SURFACES") ?: "2"));
    AVCodecContext* ec=avcodec_alloc_context3(enc_codec); if(!ec) fail("encoder alloc"); ec->width=W; ec->height=H; ec->time_base={1,24}; ec->framerate={24,1}; ec->pix_fmt=AV_PIX_FMT_CUDA; ec->bit_rate=0; ec->thread_count = 1; ec->thread_type = 0; ec->max_b_frames = 0; ec->flags |= AV_CODEC_FLAG_LOW_DELAY; av_opt_set(ec->priv_data,"preset","p1",0); av_opt_set_int(ec->priv_data,"qp",23,0); av_opt_set_int(ec->priv_data,"surfaces",nvenc_surfaces,0); av_opt_set_int(ec->priv_data,"zerolatency",1,0); av_opt_set_int(ec->priv_data,"b_adapt",0,0);
    AVBufferRef* frames=av_hwframe_ctx_alloc(hwdev); if(!frames) fail("frames alloc"); auto* fctx=(AVHWFramesContext*)frames->data; fctx->format=AV_PIX_FMT_CUDA; fctx->sw_format=AV_PIX_FMT_NV12; fctx->width=W; fctx->height=H; fctx->initial_pool_size=8; av_ok(av_hwframe_ctx_init(frames),"av_hwframe_ctx_init"); ec->hw_frames_ctx=av_buffer_ref(frames); av_ok(avcodec_open2(ec,enc_codec,nullptr),"encoder open"); av_ok(avcodec_parameters_from_context(st->codecpar,ec),"codec parameters"); st->time_base=ec->time_base;
    if(!(out->oformat->flags&AVFMT_NOFILE)) av_ok(avio_open(&out->pb,output,AVIO_FLAG_WRITE),"avio_open"); av_ok(avformat_write_header(out,nullptr),"avformat_write_header");
    AVPacket* pkt=av_packet_alloc(); AVFrame* decoded=av_frame_alloc(); if(!pkt || !decoded) fail("frame/packet alloc"); int64_t out_count=0;
    std::vector<AVFrame*> dst_pool(16, nullptr);
    std::vector<CUevent> gpu_events(16, nullptr);
    for (size_t i = 0; i < dst_pool.size(); ++i) {
      dst_pool[i] = av_frame_alloc();
      if (!dst_pool[i]) fail("frame alloc");
      dst_pool[i]->format = AV_PIX_FMT_CUDA;
      dst_pool[i]->width = W;
      dst_pool[i]->height = H;
      dst_pool[i]->hw_frames_ctx = av_buffer_ref(frames);
      av_ok(av_hwframe_get_buffer(frames, dst_pool[i], 0), "av_hwframe_get_buffer");
      cu_ok(cuEventCreate(&gpu_events[i], CU_EVENT_DISABLE_TIMING), "cuEventCreate");
    }

    struct EncodeItem {
      AVFrame* frame{nullptr};
      CUevent event{nullptr};
      int64_t pts{-1};
      bool is_flush{false};
    };

    std::queue<EncodeItem> encode_q;
    std::mutex q_mtx;
    std::condition_variable q_cv;
    std::atomic<bool> drain_done{false};

    std::thread drain_thread([&]() {
      AVPacket* local_pkt = av_packet_alloc();
      while (true) {
        EncodeItem item{};
        {
          std::unique_lock<std::mutex> lock(q_mtx);
          q_cv.wait(lock, [&]() { return !encode_q.empty(); });
          item = encode_q.front();
          encode_q.pop();
        }
        if (item.is_flush) {
          write_packets(ec, out, local_pkt, nullptr, out_count);
          break;
        }
        if (item.event) {
          cuEventSynchronize(item.event);
        }
        item.frame->pts = item.pts;
        write_packets(ec, out, local_pkt, item.frame, out_count);
      }
      av_packet_free(&local_pkt);
      drain_done = true;
    });

    size_t pool_idx = 0;
    auto process = [&](AVFrame* src) {
      size_t slot = pool_idx % dst_pool.size();
      AVFrame* dst = dst_pool[slot];
      CUevent ev = gpu_events[slot];
      pool_idx++;
      auto t_k0 = Clock::now();
      int dst_yp = dst->linesize[0], dst_uvp = dst->linesize[1];
      int src_yp = src->linesize[0], src_uvp = src->linesize[1];
      int kernel_w = W, kernel_h = H;
      void* args[] = {
        &dst->data[0], &dst->data[1], &dst_yp, &dst_uvp,
        &src->data[0], &src->data[1], &src_yp, &src_uvp,
        &gpu_layers_table, &layer_count,
        &kernel_w, &kernel_h
      };
      cu_ok(cuLaunchKernel(kernel, (W+31)/32, (H+31)/32, 1, 32, 16, 1, 0, stream, args, nullptr), "cuLaunchKernel fused");
      cu_ok(cuEventRecord(ev, stream), "cuEventRecord");
      auto t_k1 = Clock::now();
      g_timings.composite_kernel_ms += std::chrono::duration<double, std::milli>(t_k1 - t_k0).count();

      int64_t current_pts = out_count;
      {
        std::lock_guard<std::mutex> lock(q_mtx);
        encode_q.push({dst, ev, current_pts, false});
      }
      q_cv.notify_one();
    };

    while (true) {
      auto t_dm0 = Clock::now();
      int r = av_read_frame(in, pkt);
      auto t_dm1 = Clock::now();
      g_timings.demux_decode_ms += std::chrono::duration<double, std::milli>(t_dm1 - t_dm0).count();
      if (r < 0) break;

      if (pkt->stream_index == si) {
        auto t_dec0 = Clock::now();
        av_ok(avcodec_send_packet(dc, pkt), "avcodec_send_packet");
        while (avcodec_receive_frame(dc, decoded) >= 0) {
          auto t_dec1 = Clock::now();
          g_timings.demux_decode_ms += std::chrono::duration<double, std::milli>(t_dec1 - t_dec0).count();
          process(decoded);
          t_dec0 = Clock::now();
        }
        auto t_dec2 = Clock::now();
        g_timings.demux_decode_ms += std::chrono::duration<double, std::milli>(t_dec2 - t_dec0).count();
      }
      av_packet_unref(pkt);
    }

    auto t_flush0 = Clock::now();
    avcodec_send_packet(dc, nullptr);
    while (avcodec_receive_frame(dc, decoded) >= 0) {
      auto t_flush1 = Clock::now();
      g_timings.demux_decode_ms += std::chrono::duration<double, std::milli>(t_flush1 - t_flush0).count();
      process(decoded);
      t_flush0 = Clock::now();
    }

    auto t_sync0 = Clock::now();
    cu_ok(cuStreamSynchronize(stream), "cuStreamSynchronize final");
    auto t_sync1 = Clock::now();
    g_timings.cuda_sync_ms += std::chrono::duration<double, std::milli>(t_sync1 - t_sync0).count();

    {
      std::lock_guard<std::mutex> lock(q_mtx);
      encode_q.push({nullptr, nullptr, -1, true});
    }
    q_cv.notify_one();
    if (drain_thread.joinable()) drain_thread.join();

    auto t_tr0 = Clock::now();
    av_write_trailer(out);
    auto t_tr1 = Clock::now();
    g_timings.mux_write_ms += std::chrono::duration<double, std::milli>(t_tr1 - t_tr0).count();

    for (auto& dst : dst_pool) av_frame_free(&dst);
    for (auto& ev : gpu_events) if (ev) cuEventDestroy(ev);
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
    cuModuleUnload(mod);
    cuStreamDestroy(stream);
    cuDevicePrimaryCtxRelease(dev);

    auto total_end = Clock::now();
    g_timings.total_wall_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();

    std::cout << "\n================ BENCHMARK TIMING BREAKDOWN (408 frames) ================\n"
              << "  demux_decode_ms:       " << g_timings.demux_decode_ms << " ms  (" << (g_timings.demux_decode_ms / out_count) << " ms/f)\n"
              << "  copy_ms:               " << g_timings.copy_ms << " ms  (" << (g_timings.copy_ms / out_count) << " ms/f)\n"
              << "  composite_kernel_ms:   " << g_timings.composite_kernel_ms << " ms  (" << (g_timings.composite_kernel_ms / out_count) << " ms/f)\n"
              << "  encode_send_ms:        " << g_timings.encode_send_ms << " ms  (" << (g_timings.encode_send_ms / out_count) << " ms/f)\n"
              << "  mux_write_ms:          " << g_timings.mux_write_ms << " ms  (" << (g_timings.mux_write_ms / out_count) << " ms/f)\n"
              << "  cuda_sync_ms:          " << g_timings.cuda_sync_ms << " ms\n"
              << "  total_wall_ms:         " << g_timings.total_wall_ms << " ms  (" << (1000.0 * out_count / g_timings.total_wall_ms) << " FPS)\n"
              << "=========================================================================\n\n";

    std::cout << "CUDA_NVDEC_NVENC_OVERLAY_PASS decoder=" << dec->name << " surfaces=" << nvenc_surfaces << " output=" << output << " frames=" << out_count << "\n";
    return 0;
  } catch(const std::exception& e){ std::cerr << "CUDA_NVDEC_NVENC_OVERLAY_FAIL: " << e.what() << "\n"; return 1; }
}

