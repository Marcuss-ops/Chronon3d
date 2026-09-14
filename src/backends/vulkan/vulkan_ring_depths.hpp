#pragma once

// ---------------------------------------------------------------------------
// vulkan_ring_depths.hpp — THE authority for the Vulkan backend's ring depths.
//
// Three rings declared their depth as a private constant in three different
// headers, with the relations between them documented only in prose:
//
//   * upload staging ring      → VulkanUploadAuthority     (was 3)
//   * submission/command ring  → VulkanSubmissionAuthority (was 6)
//   * retained transient pool  → VulkanSurfaceAuthority    (was 8)
//
// Nothing enforced the relations, so a depth tuned in one place could silently
// invalidate the reasoning in another — the same failure mode as the
// frame-reuse offset class of bugs, where the effective period of a ring (depth
// N) leaked into frame identity. The constants and their invariants now live
// here, and the invariants are compile-time.
//
// Peak-VRAM note: this is also the number that bounds how many frame-transient
// 1080p surfaces a single render may retain. Raising it raises per-job VRAM
// (a measured single-job peak of ~8.8 GB on a 16 GB device), which is why the
// daemon serializes render jobs. Change depths here only with a measured
// before/after VRAM report.
// ---------------------------------------------------------------------------

#include <cstddef>

namespace chronon3d::backends::vulkan {

/// Staging/upload ring: how many asynchronous uploads may overlap. Two are the
/// minimum that keeps staging work off the critical path; three tolerates one
/// upload in flight while the previous one's fence is still being consumed.
inline constexpr std::size_t kUploadRingDepth = 3;

/// Submission/command ring. Depth 6 lets the CPU record frame k+2 while frame k
/// executes and matches the CUDA encode ring shape. The historical depth of 3
/// forced a fence-wait on slot k-3 before recording frame k, capping pipeline
/// depth at ~2 frames and starving NVENC (measured 73% average utilization with
/// frame_batch_drain_wait_us as the top per-frame stall).
inline constexpr std::size_t kSubmissionRingDepth = 6;

/// Warm pool of retired frame-transient physical surfaces. A surface bound to
/// any in-flight submission slot must be retainable, so this can never be
/// smaller than the submission ring.
inline constexpr std::size_t kMaxRetainedTransientSurfaceSlots = 8;

// Frame k must be recordable while frame k-1 still executes.
static_assert(kSubmissionRingDepth >= 3,
              "submission ring must allow at least two frames in flight");
// Every in-flight submission slot may hold a transient surface.
static_assert(kMaxRetainedTransientSurfaceSlots >= kSubmissionRingDepth,
              "retained transient pool must cover the whole submission ring");
// Two uploads in flight keep staging off the critical path.
static_assert(kUploadRingDepth >= 2,
              "upload ring must allow two overlapping staging transfers");
// Retention is bounded by design: each slot is an 1080p surface, and per-job
// peak VRAM is what forces the daemon's single-job serialization.
static_assert(kMaxRetainedTransientSurfaceSlots <= 16,
              "retained transient pool must stay bounded for a single render");

} // namespace chronon3d::backends::vulkan
