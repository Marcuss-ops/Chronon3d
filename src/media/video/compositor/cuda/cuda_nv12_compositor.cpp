// cuda_nv12_compositor.cpp — CudaNv12Compositor NVRTC
// compilation helpers and object lifecycle (construction, module loading,
// destruction, synchronization).
//
// The embedded CUDA kernels live in cuda_nv12_kernels.hpp, the
// composite entry points in cuda_nv12_compositor_paths.cpp and
// cuda_nv12_compositor_batch.cpp.

#include "cuda_nv12_compositor_detail.hpp"

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include <chronon3d/cache/compiled_artifact_cache.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <spdlog/spdlog.h>

#ifndef CHRONON3D_NVRTC_ARCHITECTURE
// Ampere (compute_86) is the canonical GPU target for this stack (RTX A4000).
// Override via CMake CHRONON3D_NVRTC_ARCHITECTURE when building for a device
// with a different compute capability.
#define CHRONON3D_NVRTC_ARCHITECTURE "compute_86"
#endif

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <span>
#include <system_error>
#include <vector>

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

std::filesystem::path nv12_program_cache_directory() {
    if (const char* override_dir = std::getenv("CHRONON3D_PROGRAM_CACHE_DIR");
        override_dir && *override_dir) {
        return override_dir;
    }

    std::error_code error;
    const auto temp = std::filesystem::temp_directory_path(error);
    if (error) return ".chronon3d/program-cache";
    return temp / "chronon3d" / "program-cache";
}

std::string nvrtc_version() {
    int major = 0;
    int minor = 0;
    if (nvrtcVersion(&major, &minor) != NVRTC_SUCCESS) return "unknown";
    return std::to_string(major) + "." + std::to_string(minor);
}

cache::ProgramArtifactKey nv12_program_key() {
    const auto semantic_program_fingerprint =
        chronon3d::core::hash::HashBuilder{}
            .add("chronon3d.semantic-program.nv12.v1")
            .add_bytes(kNv12Kernel, std::strlen(kNv12Kernel))
            .finish();
    return {
        semantic_program_fingerprint,
        "nvrtc-ptx",
        "cuda-driver",
        CHRONON3D_NVRTC_ARCHITECTURE,
        "NVRTC",
        nvrtc_version(),
        "--gpu-architecture=" CHRONON3D_NVRTC_ARCHITECTURE,
        1};
}

std::string compile_nv12_ptx() {
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
}

std::string get_compiled_nv12_ptx() {
    const auto key = nv12_program_key();
    cache::CompiledArtifactCache program_cache(nv12_program_cache_directory());
    if (const auto cached = program_cache.load_program_artifact(key)) {
        return std::string(reinterpret_cast<const char*>(cached->data()),
                           cached->size());
    }

    auto ptx = compile_nv12_ptx();
    try {
        program_cache.save_program_artifact(
            key,
            std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(ptx.data()),
                ptx.size()));
    } catch (const std::exception& error) {
        // The compiled artifact remains usable for this request. A cache I/O
        // failure must not create a second in-memory artifact authority.
        spdlog::warn("[cuda-nv12] ProgramCache publish failed: {}", error.what());
    }
    return ptx;
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

std::shared_ptr<SharedCudaNv12Module> load_module_for_program_artifact(
    CUcontext context) {
    using cuda_nv12_detail::check_cuda;
    using cuda_nv12_detail::fail;
    using cuda_nv12_detail::get_compiled_nv12_ptx;
    // The persistent PTX is owned by ProgramCache.  Device/context residency
    // is a per-compositor lease: it cannot outlive the VideoDeviceRuntime
    // context and therefore needs no process-global module map or mutex.
    check_cuda(cuCtxSetCurrent(context), "cuCtxSetCurrent");
    const auto ptx = get_compiled_nv12_ptx();
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

    return shared;
}

} // namespace

void CudaNv12Compositor::warm_up(CUcontext context) {
    using cuda_nv12_detail::fail;

    if (!context) fail("CudaNv12Compositor::warm_up", "null CUDA context");
    (void)load_module_for_program_artifact(context);
}

CudaNv12Compositor::CudaNv12Compositor(
    const CudaExternalMemoryInfo& target, CUcontext context)
    : context_(context), surface_u8_(target.cuda_array_format == 2) {
    using cuda_nv12_detail::check_cuda;
    using cuda_nv12_detail::fail;

    if (!context_) fail("CudaNv12Compositor", "null CUDA context");
    check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
    check_cuda(cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING), "cuStreamCreate");

    auto shared = load_module_for_program_artifact(context_);
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

    auto shared = load_module_for_program_artifact(context_);
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
