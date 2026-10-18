# CPU-only evidence

This report is intentionally limited to work that can be certified without a
GPU. It must be regenerated on the exact source SHA being reviewed with:

```text
tools/verify_cpu_only_evidence.sh build/chronon/linux-video-test
```

The script records and verifies:

- source SHA, native-FFmpeg build configuration, and local FFmpeg version;
- A1 software/native MP4 export through the real `render_and_encode_native()`
  route, plus sidecar, receipt/checksum, and same-run fail-closed path;
- B3 native segment assembly through validation, timestamp normalization,
  execution, and output checksum;
- the registered CLI contract suite, the `.inc` architecture ratchet, and
  `git diff --check`;
- the text-header census/fan-out gate (`text_resource_contracts.hpp`, focused
  Unicode headers, and the compatibility-facade consumer check).

The registry target also compiles after the include census cleanup. Its full
binary is not a CPU certification gate: on this host it reports 60 passed and
one expected GPU-only failure (`Vulkan backend creates a persistent headless
device`, requiring a discrete RTX A4000). That result is kept distinct from
the CPU evidence below.

GPU/CUDA/MTSDF hardware certification and GitHub same-SHA CI remain explicitly
`NOT RUN` here. Passing this report is therefore CPU evidence, not a green
release certification. The script also reports whether the working tree is
clean: a dirty-tree run is `PASS_SAME_TREE`, not `PASS_SAME_SHA`.
