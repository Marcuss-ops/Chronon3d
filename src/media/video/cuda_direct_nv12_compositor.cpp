#include <chronon3d/media/video/cuda_direct_nv12_compositor.hpp>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#ifndef CHRONON3D_NVRTC_ARCHITECTURE
#define CHRONON3D_NVRTC_ARCHITECTURE "compute_75"
#endif

#include <nvrtc.h>
#include <chronon3d/core/profiling/profiling.hpp>

#include <chrono>
#include <dlfcn.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace chronon3d::media {

struct alignas(8) DirectYuvLayerHost {
    int kind{0};
    CUdeviceptr rgba{0};
    CUdeviceptr y{0};
    CUdeviceptr uv{0};
    int pitch{0};
    int uv_pitch{0};
    int source_width{0};
    int source_height{0};
    float dst_x0{0.0f}, dst_y0{0.0f}, dst_x1{0.0f}, dst_y1{0.0f};
    float src_x0{0.0f}, src_y0{0.0f}, src_x1{1.0f}, src_y1{1.0f};
    float opacity{1.0f};
    int blend_mode{0};
    float corner_radius{0.0f};
    float _pad{0.0f};
};
static_assert(sizeof(DirectYuvLayerHost) == 96);

constexpr const char* kDirectKernel = R"CUDA(
struct DirectYuvLayer {
  int kind;
  unsigned long long rgba;
  unsigned long long y;
  unsigned long long uv;
  int pitch;
  int uv_pitch;
  int source_width;
  int source_height;
  float dst_x0, dst_y0, dst_x1, dst_y1;
  float src_x0, src_y0, src_x1, src_y1;
  float opacity;
  int blend_mode;
  float corner_radius;
  float _pad;
};

__device__ float4 direct_layer_load(const DirectYuvLayer& layer,
                                    int x, int y, int width, int height) {
  if (x < 0 || y < 0 || x >= width || y >= height ||
      layer.source_width <= 0 || layer.source_height <= 0) {
    return make_float4(0, 0, 0, 0);
  }
  const float u = layer.src_x0 + (layer.src_x1 - layer.src_x0) *
      ((float)x - layer.dst_x0) / fmaxf(layer.dst_x1 - layer.dst_x0, 1.0f);
  const float v = layer.src_y0 + (layer.src_y1 - layer.src_y0) *
      ((float)y - layer.dst_y0) / fmaxf(layer.dst_y1 - layer.dst_y0, 1.0f);
  const int sx = min(max((int)floorf(u * (float)layer.source_width), 0), layer.source_width - 1);
  const int sy = min(max((int)floorf(v * (float)layer.source_height), 0), layer.source_height - 1);
  if (layer.kind == 1) {
    if (layer.y == 0 || layer.uv == 0 || layer.pitch <= 0 || layer.uv_pitch <= 0) {
      return make_float4(0, 0, 0, 0);
    }
    const unsigned char* yp = (const unsigned char*)layer.y;
    const unsigned char* uvp = (const unsigned char*)layer.uv;
    const float yf = ((float)yp[sy * layer.pitch + sx] - 16.0f) / 219.0f;
    const int uvx = sx & ~1;
    const int uvrow = (sy >> 1) * layer.uv_pitch;
    const float u8 = (float)uvp[uvrow + uvx] - 128.0f;
    const float v8 = (float)uvp[uvrow + uvx + 1] - 128.0f;
    const float r = yf + 1.402f * v8 / 255.0f;
    const float g = yf - 0.344136f * u8 / 255.0f - 0.714136f * v8 / 255.0f;
    const float b = yf + 1.772f * u8 / 255.0f;
    return make_float4(fminf(fmaxf(r, 0.0f), 1.0f),
                       fminf(fmaxf(g, 0.0f), 1.0f),
                       fminf(fmaxf(b, 0.0f), 1.0f), 1.0f);
  }
  if (layer.rgba == 0 || layer.pitch <= 0) return make_float4(0, 0, 0, 0);
  const char* row = (const char*)layer.rgba + sy * layer.pitch;
  return ((const float4*)row)[sx];
}

