#include "cuda_nv12_surface_compositor.hpp"

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <nvrtc.h>

#include <chronon3d/core/profiling/profiling.hpp>

#include <stdexcept>
#include <string>
#include <chrono>
#include <dlfcn.h>
#include <filesystem>

namespace chronon3d::cli {
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
    // NVRTC loads its device-builtins library by bare soname.  Python CUDA
    // wheels expose libnvrtc and libnvrtc-builtins next to one another, but
    // the latter is not always on LD_LIBRARY_PATH.  Resolve the directory
    // from the already-linked NVRTC symbol and load the matching builtins
    // explicitly before compiling the runtime kernel.
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&nvrtcCompileProgram), &info) == 0 ||
        !info.dli_fname) return;
    const auto directory = std::filesystem::path(info.dli_fname).parent_path();
    const auto candidate = directory / "libnvrtc-builtins.so.13.0";
    if (std::filesystem::exists(candidate)) {
        (void)dlopen(candidate.c_str(), RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
    }
}

} // namespace

CudaNv12SurfaceCompositor::CudaNv12SurfaceCompositor(
    const backends::vulkan::CudaExternalMemoryInfo& target, CUcontext context)
    : context_(context) {
    if (!context_) fail("CudaNv12SurfaceCompositor", "null CUDA context");
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    check_cuda(cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING), "cuStreamCreate");
    ensure_nvrtc_builtins_loaded();

    nvrtcProgram program{};
    check_nvrtc(nvrtcCreateProgram(&program, kNv12Kernel, "chronon_nv12.cu",
                                   0, nullptr, nullptr), "nvrtcCreateProgram");
    const char* options[] = {"--gpu-architecture=compute_75"};
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
    check_cuda(cuModuleLoadData(&module_, ptx.data()), "cuModuleLoadData");
    check_cuda(cuModuleGetFunction(&kernel_, module_, "nv12_to_rgba"),
               "cuModuleGetFunction");
    bridge_ = std::make_unique<backends::vulkan::CudaVulkanSurfaceBridge>(
        target, context_, stream_);
}

CudaNv12SurfaceCompositor::~CudaNv12SurfaceCompositor() {
    if (context_) (void)cuCtxSetCurrent(context_);
    // The imported image/semaphore pair may still be referenced by the last
    // asynchronous NVDEC conversion.  Destroying the external semaphore
    // before that work retires is rejected by newer CUDA drivers and can
    // terminate the process inside cuDestroyExternalSemaphore().
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
    // surface_object() returns by value; keep a writable local for the CUDA ABI.
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

} // namespace chronon3d::cli

#endif
