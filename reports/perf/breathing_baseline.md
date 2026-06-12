# Chronon3D Telemetry Report - run_2fd4b700_d58a5d20

## Overview
| Field | Value |
| --- | --- |
| Composition | MinimalistImageTrackingBreathing |
| Run ID | run_2fd4b700_d58a5d20 |
| Status | SUCCESS |
| Finished At | 2026-06-12T12:17:56Z |
| Output | output/perf/breathing_baseline.mp4 |
| Git Commit | 5ae1f1f6 |
| Build | Release |
| OS / CPU | Linux / Generic CPU (8 cores) |

## Performance
| Metric | Value |
| --- | --- |
| Effective FPS | 32.66 fps |
| Wall Duration | 4.59 s |
| Render Duration | 3.78 s |
| Encoder Close + Flush Duration | 1.31 s |
| Peak Memory | 1.32 GB |
| Node Cache Hit Rate | 96.5% |
| Frames Total | 150 |
| Frames Written | 150 |

## Frame Summary
| Metric | Value |
| --- | --- |
| Frame Count | 150 |
| Average Frame | 23.69 ms |
| Min Frame | 0.02 ms |
| Max Frame | 104.82 ms |
| Output Frame Cache Hit Rate | 100.0% |
| Average Dirty Area Ratio | 15.5% |
| Avg Frame (active) | 29.83 ms (119 frames) |
| Avg Frame (cached) | 0.11 ms (31 frames) |
| Frames Graph Executed | 119 |
| Frames Graph Skipped | 31 |

## Core Render Phases
| Phase | Duration |
| --- | --- |
| chronon_render_loop_ms | 3778.97 ms |
| rendering_loop | 3778.97 ms |
| chronon_render_only_ms | 3553.36 ms |
| chronon_render_pure_ms | 3553.36 ms |
| graph_total_ms | 2966.00 ms |
| graph_execute_ms | 2908.00 ms |
| node_dispatch_ms | 2770.00 ms |
| chronon_writer_encode_ms | 1319.76 ms |
| ffmpeg_encode_total_ms | 1306.32 ms |
| chronon_conversion_copy_ms | 1145.00 ms |
| encoder_close_and_flush | 152.67 ms |
| framebuffer_lifetime_ms | 128.00 ms |
| graph_dirty_rect_ms | 25.00 ms |
| graph_resolve_layers_ms | 23.00 ms |
| cache_eval_ms | 20.00 ms |
| compiled_graph_refresh_ms | 4.00 ms |
| graph_build_ms | 4.00 ms |
| telemetry_emit_ms | 2.00 ms |
| input_resolve_ms | 1.00 ms |
| node_schedule_ms | 1.00 ms |
| chronon_queue_wait_ms | 0.14 ms |

## Phase CPU Efficiency
| Phase | Wall | Pixels | Est. Cores | CPU ms | GB/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| clear_node | 607.00 ms | 87628136 | 1.00 | 607.00 ms | 2.31 |
| transform_node | 1226.58 ms | 50810996 | 1.00 | 1226.58 ms | 0.66 |
| composite_node | 57.00 ms | 49088288 | 1.00 | 57.00 ms | 13.78 |
| frame_conversion | 1145.00 ms | 125310220 | 1.00 | 1145.00 ms | 1.75 |

## Graph Executor Phase Timings
| Phase | Duration |
| --- | --- |
| compiled graph refresh | 4.00 ms |
| cache eval | 20.00 ms |
| dirty eval | 0.00 ms |
| input resolve | 1.00 ms |
| predicted bbox | 0.00 ms |
| clone context | 0.00 ms |
| state assign | 0.00 ms |
| framebuffer lifetime | 128.00 ms |
| node schedule | 1.00 ms |
| node dispatch | 2770.00 ms |
| node execute actual | 0.00 ms |
| node overhead | 0.00 ms |
| telemetry emit | 2.00 ms |

