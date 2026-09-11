// cuda_nv12_compositor.cpp — CudaNv12Compositor NVRTC
// compilation helpers and object lifecycle (construction, module loading,
// destruction, synchronization).
//
// The embedded CUDA kernels live in cuda_nv12_kernels.hpp, the
// composite entry points in cuda_nv12_compositor_paths.cpp and
// cuda_nv12_compositor_batch.cpp.

#include "cuda_nv12_compositor_detail.hpp"

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

#ifndef CHRONON3D_NVRTC_ARCHITECTURE
// Ampere (compute_86) is the canonical GPU target for this stack (RTX A4000).
// Override via CMake CHRONON3D_NVRTC_ARCHITECTURE when building for a device
// with a different compute capability.
#define CHRONON3D_NVRTC_ARCHITECTURE "compute_86"
#endif

#include <chrono>
#include <dlfcn.h>
#include <filesystem>
#include <unordered_map>
#include <system_error>

namespace chronon3d::media::video {

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

const std::string& get_compiled_nv12_ptx() {
    // Function-local constant initialization is the sole compile-once
    // authority. After initialization readers only borrow the immutable PTX;
    // no explicit mutex, mutation, or per-call string copy remains on the
    // compositor hot path.
    static const std::string cached_ptx = [] {
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
        return ptx;
    }();
    return cached_ptx;
}

} // namespace cuda_nv12_detail

namespace {

struct SharedCudaNv12Module {
    CUcontext context{nullptr};
    CUmodule module{nullptr};
    CUfunction kernel{nullptr};
    CUfunction p010_kernel{nullptr};
    CUfunction direct_nv12_kernel{nullptr};
    CUfunction direct_nv12_batch_kernel{nullptr};
    CUfunction direct_p010_batch_kernel{nullptr};
    CUfunction rgba_to_nv12_kernel{nullptr};
    CUfunction rgba_u8_to_nv12_kernel{nullptr};

    ~SharedCudaNv12Module() {
        if (!module) return;

        // A compositor owns the last lease in normal operation, so its
        // context is still alive here. Make the CUDA ownership boundary
        // explicit and restore the caller's context after unloading. If the
        // context has already gone away, leak the driver handle rather than
        // calling into a dead CUDA context during process teardown.
        CUcontext previous = nullptr;
        const bool had_previous = cuCtxGetCurrent(&previous) == CUDA_SUCCESS;
        if (cuCtxSetCurrent(context) != CUDA_SUCCESS) {
            spdlog::warn("[cuda-nv12] context already unavailable while releasing module; "
                         "leaking driver handle safely");
            return;
        }
        (void)cuModuleUnload(module);
        module = nullptr;
        if (had_previous && previous != context) (void)cuCtxSetCurrent(previous);
    }
};

std::shared_ptr<SharedCudaNv12Module> get_or_load_module(CUcontext context) {
    using cuda_nv12_detail::check_cuda;
    using cuda_nv12_detail::fail;
    using cuda_nv12_detail::get_compiled_nv12_ptx;

    static std::mutex s_module_mutex;
    // Do not make this process-global map an owner. CUDA modules are owned by
    // the primary context retained by VideoDeviceRuntime; a strong static
    // cache can outlive that context and unload a stale CUmodule during C++
    // static destruction. Compositors retain the actual lease below.
    static std::unordered_map<CUcontext,
        std::weak_ptr<SharedCudaNv12Module>> s_modules;

    // AUDIT-03: the module cache is double-checked. On the warm path (every
    // compositor construction after the first) the shared_ptr is returned
    // without ever taking s_module_mutex, so concurrent compositor
    // construction on different threads no longer serializes.
    {
        // Map reads are guarded; std::unordered_map::find needs the lock.
        std::lock_guard lock(s_module_mutex);
        auto it = s_modules.find(context);
        if (it != s_modules.end()) {
            if (auto cached = it->second.lock()) return cached;
            s_modules.erase(it);
        }
    }

    // AUDIT-03: NVRTC compilation and cuModuleLoadData run OUTSIDE the
    // module-cache mutex. get_compiled_nv12_ptx() is a function-local
    // constant (thread-safe one-time init); the module load is keyed to the
    // caller's context and the mutex is only re-taken to publish the result.
    // Threads racing on the same context redundantly load the module and the
    // first publisher wins — correct, and only on the cold path.
    check_cuda(cuCtxSetCurrent(context), "cuCtxSetCurrent");
    const auto& ptx = get_compiled_nv12_ptx();
    auto shared = std::make_shared<SharedCudaNv12Module>();
    shared->context = context;

    const CUresult module_result = cuModuleLoadData(&shared->module, ptx.c_str());
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
    check_cuda(cuModuleGetFunction(&shared->kernel, shared->module, "nv12_to_rgba"),
               "cuModuleGetFunction(nv12_to_rgba)");
    check_cuda(cuModuleGetFunction(&shared->p010_kernel, shared->module, "p010_to_rgba"),
               "cuModuleGetFunction(p010_to_rgba)");
    check_cuda(cuModuleGetFunction(&shared->direct_nv12_kernel, shared->module,
                                   "nv12_composite_overlay_2x2"),
               "cuModuleGetFunction(nv12_composite_overlay_2x2)");
    check_cuda(cuModuleGetFunction(&shared->direct_nv12_batch_kernel, shared->module,
                                   "nv12_composite_layer_batch_2x2"),
               "cuModuleGetFunction(nv12_composite_layer_batch_2x2)");
    check_cuda(cuModuleGetFunction(&shared->direct_p010_batch_kernel, shared->module,
                                   "p010_composite_layer_batch_2x2"),
               "cuModuleGetFunction(p010_composite_layer_batch_2x2)");
    check_cuda(cuModuleGetFunction(&shared->rgba_to_nv12_kernel, shared->module,
                                   "rgba_surface_to_nv12_2x2"),
               "cuModuleGetFunction(rgba_surface_to_nv12_2x2)");
    check_cuda(cuModuleGetFunction(&shared->rgba_u8_to_nv12_kernel, shared->module,
                                   "rgba_u8_surface_to_nv12_2x2"),
               "cuModuleGetFunction(rgba_u8_surface_to_nv12_2x2)");

    // AUDIT-03: publish under the lock (short critical section, no CUDA API
    // inside). Another thread may have published its own load first; its
    // entry wins and ours is discarded (module unloaded via refcount).
    {
        std::lock_guard lock(s_module_mutex);
        auto it = s_modules.find(context);
        if (it != s_modules.end()) {
            if (auto cached = it->second.lock()) return cached;
            s_modules.erase(it);
        }
        s_modules[context] = shared;
    }
    return shared;
}

} // namespace

