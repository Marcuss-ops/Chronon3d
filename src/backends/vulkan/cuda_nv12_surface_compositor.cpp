// cuda_nv12_surface_compositor.cpp — CudaNv12SurfaceCompositor NVRTC
// compilation helpers and object lifecycle (construction, module loading,
// destruction, synchronization).
//
// The embedded CUDA kernels live in cuda_nv12_composite_kernels.hpp, the
// composite entry points in cuda_nv12_surface_compositor_paths.cpp and
// cuda_nv12_surface_compositor_batch.cpp.

#include "cuda_nv12_compositor_detail.hpp"

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <dlfcn.h>
#include <filesystem>
#include <mutex>
#include <system_error>

namespace chronon3d::backends::vulkan {

namespace {

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

} // namespace

namespace cuda_nv12_detail {

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

} // namespace cuda_nv12_detail

void CudaNv12SurfaceCompositor::warm_up(CUcontext context) {
    using cuda_nv12_detail::check_cuda;
    using cuda_nv12_detail::fail;
    using cuda_nv12_detail::get_compiled_nv12_ptx;

    if (!context) fail("CudaNv12SurfaceCompositor::warm_up", "null CUDA context");
    check_cuda(cuCtxSetCurrent(context), "cuCtxSetCurrent(warm_up)");
    const std::string ptx = get_compiled_nv12_ptx();
    CUmodule module = nullptr;
    check_cuda(cuModuleLoadData(&module, ptx.c_str()),
               "cuModuleLoadData(warm_up)");
    check_cuda(cuModuleUnload(module), "cuModuleUnload(warm_up)");
}

CudaNv12SurfaceCompositor::CudaNv12SurfaceCompositor(
    const CudaExternalMemoryInfo& target, CUcontext context)
    : context_(context), surface_u8_(target.cuda_array_format == 2) {
    using cuda_nv12_detail::check_cuda;
    using cuda_nv12_detail::fail;
    using cuda_nv12_detail::get_compiled_nv12_ptx;

    if (!context_) fail("CudaNv12SurfaceCompositor", "null CUDA context");
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    check_cuda(cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING), "cuStreamCreate");

    const std::string ptx = get_compiled_nv12_ptx();

    // The native encoder shares FFmpeg's primary CUDA context.  Surface any
    // earlier asynchronous failure before loading the compositor module so a
    // stale context error cannot be misreported as a PTX loading failure.
    const CUresult module_result = cuModuleLoadData(&module_, ptx.c_str());
    if (module_result != CUDA_SUCCESS) {
        const char* name = nullptr;
        const char* description = nullptr;
        cuGetErrorName(module_result, &name);
        cuGetErrorString(module_result, &description);
        std::string detail = name ? name : "CUDA error";
        if (description) {
            detail += " (";
            detail += description;
            detail += ")";
        }
        detail += "; arch=" CHRONON3D_NVRTC_ARCHITECTURE;
        detail += "; ptx_bytes=" + std::to_string(ptx.size());
        fail("cuModuleLoadData", detail);
    }
    check_cuda(cuModuleGetFunction(&kernel_, module_, "nv12_to_rgba"),
               "cuModuleGetFunction(nv12_to_rgba)");
    check_cuda(cuModuleGetFunction(&p010_kernel_, module_, "p010_to_rgba"),
               "cuModuleGetFunction(p010_to_rgba)");
    check_cuda(cuModuleGetFunction(&direct_nv12_kernel_, module_,
                                   "nv12_composite_overlay_2x2"),
               "cuModuleGetFunction(nv12_composite_overlay_2x2)");
    check_cuda(cuModuleGetFunction(&direct_nv12_batch_kernel_, module_,
                                   "nv12_composite_layer_batch_2x2"),
               "cuModuleGetFunction(nv12_composite_layer_batch_2x2)");
    check_cuda(cuModuleGetFunction(&rgba_to_nv12_kernel_, module_,
                                   "rgba_surface_to_nv12_2x2"),
               "cuModuleGetFunction(rgba_surface_to_nv12_2x2)");
    check_cuda(cuModuleGetFunction(&rgba_u8_to_nv12_kernel_, module_,
                                   "rgba_u8_surface_to_nv12_2x2"),
               "cuModuleGetFunction(rgba_u8_surface_to_nv12_2x2)");
    bridge_ = std::make_unique<CudaVulkanSurfaceBridge>(
        target, context_, stream_);
}

CudaNv12SurfaceCompositor::CudaNv12SurfaceCompositor(CUcontext context)
    : context_(context), surface_u8_(false) {
    using cuda_nv12_detail::check_cuda;
    using cuda_nv12_detail::fail;
    using cuda_nv12_detail::get_compiled_nv12_ptx;

    if (!context_) fail("CudaNv12SurfaceCompositor", "null CUDA context");
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    check_cuda(cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING), "cuStreamCreate");
    const std::string ptx = get_compiled_nv12_ptx();
    check_cuda(cuModuleLoadData(&module_, ptx.c_str()), "cuModuleLoadData");
    check_cuda(cuModuleGetFunction(&direct_nv12_kernel_, module_,
                                   "nv12_composite_overlay_2x2"),
               "cuModuleGetFunction(nv12_composite_overlay_2x2)");
    check_cuda(cuModuleGetFunction(&direct_nv12_batch_kernel_, module_,
                                   "nv12_composite_layer_batch_2x2"),
               "cuModuleGetFunction(nv12_composite_layer_batch_2x2)");
}

CudaNv12SurfaceCompositor::~CudaNv12SurfaceCompositor() {
    if (context_) (void)cuCtxSetCurrent(context_);
    if (stream_) (void)cuStreamSynchronize(stream_);
    bridge_.reset();
    if (layer_batch_buffer_) (void)cuMemFree(layer_batch_buffer_);
    if (module_) (void)cuModuleUnload(module_);
    if (stream_) (void)cuStreamDestroy(stream_);
}

bool CudaNv12SurfaceCompositor::synchronize() noexcept {
    if (!stream_) return true;
    if (context_) (void)cuCtxSetCurrent(context_);
    return cuStreamSynchronize(stream_) == CUDA_SUCCESS;
}

} // namespace chronon3d::backends::vulkan

#endif