__device__ float4 direct_layer_pixel(const DirectYuvLayer* layers, int count,
                                     int x, int y, int width, int height) {
  float4 accum = make_float4(0, 0, 0, 0);
  for (int i = 0; i < count; ++i) {
    const DirectYuvLayer layer = layers[i];
    if (x < layer.dst_x0 || x >= layer.dst_x1 ||
        y < layer.dst_y0 || y >= layer.dst_y1) continue;
    float4 src = direct_layer_load(layer, x, y, width, height);
    float alpha = src.w * layer.opacity;

    if (layer.corner_radius > 0.0f) {
      const float half_w = (layer.dst_x1 - layer.dst_x0) * 0.5f;
      const float half_h = (layer.dst_y1 - layer.dst_y0) * 0.5f;
      const float cx = (layer.dst_x0 + layer.dst_x1) * 0.5f;
      const float cy = (layer.dst_y0 + layer.dst_y1) * 0.5f;
      const float r = fminf(layer.corner_radius, fminf(half_w, half_h));
      const float dx = fabsf((float)x + 0.5f - cx) - (half_w - r);
      const float dy = fabsf((float)y + 0.5f - cy) - (half_h - r);
      const float ax = fmaxf(dx, 0.0f);
      const float ay = fmaxf(dy, 0.0f);
      const float dist = sqrtf(ax * ax + ay * ay) + fminf(fmaxf(dx, dy), 0.0f) - r;
      const float clip = fminf(fmaxf(0.5f - dist, 0.0f), 1.0f);
      alpha *= clip;
    }

    src.w = fminf(fmaxf(alpha, 0.0f), 1.0f);
    if (src.w <= 0.0f) continue;

    if (layer.blend_mode == 1) {
      accum.x += src.x * src.w;
      accum.y += src.y * src.w;
      accum.z += src.z * src.w;
      accum.w = fminf(accum.w + src.w, 1.0f);
    } else {
      accum.x = src.x * src.w + accum.x * (1.0f - src.w);
      accum.y = src.y * src.w + accum.y * (1.0f - src.w);
      accum.z = src.z * src.w + accum.z * (1.0f - src.w);
      accum.w = src.w + accum.w * (1.0f - src.w);
    }
  }
  return accum;
}

// Direct GpuLayerBatch -> NV12/P010 precursor. One invocation owns a 2x2
// block, so luma and chroma observe exactly the same ordered layer samples.
extern "C" __global__ void nv12_composite_layer_batch_2x2(
    const unsigned char* bg_y, int bg_yp, const unsigned char* bg_uv, int bg_uvp,
    unsigned char* out_y, int out_yp, unsigned char* out_uv, int out_uvp,
    const DirectYuvLayer* layers, int layer_count, int width, int height) {
  const int x = ((int)blockIdx.x * blockDim.x + threadIdx.x) * 2;
  const int y = ((int)blockIdx.y * blockDim.y + threadIdx.y) * 2;
  if (x >= width || y >= height) return;
  const int x1 = min(x + 1, width - 1);
  const int y1 = min(y + 1, height - 1);
  const float4 f00 = direct_layer_pixel(layers, layer_count, x, y, width, height);
  const float4 f10 = direct_layer_pixel(layers, layer_count, x1, y, width, height);
  const float4 f01 = direct_layer_pixel(layers, layer_count, x, y1, width, height);
  const float4 f11 = direct_layer_pixel(layers, layer_count, x1, y1, width, height);
  const float4 fs[4] = {f00, f10, f01, f11};
  const int xs[4] = {x, x1, x, x1};
  const int ys[4] = {y, y, y1, y1};
  for (int i = 0; i < 4; ++i) {
    const float y_fg = 255.0f *
        (0.2126f * fs[i].x + 0.7152f * fs[i].y + 0.0722f * fs[i].z);
    const float bg = (float)bg_y[ys[i] * bg_yp + xs[i]];
    out_y[ys[i] * out_yp + xs[i]] = (unsigned char)fminf(fmaxf(roundf(
        (1.0f - fs[i].w) * bg + fs[i].w * y_fg), 0.0f), 255.0f);
  }
  const float a = (f00.w + f10.w + f01.w + f11.w) * 0.25f;
  const int uv = (y >> 1) * out_uvp + x;
  const int bg_uv_index = (y >> 1) * bg_uvp + x;
  if (a <= 0.0f) {
    out_uv[uv] = bg_uv[bg_uv_index];
    out_uv[uv + 1] = bg_uv[bg_uv_index + 1];
    return;
  }
  const float r = (f00.x*f00.w + f10.x*f10.w + f01.x*f01.w + f11.x*f11.w) /
      fmaxf(f00.w + f10.w + f01.w + f11.w, 1e-6f);
  const float g = (f00.y*f00.w + f10.y*f10.w + f01.y*f01.w + f11.y*f11.w) /
      fmaxf(f00.w + f10.w + f01.w + f11.w, 1e-6f);
  const float b = (f00.z*f00.w + f10.z*f10.w + f01.z*f01.w + f11.z*f11.w) /
      fmaxf(f00.w + f10.w + f01.w + f11.w, 1e-6f);
  const float u = 128.0f + 255.0f * (-0.1146f*r - 0.3854f*g + 0.5000f*b);
  const float v = 128.0f + 255.0f * ( 0.5000f*r - 0.4542f*g - 0.0458f*b);
  out_uv[uv] = (unsigned char)fminf(fmaxf(roundf((1.0f-a)*bg_uv[bg_uv_index] + a*u), 0.0f), 255.0f);
  out_uv[uv+1] = (unsigned char)fminf(fmaxf(roundf((1.0f-a)*bg_uv[bg_uv_index+1] + a*v), 0.0f), 255.0f);
}
)CUDA";

