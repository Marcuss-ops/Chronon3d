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

For a retained capture, invoke `renderdoccmd` directly:

```bash
mkdir -p out/renderdoc
renderdoccmd capture --wait-for-exit \
  --capture-file out/renderdoc/chronon_capture.rdc \
  build/chronon/linux-vulkan-check/apps/chronon3d_cli/chronon3d_cli \
  render --input scene.json --output frame.png
```

The wrapper this section used to prescribe was retired in `5602bdb57`; the
command above is the direct invocation it performed. A missing RenderDoc
installation, a failed launch or an empty `.rdc` is still a hard failure —
treat it as a capture that did not run.