## Telemetry Counters
| Counter | Value |
| --- | --- |
| pixels touched | 125310220 |
| node cache hits | 574 |
| node cache misses | 21 |
| nodes executed | 952 |
| layers rendered | 121 |
| text glyphs rasterized | 0 |
| images sampled | 17 |
| blur pixels | 0 |
| simd lerp calls | 121 |
| tiles total | 0 |
| tiles hit | 0 |
| tiles miss | 0 |
| tiles partial | 0 |
| node cache hash collisions | 0 |
| clear skipped calls | 31 |
| clear skipped pixels | 64281600 |
| clear calls | 138 |
| clear pixels | 87628136 |
| composite calls | 121 |
| composite pixels | 49088288 |
| transform calls | 119 |
| transform pixels | 50810996 |
| effect stack calls | 0 |
| effect pixels | 0 |
| layer culling tests | 0 |
| layers culled | 0 |
| layers visible | 0 |
| framebuffer allocations | 44 |
| framebuffer reuses | 147 |
| framebuffer bytes allocated | 0.00 GB |
| framebuffer bytes peak | 1.26 GB |
| dirty rect count | 240 |
| dirty pixels | 97318024 |
| dirty full fallbacks | 0 |
| framebuffer reuse rate | 77.0% |

## Render
| Metric | Value |
| --- | --- |
| clearnode | 607.00 ms |
| clearnode restore | 0.00 ms |
| video graph eval | 3489.00 ms |

## Framebuffer
| Metric | Value |
| --- | --- |
| framebuffer acquire | 0.00 ms |
| framebuffer clear | 607.00 ms |
| framebuffer pool clear | 0.00 ms |
| framebuffer enqueue | 0.00 ms |
| framebuffer pool exact hit | 140 |
| framebuffer pool empty alloc | 0 |
| framebuffer pool best-fit reuse | 0 |
| framebuffer returned to pool | 113 |

## Framebuffer Pool (runtime snapshot)
| Metric | Value |
| --- | --- |

## Clear
| Metric | Value |
| --- | --- |
| clear calls (graph) | 138 |
| clear pixels (graph) | 87628136 |
| clear skipped calls | 31 |
| clear skipped pixels | 64281600 |

## Conversion / Copy
| Metric | Value |
| --- | --- |
| frame conversion copy | 1145.00 ms |
| video conversion | 1145.00 ms |
| video pipe write | 1306.00 ms |
| unaligned memory copies | 0 |

## Queue
| Metric | Value |
| --- | --- |
| IO queue push blocked | 0.00 ms |
| IO queue pop wait | 3062.00 ms |
| IO writer idle wait | 3062.00 ms |
| IO queue peak depth | 2 frames |
| IO queue peak bytes | 0.00 GB |

## FFmpeg Pipe
| Metric | Value |
| --- | --- |
| ffmpeg pipe write blocked | 1306.00 ms |
| Converted Frame Cache Hits | 30 |
| ffmpeg flush | 0.00 ms |
| video ffmpeg latency | 0.00 ms |
| FFmpeg CPU user | 0% |
| FFmpeg CPU sys | 0% |

## Hot Nodes
| Node | Type | Calls | Total | Avg | Cache Hit Rate | Pixels Touched |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| image_layer_multi | Source | 119 | 882.75 ms | 7.42 ms | 85.7% | 0 |
| Clear | Output | 119 | 667.25 ms | 5.61 ms | 0.0% | 0 |
| Composite | Composite | 119 | 649.20 ms | 5.46 ms | 0.0% | 0 |
| Transform | Transform | 119 | 586.76 ms | 4.93 ms | 0.0% | 0 |
| fill | Source | 119 | 26.67 ms | 0.22 ms | 99.2% | 0 |
| grid_bg | Source | 119 | 6.54 ms | 0.05 ms | 99.2% | 0 |
| ClearNode clear | Overhead | 1 | 607.00 ms | 607.00 ms | — | 0 |
| ClearNode restore | Overhead | 1 | 607.00 ms | 607.00 ms | — | 0 |
| Composite acquire | Overhead | 1 | 463.00 ms | 463.00 ms | — | 0 |
| Composite blend | Overhead | 1 | 57.00 ms | 57.00 ms | — | 0 |
| Composite internal (non-blend) | Overhead | 1 | 529.85 ms | 529.85 ms | — | 0 |
| Frame conversion copy | Overhead | 1 | 1145.00 ms | 1145.00 ms | — | 0 |
| Video pipe write | Overhead | 1 | 1306.00 ms | 1306.00 ms | — | 0 |

