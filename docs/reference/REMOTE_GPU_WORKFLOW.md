# Remote NVIDIA GPU workflow

This note describes how to use the `gpu-node` host for headless Vulkan runs
while keeping source edits and the normal build on the development machine.
Run one build at a time and one Chronon GPU render at a time. The GPU is shared
with other services.

## Verified host details

Checked on 2026-11-02:

| Item | Value |
|---|---|
| SSH alias | `gpu-node` |
| SSH host/user | `77.93.152.122`, `pierone` |
| GPU | NVIDIA RTX A4000, 16,376 MiB |
| Remote OS | Ubuntu 22.04.5 LTS, x86_64 |
| NVIDIA Vulkan ICD | `/usr/share/vulkan/icd.d/nvidia_icd.json` |
| Development OS | Ubuntu 26.04.1 LTS, x86_64 |

The last GPU check reported about 5.4 GiB already in use. Check `nvidia-smi`
before each render; available VRAM changes as other services use the card.
The last `vulkaninfo` output listed both the NVIDIA device and `llvmpipe`.
Confirm that the NVIDIA device is listed before testing Chronon.

At the time of the check, `~/Projects/company/Chronon3d` and its CLI did not
exist on `gpu-node`. Create a checkout there once if the render needs repository
fixtures or assets.

## SSH connection

Add this host entry to the development machine's `~/.ssh/config` if it is not
already present. Keep the file private with `chmod 600 ~/.ssh/config`.

```sshconfig
Host gpu-node
    HostName 77.93.152.122
    User pierone
    ServerAliveInterval 60
```

Check connectivity and GPU occupancy:

```bash
ssh gpu-node 'nvidia-smi'
```

Let SSH request authentication interactively. Do not put account or `sudo`
passwords in commands, scripts, repository files, or shell history; configure a
key for repeatable use.

Do not stop another GPU process to make room without coordinating with its
owner. In particular, the GPU may be serving an Ollama model as well as Chronon.

## Select and verify Vulkan

Run Vulkan commands with the NVIDIA ICD explicitly selected. This keeps the
selection local to that command and works for non-interactive SSH sessions:

```bash
ssh gpu-node 'VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json vulkaninfo --summary'
```

Check for `deviceName = NVIDIA RTX A4000`. The message about `DISPLAY` being
unset is expected for a headless run; the summary skips surface information.
If Vulkan lists only `llvmpipe`, stop and fix the ICD/driver setup before
running Chronon. `NVIDIA_VISIBLE_DEVICES` and
`NVIDIA_DRIVER_CAPABILITIES` are container-runtime settings and are not needed
for a normal SSH-launched process.

## Keep the source checkout current

The checkout is needed for plans, fixtures and assets. On the GPU host, clone
once (the host must have GitHub access):

```bash
ssh gpu-node 'mkdir -p ~/Projects/company && git clone git@github.com:Marcuss-ops/Chronon3d.git ~/Projects/company/Chronon3d'
```

Before a run, fast-forward it to the commit being tested:

```bash
ssh gpu-node 'git -C ~/Projects/company/Chronon3d fetch origin && git -C ~/Projects/company/Chronon3d checkout main && git -C ~/Projects/company/Chronon3d pull --ff-only origin main'
```

Record the tested commit with:

```bash
ssh gpu-node 'git -C ~/Projects/company/Chronon3d rev-parse HEAD'
```

## Build locally, run remotely

Configure and build the CLI once on the development machine. Do not start
another Ninja build or have a second agent build in the same checkout at the
same time. The video preset enables Vulkan and builds the CLI target:

```bash
cmake --preset linux-video-fast-dev
ninja -C build/chronon/linux-video-fast-dev -j12 chronon3d_cli
build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli
```

Before copying a binary, check its runtime dependencies on the destination
host. The development machine is currently Ubuntu 26.04 and `gpu-node` is
Ubuntu 22.04; a binary linked against newer glibc or `libstdc++` may not run on
the older host. Build in an Ubuntu 22.04-compatible environment when needed,
and include any non-system shared libraries required by the CLI.

