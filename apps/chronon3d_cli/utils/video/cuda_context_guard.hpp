#pragma once

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>

namespace chronon3d::cli::cuda {

// Temporarily activates a CUDA context and restores the caller's context on
// scope exit. CUDA handles (notably events) are context-affine, while
// FFmpeg/NVENC is allowed to change the current context on its thread.
class ScopedCudaContext {
public:
    explicit ScopedCudaContext(CUcontext target) noexcept {
        current_target_ = target;
        if (!target || cuCtxGetCurrent(&previous_) != CUDA_SUCCESS ||
            previous_ == target) {
            valid_ = target && previous_ == target;
            return;
        }
        valid_ = cuCtxPushCurrent(target) == CUDA_SUCCESS;
    }

    ~ScopedCudaContext() noexcept {
        if (valid_ && previous_ != current_target_) {
            CUcontext ignored = nullptr;
            (void)cuCtxPopCurrent(&ignored);
        }
    }

    ScopedCudaContext(const ScopedCudaContext&) = delete;
    ScopedCudaContext& operator=(const ScopedCudaContext&) = delete;

    [[nodiscard]] bool ok() const noexcept { return valid_; }

private:
    CUcontext previous_{nullptr};
    CUcontext current_target_{nullptr};
    bool valid_{false};
};

struct OwnedCudaEvent {
    CUevent event{nullptr};
    CUcontext owner_context{nullptr};
};

inline void destroy(OwnedCudaEvent& owned) noexcept;

inline CUresult with_event_context(const OwnedCudaEvent& owned,
                                   CUresult (*operation)(CUevent)) noexcept {
    if (!owned.event || !owned.owner_context) return CUDA_ERROR_INVALID_HANDLE;
    ScopedCudaContext guard(owned.owner_context);
    if (!guard.ok()) return CUDA_ERROR_INVALID_CONTEXT;
    return operation(owned.event);
}

inline CUresult query(const OwnedCudaEvent& owned) noexcept {
    return with_event_context(owned, cuEventQuery);
}

inline CUresult synchronize(const OwnedCudaEvent& owned) noexcept {
    return with_event_context(owned, cuEventSynchronize);
}

inline CUresult create_recorded(OwnedCudaEvent& owned, CUcontext context,
                                CUstream stream) noexcept {
    if (!context || !stream) return CUDA_ERROR_INVALID_CONTEXT;
    ScopedCudaContext guard(context);
    if (!guard.ok()) return CUDA_ERROR_INVALID_CONTEXT;
    const auto created = cuEventCreate(&owned.event, CU_EVENT_DISABLE_TIMING);
    if (created != CUDA_SUCCESS) return created;
    owned.owner_context = context;
    const auto recorded = cuEventRecord(owned.event, stream);
    if (recorded != CUDA_SUCCESS) destroy(owned);
    return recorded;
}

inline void destroy(OwnedCudaEvent& owned) noexcept {
    if (!owned.event) return;
    if (owned.owner_context) {
        ScopedCudaContext guard(owned.owner_context);
        if (guard.ok()) (void)cuEventDestroy(owned.event);
    }
    owned.event = nullptr;
    owned.owner_context = nullptr;
}

} // namespace chronon3d::cli::cuda
#endif