[[noreturn]] void fail(const char* operation, const std::string& detail) {
    throw std::runtime_error(std::string(operation) + ": " + detail);
}

void check_cuda(CUresult result, const char* operation) {
    if (result == CUDA_SUCCESS) return;
    const char* name = nullptr;
    const char* description = nullptr;
    cuGetErrorName(result, &name);
    cuGetErrorString(result, &description);
    std::string detail = name ? name : "CUDA error";
    if (description && description != detail) {
        detail += " (";
        detail += description;
        detail += ")";
    }
    fail(operation, detail);
}

void check_nvrtc(nvrtcResult result, const char* operation) {
    if (result == NVRTC_SUCCESS) return;
    fail(operation, nvrtcGetErrorString(result));
}

void ensure_nvrtc_builtins_loaded() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&nvrtcCompileProgram), &info) == 0 ||
        !info.dli_fname) return;
    const auto directory = std::filesystem::path(info.dli_fname).parent_path();
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file(error)) continue;
        const auto filename = entry.path().filename().string();
        if (filename.rfind("libnvrtc-builtins.so", 0) != 0) continue;
        (void)dlopen(entry.path().c_str(), RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
        break;
    }
}

std::string get_compiled_direct_ptx() {
    static std::mutex ptx_mutex;
    static std::string cached_ptx;
    std::lock_guard<std::mutex> lock(ptx_mutex);
    if (!cached_ptx.empty()) {
        return cached_ptx;
    }
    ensure_nvrtc_builtins_loaded();

    nvrtcProgram program{};
    check_nvrtc(nvrtcCreateProgram(&program, kDirectKernel, "chronon_direct_nv12.cu",
                                   0, nullptr, nullptr), "nvrtcCreateProgram");
    const char* options[] = {
        "--gpu-architecture=" CHRONON3D_NVRTC_ARCHITECTURE};
    const nvrtcResult compile = nvrtcCompileProgram(program, 1, options);
    if (compile != NVRTC_SUCCESS) {
        size_t log_size = 0;
        nvrtcGetProgramLogSize(program, &log_size);
        std::string log(log_size, '\0');
        if (log_size) nvrtcGetProgramLog(program, log.data());
        nvrtcDestroyProgram(&program);
        fail("nvrtcCompileProgram", log);
    }
    size_t ptx_size = 0;
    check_nvrtc(nvrtcGetPTXSize(program, &ptx_size), "nvrtcGetPTXSize");
    std::string ptx(ptx_size, '\0');
    check_nvrtc(nvrtcGetPTX(program, ptx.data()), "nvrtcGetPTX");
    nvrtcDestroyProgram(&program);
    cached_ptx = std::move(ptx);
    return cached_ptx;
}

CudaDirectNv12Compositor::CudaDirectNv12Compositor(CUcontext context)
    : context_(context) {
    if (!context_) fail("CudaDirectNv12Compositor", "null CUDA context");
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    check_cuda(cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING), "cuStreamCreate");
    const auto ptx = get_compiled_direct_ptx();
    check_cuda(cuModuleLoadData(&module_, ptx.c_str()), "cuModuleLoadData(direct)");
    check_cuda(cuModuleGetFunction(&kernel_, module_,
                                   "nv12_composite_layer_batch_2x2"),
               "cuModuleGetFunction(nv12_composite_layer_batch_2x2)");
}

CudaDirectNv12Compositor::~CudaDirectNv12Compositor() {
    if (context_) (void)cuCtxSetCurrent(context_);
    if (stream_) (void)cuStreamSynchronize(stream_);
    if (layer_batch_buffer_) (void)cuMemFree(layer_batch_buffer_);
    if (module_) (void)cuModuleUnload(module_);
    if (stream_) (void)cuStreamDestroy(stream_);
}

