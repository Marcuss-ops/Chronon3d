# GOP copy native evidence — Release / 8c2fbd54

## Host safety

- Build: `build/chronon/linux-video-release`
- Build parallelism: `ninja -j12 chronon3d_cli`
- Pre-build guard: 26 GiB `MemAvailable`, 1.1 GiB `SwapFree`, 57 GiB free disk
- The unrelated rendering daemon was left running.

## Integration command

```text
chronon3d_cli render \
  --plan .tmp/direct-yuv-cert/p010.plan.json \
  --assets-root . --backend vulkan \
  --gop-source .tmp/direct-yuv-cert/p010-input.mp4 \
  --gop-copy-only --start-frame 0 --end-frame 23 \
  --report -o .tmp/gop-copy-repro-fixed-20260914/render.mp4
```

Observed:

```text
gop preflight: 1 GOPs (1 copy-eligible, 0 re-encode, all-copy)
execution: decode=0 composite=0 encode=0
bitstream-copy: 24 video packets, 0 audio packets in 3.9ms
```

`ffprobe` verified the output as HEVC 1280x720 with 24 decoded frames and
0.800 s duration. No Vulkan, NVDEC, CUDA compositor, or NVENC work was
entered on this path.

## Regression coverage

`chronon3d_cli_tests --test-case='GOP planner copies only a closed safe untouched GOP'`
passed with 16 assertions. The test covers a monotonic-DTS GOP whose PTS are
reordered as expected for B-frames, plus rejection of non-monotonic DTS.

The complete CLI test binary still has one unrelated existing failure in
`VideoExecutionResolver: image/text composition without source`: its expected
`pipe/none` values do not match the current `native/nvenc` configuration.
