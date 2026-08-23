#include <chronon3d/backends/vulkan/cuda_nv12_surface_compositor.hpp>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#ifndef CHRONON3D_NVRTC_ARCHITECTURE
#define CHRONON3D_NVRTC_ARCHITECTURE "compute_75"
#endif

#include <nvrtc.h>

#include <chronon3d/core/profiling/profiling.hpp>

#include <stdexcept>
#include <string>
#include <chrono>
#include <dlfcn.h>
#include <filesystem>
#include <system_error>

namespace chronon3d::backends::vulkan {
namespace {

constexpr const char* kNv12Kernel = R"CUDA(
extern "C" __global__ void nv12_to_rgba(
    const unsigned char* y, int yp, const unsigned char* uv, int uvp,
    cudaSurfaceObject_t out, int width, int height) {
  const int x = (int)(blockIdx.x * blockDim.x + threadIdx.x);
  const int yy = (int)(blockIdx.y * blockDim.y + threadIdx.y);
  if (x >= width || yy >= height) return;
  const float yf = ((float)y[yy * yp + x] - 16.0f) / 219.0f;
  const int uvx = x & ~1;
  const float u = ((float)uv[(yy >> 1) * uvp + uvx] - 128.0f) / 224.0f;
  const float v = ((float)uv[(yy >> 1) * uvp + uvx + 1] - 128.0f) / 224.0f;
  float r = yf + 1.5748f * v;
  float g = yf - 0.1873f * u - 0.4681f * v;
  float b = yf + 1.8556f * u;
  r = fminf(fmaxf(r, 0.0f), 1.0f);
  g = fminf(fmaxf(g, 0.0f), 1.0f);
  b = fminf(fmaxf(b, 0.0f), 1.0f);
  surf2Dwrite(make_float4(r, g, b, 1.0f), out, x * (int)sizeof(float4), yy);
}

extern "C" __global__ void nv12_composite_overlay(
    const unsigned char* bg_y, int bg_yp, const unsigned char* bg_uv, int bg_uvp,
    cudaSurfaceObject_t fg_rgba,
    unsigned char* out_y, int out_yp, unsigned char* out_uv, int out_uvp,
    int width, int height) {
  const int x = (int)(blockIdx.x * blockDim.x + threadIdx.x);
  const int y = (int)(blockIdx.y * blockDim.y + threadIdx.y);
  if (x >= width || y >= height) return;

  float4 fg;
  surf2Dread(&fg, fg_rgba, x * (int)sizeof(float4), y);
  const float a = fminf(fmaxf(fg.w, 0.0f), 1.0f);

  const float bg_y_val = (float)bg_y[y * bg_yp + x];
  if (a <= 0.0f) {
    out_y[y * out_yp + x] = (unsigned char)bg_y_val;
  } else {
    const float y_fg = 16.0f + 219.0f * (0.2126f * fg.x + 0.7152f * fg.y + 0.0722f * fg.z);
    const float mixed_y = (1.0f - a) * bg_y_val + a * y_fg;
    out_y[y * out_yp + x] = (unsigned char)fminf(fmaxf(roundf(mixed_y), 16.0f), 235.0f);
  }

  if ((x & 1) == 0 && (y & 1) == 0 && (x + 1 < width) && (y + 1 < height)) {
    float4 fg10, fg01, fg11;
    surf2Dread(&fg10, fg_rgba, (x + 1) * (int)sizeof(float4), y);
    surf2Dread(&fg01, fg_rgba, x * (int)sizeof(float4), y + 1);
    surf2Dread(&fg11, fg_rgba, (x + 1) * (int)sizeof(float4), y + 1);

    const float a10 = fminf(fmaxf(fg10.w, 0.0f), 1.0f);
    const float a01 = fminf(fmaxf(fg01.w, 0.0f), 1.0f);
    const float a11 = fminf(fmaxf(fg11.w, 0.0f), 1.0f);

    const float a_sum = a + a10 + a01 + a11;
    const float a_block = a_sum * 0.25f;

    const int uv_idx = (y >> 1) * out_uvp + x;
    const int bg_uv_idx = (y >> 1) * bg_uvp + x;

    if (a_block <= 0.0f) {
      out_uv[uv_idx] = bg_uv[bg_uv_idx];
      out_uv[uv_idx + 1] = bg_uv[bg_uv_idx + 1];
    } else {
      const float r_avg = (fg.x * a + fg10.x * a10 + fg01.x * a01 + fg11.x * a11) / a_sum;
      const float g_avg = (fg.y * a + fg10.y * a10 + fg01.y * a01 + fg11.y * a11) / a_sum;
      const float b_avg = (fg.z * a + fg10.z * a10 + fg01.z * a01 + fg11.z * a11) / a_sum;

      const float u_fg = 128.0f + 224.0f * (-0.1146f * r_avg - 0.3854f * g_avg + 0.5000f * b_avg);
      const float v_fg = 128.0f + 224.0f * ( 0.5000f * r_avg - 0.4542f * g_avg - 0.0458f * b_avg);

      const float bg_u = (float)bg_uv[bg_uv_idx];
      const float bg_v = (float)bg_uv[bg_uv_idx + 1];

      const float u_out = (1.0f - a_block) * bg_u + a_block * u_fg;
      const float v_out = (1.0f - a_block) * bg_v + a_block * v_fg;

      out_uv[uv_idx] = (unsigned char)fminf(fmaxf(roundf(u_out), 16.0f), 240.0f);
      out_uv[uv_idx + 1] = (unsigned char)fminf(fmaxf(roundf(v_out), 16.0f), 240.0f);
    }
  }
}
)CUDA";

[[noreturn]] void fail(const char* operation, const std::string& detail) {
    throw std::runtime_error(std::string(operation) + ": " + detail);
}

void check_cuda(CUresult result, const char* operation) {
    if (result == CUDA_SUCCESS) return;
    const char* name = nullptr;
    cuGetErrorName(result, &name);
    fail(operation, name ? name : "CUDA error");
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

std::string get_compiled_nv12_ptx() {
    static std::mutex ptx_mutex;
    static std::string cached_ptx;
    std::lock_guard<std::mutex> lock(ptx_mutex);
    if (!cached_ptx.empty()) {
        return cached_ptx;
    }
    ensure_nvrtc_builtins_loaded();

    nvrtcProgram program{};
    check_nvrtc(nvrtcCreateProgram(&program, kNv12Kernel, "chronon_nv12.cu",
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

} // namespace

CudaNv12SurfaceCompositor::CudaNv12SurfaceCompositor(
    const CudaExternalMemoryInfo& target, CUcontext context)
    : context_(context) {
    if (!context_) fail("CudaNv12SurfaceCompositor", "null CUDA context");
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    check_cuda(cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING), "cuStreamCreate");

    const std::string ptx = get_compiled_nv12_ptx();
    check_cuda(cuModuleLoadData(&module_, ptx.data()), "cuModuleLoadData");
    check_cuda(cuModuleGetFunction(&kernel_, module_, "nv12_to_rgba"),
               "cuModuleGetFunction");
    (void)cuModuleGetFunction(&direct_nv12_kernel_, module_, "nv12_composite_overlay");
    bridge_ = std::make_unique<CudaVulkanSurfaceBridge>(
        target, context_, stream_);
}

CudaNv12SurfaceCompositor::~CudaNv12SurfaceCompositor() {
    if (context_) (void)cuCtxSetCurrent(context_);
    if (stream_) (void)cuStreamSynchronize(stream_);
    (void)cuCtxSynchronize();
    bridge_.reset();
    if (module_) (void)cuModuleUnload(module_);
    if (stream_) (void)cuStreamDestroy(stream_);
}

bool CudaNv12SurfaceCompositor::composite(
    CUdeviceptr y, int y_pitch, CUdeviceptr uv, int uv_pitch,
    std::uint32_t width, std::uint32_t height, CUstream stream) {
    if (!bridge_ || !kernel_ || !y || !uv || width == 0 || height == 0) return false;
    const auto started = std::chrono::steady_clock::now();
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    CUstream active = stream ? stream : stream_;
    if (!first_write_) bridge_->wait_for_vulkan(active);
    void* args[] = {&y, &y_pitch, &uv, &uv_pitch, nullptr, &width, &height};
    CUsurfObject surface = bridge_->surface_object();
    args[4] = &surface;
    check_cuda(cuLaunchKernel(kernel_, (width + 15) / 16, (height + 15) / 16, 1,
                              16, 16, 1, 0, active, args, nullptr),
               "cuLaunchKernel(nv12_to_rgba)");
    bridge_->signal_for_vulkan(active);
    first_write_ = false;
    if (auto* counters = profiling::g_current_counters) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        counters->cuda_composite_frames.fetch_add(1, std::memory_order_relaxed);
        counters->cuda_composite_wall_us.fetch_add(
            static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
    }
    return true;
}

bool CudaNv12SurfaceCompositor::composite_direct_nv12(
    CUdeviceptr bg_y, int bg_yp, CUdeviceptr bg_uv, int bg_uvp,
    CUdeviceptr out_y, int out_yp, CUdeviceptr out_uv, int out_uvp,
    std::uint32_t width, std::uint32_t height, CUstream stream) {
    if (!bridge_ || !direct_nv12_kernel_ || !bg_y || !bg_uv || !out_y || !out_uv || width == 0 || height == 0) {
        return false;
    }
    const auto started = std::chrono::steady_clock::now();
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    CUstream active = stream ? stream : stream_;
    if (!first_write_) bridge_->wait_for_vulkan(active);

    CUsurfObject surface = bridge_->surface_object();
    void* args[] = {
        &bg_y, &bg_yp, &bg_uv, &bg_uvp,
        &surface,
        &out_y, &out_yp, &out_uv, &out_uvp,
        &width, &height
    };
    check_cuda(cuLaunchKernel(direct_nv12_kernel_, (width + 15) / 16, (height + 15) / 16, 1,
                              16, 16, 1, 0, active, args, nullptr),
               "cuLaunchKernel(nv12_composite_overlay)");
    bridge_->signal_for_vulkan(active);
    first_write_ = false;

    if (auto* counters = profiling::g_current_counters) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        counters->cuda_composite_frames.fetch_add(1, std::memory_order_relaxed);
        counters->cuda_composite_wall_us.fetch_add(
            static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
    }
    return true;
}

} // namespace chronon3d::backends::vulkan

#endif