```bash
CLI=build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli
ldd "$CLI" | rg 'not found' || true
readelf --version-info "$CLI" | rg -o 'GLIBC(_|XX_)[0-9.]+' | sort -Vu | tail
RUN_ID="$(git rev-parse --short HEAD)"
git status --short
ssh gpu-node "mkdir -p ~/gpu-runs/$RUN_ID"
scp "$CLI" "gpu-node:gpu-runs/$RUN_ID/chronon3d_cli"
ssh gpu-node "chmod 755 ~/gpu-runs/$RUN_ID/chronon3d_cli; ldd ~/gpu-runs/$RUN_ID/chronon3d_cli"
```

Resolve any `not found` entries on the GPU host before rendering. Transfer the
plan and any external assets too, or use files already present in the remote
checkout. For example, after syncing `main` and copying the CLI:

```bash
ssh gpu-node 'nvidia-smi'
ssh gpu-node "cd ~/Projects/company/Chronon3d && VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json ~/gpu-runs/$RUN_ID/chronon3d_cli render-plan --input tools/fixtures/vram-calibration-4k-overlay.plan.json --assets-root \"\$PWD\" --backend vulkan --gpu-hot-path-mode require_gpu_native --output /tmp/chronon-gpu-$RUN_ID.mp4"
mkdir -p output/remote-gpu
scp "gpu-node:/tmp/chronon-gpu-$RUN_ID.mp4" "output/remote-gpu/chronon-gpu-$RUN_ID.mp4"
```

The strict GPU mode makes a missing native Vulkan path fail instead of quietly
using a software path. For quick diagnostics, `--gpu-hot-path-mode auto` is
available, but record which mode was used with the result. Keep only one
Chronon render active on the card; check `nvidia-smi` again after it exits.

### Focused Vulkan regression tests

For the camera/DoF hardware probes, build `chronon3d_backend_registry_tests`
locally, strip debug information and compress it before transfer. The test
binary can be much larger than the CLI because it includes debug symbols.

```bash
TEST=build/chronon/linux-video-test/tests/chronon3d_backend_registry_tests
STRIPPED=/tmp/chronon3d_backend_registry_tests.stripped
objcopy --strip-debug "$TEST" "$STRIPPED"
gzip -1 -c "$STRIPPED" > "$STRIPPED.gz"
RUN_ID="$(git rev-parse --short=12 HEAD)"
ssh gpu-node "mkdir -p ~/gpu-runs/$RUN_ID"
scp "$STRIPPED.gz" "gpu-node:gpu-runs/$RUN_ID/backend-tests.gz"
sha256sum "$STRIPPED"
```

On the GPU host, decompress and verify the remote hash. Use the system loader
when the ABI check passes. If the executable needs a newer GLIBC/GLIBCXX, use a
separately verified user-space runtime bundle; leave Vulkan ICD and NVIDIA
driver libraries on the GPU host. The bundle used for the 2026-11-02 check is
at `/tmp/chronon-p2-gpucheck/runtime`.

```bash
ssh gpu-node "cd ~/gpu-runs/$RUN_ID && \
  gzip -dc backend-tests.gz > backend-tests && chmod +x backend-tests && \
  sha256sum backend-tests && \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json \
  /tmp/chronon-p2-gpucheck/runtime/lib/ld-linux-x86-64.so.2 \
  --library-path /tmp/chronon-p2-gpucheck/runtime/lib:. \
  ./backend-tests --test-case='Vulkan native occlusion holds a mesh half behind a 2.5D plane in both authoring orders'"
```

Run the disk-bokeh parity case with the same ICD/runtime and this exact filter:
`Vulkan disk bokeh kernel matches the CPU kernel within the frozen tolerance`.
Record local and remote executable hashes. Require a nonzero doctest case count;
an exit code of zero with zero matching cases is not a test pass.

## If the binary cannot be transferred

Use the same pinned source commit on the GPU host and build there, one Ninja
process at a time. The remote machine already has the NVIDIA driver and Vulkan
runtime. This avoids glibc/libstdc++ incompatibility between the newer
development OS and Ubuntu 22.04, at the cost of compiling on the GPU host.

## Record the result

For a useful remote check, keep the source commit, CLI SHA-256, command line,
`nvidia-smi` output before/after, Vulkan device name, exit status and output
artifact together. A successful SSH connection or `vulkaninfo` check alone
does not prove that Chronon rendered on the NVIDIA GPU.