bool CudaDirectNv12Compositor::composite_direct_nv12_batch(
    const runtime::GpuLayerBatch& batch,
    std::span<const CudaLayerResource> resources,
    CUdeviceptr bg_y, int bg_yp, CUdeviceptr bg_uv, int bg_uvp,
    CUdeviceptr out_y, int out_yp, CUdeviceptr out_uv, int out_uvp,
    std::uint32_t width, std::uint32_t height, CUstream stream) {
    if (!kernel_ || !bg_y || !bg_uv || !out_y || !out_uv ||
        width == 0 || height == 0 || batch.instances.empty()) return false;

    std::vector<DirectYuvLayerHost> host_layers;
    host_layers.reserve(batch.instances.size());
    for (const auto& instance : batch.instances) {
        if (instance.resource_index >= resources.size()) return false;
        const auto& resource = resources[instance.resource_index];
        const bool valid_rgba = resource.kind == CudaLayerResourceKind::Rgba &&
            resource.rgba && resource.pitch_bytes > 0;
        const bool valid_nv12 = resource.kind == CudaLayerResourceKind::Nv12 &&
            resource.y && resource.uv && resource.pitch_bytes > 0 &&
            resource.uv_pitch_bytes > 0;
        if ((!valid_rgba && !valid_nv12) ||
            resource.width == 0 || resource.height == 0) return false;
        if (instance.blend != BlendMode::Normal && instance.blend != BlendMode::Add) {
            return false;
        }
        DirectYuvLayerHost layer;
        layer.kind = static_cast<int>(resource.kind);
        layer.rgba = resource.rgba;
        layer.y = resource.y;
        layer.uv = resource.uv;
        layer.pitch = resource.pitch_bytes;
        layer.uv_pitch = resource.uv_pitch_bytes;
        layer.source_width = static_cast<int>(resource.width);
        layer.source_height = static_cast<int>(resource.height);
        layer.dst_x0 = instance.dst_x0;
        layer.dst_y0 = instance.dst_y0;
        layer.dst_x1 = instance.dst_x1;
        layer.dst_y1 = instance.dst_y1;
        if (layer.dst_x1 <= 1.0f && layer.dst_y1 <= 1.0f &&
            (layer.dst_x1 > layer.dst_x0 || layer.dst_y1 > layer.dst_y0)) {
            layer.dst_x0 *= static_cast<float>(width);
            layer.dst_y0 *= static_cast<float>(height);
            layer.dst_x1 *= static_cast<float>(width);
            layer.dst_y1 *= static_cast<float>(height);
        }
        layer.src_x0 = instance.src_x0;
        layer.src_y0 = instance.src_y0;
        layer.src_x1 = (instance.src_x1 == 0.0f && instance.src_y1 == 0.0f)
            ? 1.0f : instance.src_x1;
        layer.src_y1 = (instance.src_x1 == 0.0f && instance.src_y1 == 0.0f)
            ? 1.0f : instance.src_y1;
        layer.opacity = instance.opacity;
        layer.blend_mode = instance.blend == BlendMode::Add ? 1 : 0;
        layer.corner_radius = instance.corner_radius;
        host_layers.push_back(layer);
    }

    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    const CUstream active = stream ? stream : stream_;
    const std::size_t bytes = host_layers.size() * sizeof(DirectYuvLayerHost);
    if (layer_batch_capacity_ < bytes) {
        if (layer_batch_buffer_) check_cuda(cuMemFree(layer_batch_buffer_), "cuMemFree(layer batch)");
        check_cuda(cuMemAlloc(&layer_batch_buffer_, bytes), "cuMemAlloc(layer batch)");
        layer_batch_capacity_ = bytes;
    }
    const auto started = std::chrono::steady_clock::now();
    check_cuda(cuMemcpyHtoDAsync(
                   layer_batch_buffer_, host_layers.data(), bytes, active),
               "cuMemcpyHtoDAsync(layer batch)");
    CUdeviceptr layers = layer_batch_buffer_;
    int layer_count = static_cast<int>(host_layers.size());
    void* args[] = {&bg_y, &bg_yp, &bg_uv, &bg_uvp, &out_y, &out_yp,
                    &out_uv, &out_uvp, &layers, &layer_count, &width, &height};
    check_cuda(cuLaunchKernel(
                   kernel_, (width + 31) / 32,
                   (height + 31) / 32, 1, 16, 16, 1, 0, active, args, nullptr),
               "cuLaunchKernel(nv12_composite_layer_batch_2x2)");
    if (auto* counters = profiling::g_current_counters) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        counters->cuda_composite_frames.fetch_add(1, std::memory_order_relaxed);
        counters->cuda_composite_wall_us.fetch_add(
            static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
    }
    return true;
}

} // namespace chronon3d::media

#endif
