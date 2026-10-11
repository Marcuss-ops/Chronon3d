# CPU-only evidence

## Run — 2026-10-11

- Source HEAD: `e8e06f5b315f4ffe39e6819e926dcfc1156b8408`; worktree was dirty,
  so this certifies the tested tree and is not same-SHA evidence.
- Preset: `linux-video-test`; FFmpeg `8.0.1-3ubuntu2`; native FFmpeg enabled.
- `chronon3d_cli_tests` built successfully and its registered CTest passed
  (`1/1`, 48.49 s). The build exposed stale `FF_PROFILE_H264_*` identifiers;
  these were replaced with FFmpeg's `AV_PROFILE_H264_*` names in the encoder,
  dispatch, and packet-assembler contract test.
- A1 fail-closed/export contract: `1/1` case, `33/33` assertions.
- A1 direct software-composition native export: `1/1` case, `25/25`
  assertions.
- B3 CPU segment assembly: `1/1` case, `53/53` assertions.
- Architecture `.inc` ratchet: `1/1` rule passed, handwritten production
  `.inc` count `0`.
- Text header fan-out: `19` consumers against ratchet `22`; Unicode utility
  direct consumers `0`.
- `git diff --check` passed. GPU/CUDA certification and GitHub same-SHA CI
  were not run. The verifier reported `CPU_EVIDENCE=PASS_SAME_TREE` and
  `SAME_SHA_ELIGIBLE=NO_DIRTY_WORKTREE`.

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

## Static-gate refresh — 2026-10-11

- `python3 tools/check_architecture.py --root .` passed **206/206** rules on
  the current checkout.
- `python3 tools/check_text_header_fanout.py --root .` passed: 19 direct
  consumers against the ratchet of 22, with zero direct Unicode utility
  consumers.
- `git diff --check` passed.
- The checkout remains dirty. These local static checks do not upgrade the
  native-FFmpeg run above to same-SHA CI or GPU certification.