**Hot Nodes Coverage:** 2819.00 ms / 3025.00 ms (93.2%) — phase breakdown adds 4714.00 ms in sub-costs

## Hot Work Attribution
| Work | Wall | Pixels | Est. Cores | CPU ms | GB/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| ClearNode clear | 607.00 ms | 87628136 | 1.00 | 607.00 ms | 2.31 |
| ClearNode restore | 607.00 ms | 0 | 1.00 | 607.00 ms | 0.00 |
| Composite blend | 57.00 ms | 49088288 | 1.00 | 57.00 ms | 13.78 |
| Transform rows | 1226.00 ms | 50810996 | 1.00 | 1226.00 ms | 0.66 |
| Frame conversion | 1145.00 ms | 125310220 | 1.00 | 1145.00 ms | 1.75 |
| Pipe write | 1306.00 ms | 125310220 | 1.00 | 1306.00 ms | blocked |

**Attribution coverage:** 4948.00 ms / 3025.00 ms (163.6%)

## Layer Cost Breakdown
| Layer | Type | Calls | Time | Visible Pixels | Dirty Pixels | Glyphs | Images |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Composite | Composite | 119 | 629.08 ms | 48794344 | 0 | 0 | 0 |
| Composite | Composite | 119 | 4.96 ms | 245114076 | 0 | 0 | 0 |
| Composite | Composite | 119 | 0.54 ms | 245114076 | 0 | 0 | 0 |

## Frame Samples
| Frame | Duration | Cache | Dirty Ratio | Dirty Enabled | Dirty Rect | Tile | Fast Path | Graph Reused |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| #0 | 0.11 ms | hit | 0.0% | no | - | no | yes | no |
| #1 | 79.93 ms | hit | 20.7% | yes | [529,291→1391,789] | no | no | yes |
| #2 | 101.39 ms | hit | 20.7% | yes | [529,291→1391,789] | no | no | yes |
| #3 | 13.65 ms | hit | 20.7% | yes | [530,291→1390,789] | no | no | yes |
| #4 | 18.66 ms | hit | 20.6% | yes | [530,292→1390,788] | no | no | yes |
| #5 | 15.07 ms | hit | 20.5% | yes | [531,292→1389,788] | no | no | yes |
| #6 | 15.85 ms | hit | 20.5% | yes | [531,292→1389,788] | no | no | yes |
| #7 | 20.80 ms | hit | 20.5% | yes | [531,292→1389,788] | no | no | yes |
| #8 | 14.47 ms | hit | 20.4% | yes | [532,293→1388,787] | no | no | yes |
| #9 | 12.63 ms | hit | 20.4% | yes | [532,293→1388,787] | no | no | yes |
| #10 | 44.79 ms | hit | 20.4% | yes | [532,293→1388,787] | no | no | yes |
| #11 | 11.92 ms | hit | 20.3% | yes | [533,293→1387,787] | no | no | yes |

## Cache Diagnostics
- hit: 338
- bypass_not_cacheable: 238
- bypass_frame_dependent: 119
- miss: 19

## Setup Deep Dive
*Note: in chunked export mode, per-worker setup costs are summed in the aggregate.*

| Sub-Phase | Duration |
| --- | --- |
| Graph parsing | 0.00 ms |
| Asset I/O load | 0.00 ms |
| Pool preallocation | 0.00 ms |
| Image decode | 0.00 ms |

## Parallelism Decisions
| Decision | Count |
| --- | ---: |
| skipped_clear_small | 0 |
| skipped_composite_small | 0 |
| skipped_encoder_backpressure | 0 |
| skipped_transform_small | 0 |
| used_parallel_clear | 0 |
| used_parallel_composite | 124 |
| used_parallel_transform | 120 |
| level_parallel_count | 480 |
| level_sequential_count | 0 |
| parallel_regions_skipped_small_level | 0 |

**Bottleneck classification:** Encoder/pipe bottleneck — ffmpeg write blocked for 1306.00 ms (35% of render time).

