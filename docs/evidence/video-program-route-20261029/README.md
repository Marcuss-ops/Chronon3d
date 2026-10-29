# CompiledProgram video route A/B — 2026-10-29

## Current-source glTF probe

- Source HEAD: `014eeb4dfd1703820d49b6382d127ab253bf1449` (dirty worktree).
- Build: `build/chronon/linux-video-release`, target `chronon3d_cli`, incremental rebuild `-j7` PASS after making the typed route opt-in.
- Host: Linux x86_64, RTX A4000. Pixel probes used the software backend and raw RGBA sink.

Canonical composition route, `CHRONON3D_VIDEO_PROGRAM_ROUTE=0`:

- Command used the current Motion3D fixture `ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf/animated-gltf.plan.json`, `--backend software --start-frame 0 --end-frame 30 --video-sink raw`.
- Exit 0; 31 frames, 31 distinct hashes; full RGBA SHA-256 `a8d12571e3cc657b7a847dfc05a8888c766b6d9658aecbae3a18c65c88fe2648`.

Typed route, `CHRONON3D_VIDEO_PROGRAM_ROUTE=1`, same plan/backend/frame range/sink:

- Exit 0; 31 frames but only 1 distinct frame hash; full RGBA SHA-256 `5a6ee5f3a373b03d58fa9eb49942fcda13e1d6a5070a29850b28fe2787319dde`.
- Every frame is transparent (`alpha > 0` pixels: 0). It drops the animated mesh entirely, so this route fails pixel equivalence and cannot be used for export.
- The rendered frame log says `program route active (typed fast-path): 31 frames, render_graph_eval_ms=97.7`; the timing is not a valid performance result because the pixels are missing.

## Text stress A/B

The 32-frame 1920×1080 text stress run also failed exact equivalence: canonical route SHA-256 `d0078155902879832fd7d102c42aaa26f50b93ef7f3c6a58fdeb6daf8f145237`; typed output SHA-256 `8921bd8dc5282cea294ef35d635ddb87c437893f52bb5e6a2164587531693592`. The typed output omitted the text. Adding a cached compatibility-scene topology produced the same typed digest, so that attempted correction did not restore the missing content.

## Fail-safe route selection

Because the typed route demonstrably loses content, `CHRONON3D_VIDEO_PROGRAM_ROUTE` now requires an explicit truthy value (`1`, `on`, or `true`) to enable it. With the variable unset, a one-frame default-route raw probe matched canonical frame 0 exactly (SHA-256 `f0284422aa3956dfe22572a8fe3f75ec83d3e152decda7e69938a76a0039181e`) and did not log that the program route was active.

This keeps the diagnostic implementation available for continued A/B work but does not close the performance ticket. The typed route must pass per-frame pixel equivalence for text, animated mesh, alpha and the wider corpus before it can be considered for default use.
