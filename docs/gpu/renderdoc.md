# RenderDoc Vulkan capture

The Vulkan backend exposes validation/debug-name controls through
`VulkanDebugContext`. Names are enabled by default when the backend is built
with Vulkan and can be controlled with:

```text
CHRONON3D_VULKAN_DEBUG_NAMES=1
CHRONON3D_VULKAN_VALIDATION=1
CHRONON3D_VULKAN_SYNC_VALIDATION=1
CHRONON3D_VULKAN_GPU_ASSISTED_VALIDATION=1
```

For a retained capture, use the checked-in wrapper:

```bash
tools/capture_renderdoc.sh out/renderdoc -- \
  build/chronon/linux-vulkan-check/apps/chronon3d_cli/chronon3d_cli \
  render --input scene.json --output frame.png
```

The wrapper requires `renderdoccmd`, launches with `--wait-for-exit`, writes a
`.rdc` file and emits `capture.json` containing the command, commit, host,
exit status and capture size. A missing RenderDoc installation, failed launch
or empty capture is a hard failure.
