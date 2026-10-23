# PhotoStack Vulkan shutter diagnostic — 2026-10-21

GPU: NVIDIA RTX A4000. The PhotoStack RenderPlan was rendered at 1920×1080, frames 45–53, with the requested 90° shutter and 8 temporal samples through both Vulkan and software raw output. Both jobs exited successfully. The Vulkan frame 46 and frame 47 were visually reviewed against software and fail parity: frame 46 is nearly white; frame 47 is substantially overbright. Per-frame mean RGB (software → Vulkan) is 34.5/31.6/30.1 → 241.6/241.5/241.6 at frame 46, and 37.8/35.9/36.5 → 116.9/116.3/118.0 at frame 47. Frames 45 and 48–53 are much closer, though not pixel-identical.

The Vulkan raw job was repeated for frame 46 with 2 temporal samples; it remains nearly white. A strict `require_gpu_native_full_graph`/NVENC attempt crashed with SIGSEGV inside a oneTBB temporal sample worker; limiting the worker count to one did not prevent it. The strict encode artifact is incomplete and is not evidence of a successful render.

The experimental Vulkan radial-gradient dispatch/shader change did not alter the frame and has been reverted. The evidence therefore records a backend correctness failure, not a fix.

Artifacts: `photo-stack_vulkan_045_053.rgba` and `photo-stack_software_045_053.rgba` are the nine-frame comparison sources; `photo-stack_shutter_backend_comparison.webp` is the visual comparison; `photo-stack_vulkan_shutter_contact.webp` is the Vulkan contact sheet; the two-sample frame-46 raw output and timing sidecar are in `/tmp` and were used only for diagnosis. `photo-stack_strict_restored_046.log` records the strict baseline capability failure; its incomplete MP4 is not a deliverable.
