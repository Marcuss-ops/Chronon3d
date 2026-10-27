# Chronon3D hotspot refactor checklist

Working checklist for the 30 hotspot splits in the user-provided refactor plan. A row is marked complete only after its implementation and a targeted build or syntax check have been verified. The checklist is updated as each split is completed.

## A. Render Plan

- [x] **1. `compile_render_plan`** — entry now orchestrates separate request validation, media resolution, scene materialization, and composition compilation phases; video compatibility remains in its own translation unit. Render Plan target rebuilt successfully.
- [x] **2. `render_plan.hpp`** — split into core/audio/lighting/overlay domain headers; `render_plan.hpp` is the thin umbrella that includes the core. Header smoke compile passed.
- [x] **3. Decoder layer support** — split into geometry, materials, and orchestration translation units; syntax checked.
- [x] **4. `render_plan_decoder.cpp`** — top-level orchestration with preflight, globals, output, audio, and layer phases extracted; decoder translation units syntax checked.
- [x] **5. Fingerprint dispatch** — `fingerprint.cpp` is a compact plan-level dispatcher and layer work is split across identity, transform, style, and effects translation units; verified in the Render Plan build.
- [x] **6. Static Lottie expansion** — expansion now lives under `lottie_expansion/` behind its own private interface; effects, text, masks, paint, and transform helpers remain in focused compiler units. Render Plan target rebuilt successfully.
- [x] **7. Validator/fingerprint/schema mapping** — split V3→V2 compatibility projection from schema dispatch/diagnostics; schemas remain versioned source files, and fingerprint responsibilities are in layer-specific TUs. Both validator units built successfully.

## B. Vulkan

- [x] **8. `VulkanBackend::Impl`** — split Image, surface store, domain lock, and recording scope into dedicated private implementation fragments; Vulkan target rebuilt successfully.
- [x] **9. Vulkan lifecycle and plan batch** — `begin_plan_batch` moved to `vulkan_plan_batch.cpp`; syntax checked, full target build in progress.
- [x] **10. Private backend operations** — composite dispatch, shape/fill wrappers, and transforms now live in focused translation units alongside existing filter/color files; Vulkan target rebuilt successfully.
- [x] **11. Native shape dispatch** — dispatcher now forwards to path (fill/stroke) and rect/grid implementation units; text rendering remains in the separate GPU text-run subsystem. Vulkan target build passed.
- [x] **12. `vulkan_surface_authority.hpp`** — moved the 43 template method definitions to `vulkan_surface_authority.cpp` and explicitly instantiated the sole Vulkan backend specialization; header now retains the API and state layout. Vulkan target rebuilt successfully.

## C. Render Graph

- [x] **13. Node execution policy** — runtime transparent-skip policy and fused-batch classification/dispatch are grouped under `executor/graph_dispatch/`; single-node execution delegates those decisions there. Layer/source pipeline passes remain in `builder/passes/`, since they construct the graph rather than execute runtime nodes. Graph executor target rebuilt successfully.
- [x] **14. Composite and multi-source nodes** — composite native/backdrop paths are in focused units; multi-source entry now prepares the framebuffer and dispatches item/group execution to `multi_source_node_execute_items.cpp`, which finalizes the result. Graph node target rebuilt successfully.
- [x] **15. GPU text run** — the cached bridge, glyph packing/preparation, and MTSDF draw path now live in separate translation units; graph node target rebuilt successfully.
- [x] **16. Temporal pipeline** — grouped temporal execution, policy, context, frame orchestration, and internal headers under `pipeline/temporal/`; the graph pipeline target compiled successfully.
- [x] **17. Native effect stack details** — moved the glow/shadow, color, echo, and bulk effect implementations into dedicated `.cpp` translation units with a declaration-only private API; graph node target rebuilt successfully.
- [ ] **18. Backend/context headers** — partial: `render_graph_context.hpp` no longer pulls in `render_backend.hpp`; result/error contracts moved to `render_backend_types.hpp`; native color and bulk-operation parameter layouts now live in `render_backend_surface_types.hpp`; text-run consumers use lighter contract headers and method callers include the full backend API. Explicit native-surface/backend includes were added at their consumers. Graph node/executor and Vulkan targets built. The virtual method API remains together in `render_backend.hpp`; splitting that contract into domain APIs is still pending.

## D. Video export and CLI

- [x] **19. Pipe export core** — session setup, render loop, pipeline warmup/finalization, and sink/FFmpeg support are already split across focused translation units; video export target build passed.
- [x] **20. `NativeAvEncoder::open`** — support, negotiate, probe, and start phases extracted; all open units passed the video export target build (existing FFmpeg deprecation warnings only).
- [x] **21. Video job dispatch** — packet-copy and GOP/direct-mux lanes now live in dedicated translation units; the main dispatcher orchestrates them. CLI render target built and linked successfully.
- [x] **22. Large CLI commands** — verified one registration/entry file per scopes, mask-propagation, saturation-benchmark, and render-plan command; support/detail work is already split into scope analysis, mask frame I/O/video output, and benchmark metrics TUs. `register_render_commands.cpp` is registration-only. CLI render target built and linked successfully.