## System Resources & Utilization
| Metric | Value |
| --- | --- |
| Logical cores | 8 |
| RAM total | 22951 MB |
| RAM available (minimum during run) | 16613 MB |
| Process RSS peak | 921 MB |
| Process CPU user | 10570.00 ms |
| Process CPU sys | 4420.00 ms |
| Process CPU total | 14990.00 ms |
| Effective cores used | 3.26 / 8 |
| CPU utilization | 40.8% |
| TBB arena max concurrency | 8 |
| TBB active workers peak | 8 workers |
| Parallel regions executed | 480 |
| Parallel regions skipped (≤1 node) | 0 |

## Bottleneck Diagnosis
| Area | Status |
| --- | --- |
| CPU saturation | MODERATE (40.8% of 8 cores) |
| Memory pressure | SAFE (921 MB RSS / 16613 MB avail) |
| Encoder pipe | MAJOR BOTTLENECK (35% of render time blocked) |
| Graph parallelism | STRONG (480 parallel / 0 sequential levels) |
| Node parallelism | STRONG (peak: 8 workers) |
| Frame conversion | MAJOR BOTTLENECK (30% of render time) |
| Cache efficiency | EXCELLENT (96.5%) |
| Dirty rect efficiency | MODERATE (77.7% dirty) |
| Hot attribution | EXCELLENT (164% explained) |

**Primary bottleneck:** Encoder pipe (FFmpeg subprocess).

**Recommendation:** Increase encoder queue depth or use native encoder path.

## OS & Process Diagnostics
| Metric | Value |
| --- | --- |
| Context switches (voluntary) | 0 |
| Context switches (involuntary) | 0 |
| Page faults (major) | 0 |
| Page faults (minor) | 362264 |
| LLC references | 0 |
| LLC misses | 0 |

## Cache Architecture
Chronon uses three separate caching layers with distinct roles:

| Cache Layer | Measures | Reported As |
| --- | --- | --- |
| **Node Cache** | Per-render-node hit rate. Each render node (Clear, Composite, grid_bg, etc.) is checked against a content-addressable cache before execution. | `Node Cache Hit Rate`, `node cache hits/misses` |
| **Output Frame Cache** | Whether the entire rendered output frame was reused from a previous frame. High on static scenes, drops when content changes. | `Output Frame Cache Hit Rate`, Frame Samples `hit/miss` |
| **Converted Frame Cache** | YUV conversion cache for the video encoder. Avoids re-converting RGBA->YUV when the same frame is sent multiple times. | `converted frame cache hits` (FFmpeg Pipe section) |

**Important:** A "Node Cache Hit Rate" of 0% with "Output Frame Cache Hit Rate" of 100% is **not a contradiction**.
The output frame cache operates at a higher level: if the entire scene is unchanged between frames, the output is reused
without executing individual render nodes at all. Conversely, per-node counters (composite_calls, nodes_executed, etc.)
may show 0 because the node telemetry count via `RenderCounters` atomics operates independently from per-node event logging.
The Hot Nodes and Layer Cost Breakdown sections below draw from per-event logs and give a more precise picture.

## Correctness Verification
| Check | Status |
| --- | --- |
| Pixel NaN/Inf | Not checked inline -- run `chronon3d_breathing_golden_tests` to verify |
| Golden comparison | Execute `chronon3d_breathing_golden_tests` (separate binary) |
| Determinism | Hash-identical across runs (verified via `chronon3d_determinism_test`) |

**Note:** Correctness verification requires running the dedicated golden test binary.
Use `ctest --test-dir build -R breathing_golden` or run the binary directly.

## Things to Know
- Node Cache hit rate: 96.5%. 
- Average dirty area ratio: 15.5% of the frame.
- Dirty pixels as share of touched pixels: 77.7%.
- Dirty full fallbacks: 0.
- Framebuffer reuse rate: 77.0%. 
- If render time stays high while cache hit rate is strong, the hot path is likely compositing, clear passes, or framebuffer churn rather than rasterization.
- If text glyph rasterization is low, text is probably not the main bottleneck anymore; blur/glow and layer recomposition become the next suspects.