void CudaNv12Compositor::warm_up(CUcontext context) {
    using cuda_nv12_detail::fail;

    if (!context) fail("CudaNv12Compositor::warm_up", "null CUDA context");
    (void)get_or_load_module(context);
}

CudaNv12Compositor::CudaNv12Compositor(
    const CudaExternalMemoryInfo& target, CUcontext context)
    : context_(context), surface_u8_(target.cuda_array_format == 2) {
    using cuda_nv12_detail::check_cuda;
    using cuda_nv12_detail::fail;

    if (!context_) fail("CudaNv12Compositor", "null CUDA context");
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    check_cuda(cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING), "cuStreamCreate");

    auto shared = get_or_load_module(context_);
    module_lease_ = shared;
    module_ = shared->module;
    kernel_ = shared->kernel;
    p010_kernel_ = shared->p010_kernel;
    direct_nv12_kernel_ = shared->direct_nv12_kernel;
    direct_nv12_batch_kernel_ = shared->direct_nv12_batch_kernel;
    direct_p010_batch_kernel_ = shared->direct_p010_batch_kernel;
    rgba_to_nv12_kernel_ = shared->rgba_to_nv12_kernel;
    rgba_u8_to_nv12_kernel_ = shared->rgba_u8_to_nv12_kernel;

    bridge_ = std::make_unique<CudaVulkanSurfaceBridge>(
        target, context_, stream_);
}

CudaNv12Compositor::CudaNv12Compositor(CUcontext context)
    : context_(context), surface_u8_(false) {
    using cuda_nv12_detail::check_cuda;
    using cuda_nv12_detail::fail;

    if (!context_) fail("CudaNv12Compositor", "null CUDA context");
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    check_cuda(cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING), "cuStreamCreate");

    auto shared = get_or_load_module(context_);
    module_lease_ = shared;
    module_ = shared->module;
    direct_nv12_kernel_ = shared->direct_nv12_kernel;
    direct_nv12_batch_kernel_ = shared->direct_nv12_batch_kernel;
    direct_p010_batch_kernel_ = shared->direct_p010_batch_kernel;
}

CudaNv12Compositor::~CudaNv12Compositor() {
    if (context_) (void)cuCtxSetCurrent(context_);
    if (stream_) (void)cuStreamSynchronize(stream_);
    bridge_.reset();
    for (int slot = 0; slot < kLayerBatchSlots; ++slot) {
        if (layer_batch_buffer_[slot]) (void)cuMemFree(layer_batch_buffer_[slot]);
        if (layer_batch_host_[slot])
            (void)cuMemFreeHost(reinterpret_cast<void*>(layer_batch_host_[slot]));
    }
    // module_ is shared and owned by SharedCudaNv12Module
    if (stream_) (void)cuStreamDestroy(stream_);
}

bool CudaNv12Compositor::synchronize() noexcept {
    if (!stream_) return true;
    if (context_) (void)cuCtxSetCurrent(context_);
    return cuStreamSynchronize(stream_) == CUDA_SUCCESS;
}

} // namespace chronon3d::media::video

#endif