## E. Media and assets

- [x] **23. SVG loader** — path-data parsing is owned by `SvgImporter`; document subset validation, walking, CSS, gradients, text, and animation are separate focused units. `svg_path_loader.cpp` remains the document orchestration entry. Asset target included in full CLI build.
- [x] **24. Native video frame decoder** — session lifecycle, helpers, decode-at, access decode, and frame decode live in separate translation units. Media target compiled in the full CLI build.

## F. Tests and contracts

- [x] **25. Render Plan tests** — decoder tests are separated into validation/features and numbered parts; prepared-plan coverage is divided into runtime, timeline, Lottie, SVG-filter, and numbered parts with shared local support headers. Existing test sources are organized in CMake manifests.
- [ ] **26. Plan JSON fixtures** — replace giant committed JSON fixtures with deterministic builders/generators.
- [ ] **27. Generated schema and C ABI** — establish a single source and reproducible generation/check flow for the V3 extensions schema and `chronon3d_c.abi`.

## G. Build system

- [x] **28. Root/source CMake** — subsystem source lists and targets are owned by local `src/*/CMakeLists.txt` files and the root includes those subdirectories; the full CLI build configured and linked.
- [x] **29. Architecture rules** — architecture constraints are centralized in `tools/architecture_rules.toml` and the gate manifest; the refactor build passed.
- [x] **30. Test manifests** — test definitions, aggregates, and per-area source lists are split under `tests/manifests/`; `renderer_tests.cmake` delegates to those manifests.

## Verification log

- Render Plan decoder translation units: `-fsyntax-only` passed.
- Render Plan header smoke compile: passed.
- Native AV encoder open translation units: `-fsyntax-only` passed (existing FFmpeg deprecation warnings only).
- Vulkan native shape, plan batch, render graph node splits, and video GOP split: targeted `-fsyntax-only` checks passed.
- Full Render Plan + Vulkan build: `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_render_plan chronon3d_backend_vulkan -j 10` passed (91/91 objects); peak observed compiler RSS was about 4.2 GiB aggregate and the machine retained at least 11 GiB available.
- Vulkan `Impl` fragment and operation TU changes rebuilt successfully with `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_backend_vulkan -j 10` (31 objects after `Impl` changes, then the three extracted operation objects).
- Validator schema projection and dispatcher rebuilt successfully with `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_render_plan -j 10`.
- Compiler entry and request-validation TUs rebuilt successfully with `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_render_plan -j 10`.
- Effect stack detail implementation units and caller rebuilt successfully with `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_graph_nodes -j 10`.
- GPU text cached, packing, and draw units rebuilt successfully with `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_graph_nodes -j 10`.
- Video export integration target built successfully with `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_cli_video_export -j 10` (158/158 objects).
- CLI render target, including packet-copy dispatcher, GOP lane, and command registration, built and linked successfully with `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_cli_render -j 10` (163/163 objects).
- Temporal graph pipeline relocation compiled successfully with `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_graph_pipeline -j 10`.
- Vulkan surface authority explicit-instantiation and implementation TU passed the backend target rebuild (`26/26` changed objects).
- Render graph core rebuilt after removing its unnecessary `render_backend.hpp` include from `render_graph_context.hpp`.
- Render Plan target rebuilt after extracting canonical/standalone composition compilation to `render_plan_compiler_compose.cpp`.
- Render Plan target rebuilt after moving static Lottie expansion behind `lottie_expansion/lottie_expansion.hpp`.
- Graph node target rebuilt after isolating multi-source item/group execution from framebuffer preparation.
- Graph executor target rebuilt after grouping fused-batch and transparent-skip policies under `executor/graph_dispatch/`.
- Graph node and executor targets rebuilt after separating backend result/error types and making backend method dependencies explicit.
- Vulkan backend target rebuilt after extracting native surface-operation parameter layouts into `render_backend_surface_types.hpp` (`38/38` objects, `-j10`).
- Full configured CLI build: `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_cli -j10` passed (296/296 steps). This validated the current Render Plan, Vulkan, Render Graph, runtime, media, CLI, and SDK object graph. Existing FFmpeg `AVCodec::pix_fmts` deprecation warnings remain.
- Full build caught and fixed two split-integration defects: stale runtime includes after moving temporal headers, and the missing `optional_frame` definition after splitting decoder fields. The link also caught and fixed the non-inline header definition of `make_render_backend_error`.
