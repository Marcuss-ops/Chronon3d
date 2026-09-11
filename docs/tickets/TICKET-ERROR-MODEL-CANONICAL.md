# TICKET-ERROR-MODEL-CANONICAL — canonical error model

**Status:** ACTIVE — Phase 1 checkpoint
**Owner:** Runtime and render-graph maintainers

## Scope

Establish one `chronon::ErrorCategory` taxonomy and one layer-responsibility
contract before the prepared execution plan migration.

## Demolition-debt card

| Legacy enum | Owner | Reason retained | Exit condition | Equivalence test/gate | Removal scope | Status |
|---|---|---|---|---|---|---|
| `chronon3d::sdk::RenderErrorCode` | SDK | Public ABI compatibility | zero direct constructions outside SDK adapter | SDK error formatter + API propagation tests | legacy SDK enum after ABI migration | ACTIVE |
| `chronon3d::render::RenderDiagnosticCode` | Render | Diagnostic-only compatibility | zero direct constructions outside diagnostic adapter | diagnostic serialization tests | diagnostic enum/adapter | ACTIVE |
| `chronon3d::graph::RenderBackendErrorCode` | Render graph | Backend detail payload | zero direct constructions outside backend adapter | backend failure → frame failure test | backend detail enum | ACTIVE |
| `chronon3d::graph::BackendResolveErrorCode` | Backend registry | Resolver detail payload | zero direct constructions outside resolver adapter | backend resolver matrix | resolver detail enum | ACTIVE |
| `chronon3d::compiler::CompileErrorCode` | Compiler | Compiler diagnostics | zero direct constructions outside compiler adapter | compiler invalid-plan corpus | compiler detail enum | ACTIVE |
| `chronon3d::RenderJobErrorCode` | Timeline | Job API compatibility | zero direct constructions outside API adapter | render-job API error contract | public legacy job enum | ACTIVE |
| `chronon3d::runtime::job::RenderJobRequestErrorCode` | Job API | Request validation detail | zero direct constructions outside request adapter | request validation tests | request detail enum | ACTIVE |
| `chronon3d::graph::ScopeErrorCode` | Executor | Scope diagnostics | zero direct constructions outside scope adapter | scope validation tests | scope detail enum | ACTIVE |
| `chronon3d::TextErrorCode` | Text | Text domain detail | zero direct constructions outside text adapter | text error corpus | text detail enum | ACTIVE |
| `chronon3d::TextLayoutErrorKind` | Text | Layout detail | zero direct constructions outside text adapter | layout failure tests | layout detail enum | ACTIVE |
| `chronon3d::CameraErrorCode` | Camera | Camera detail | zero direct constructions outside camera adapter | camera failure tests | camera detail enum | ACTIVE |
| `chronon3d::CameraCompileErrorCode` | Camera/compiler | Compile detail | zero direct constructions outside camera adapter | camera compile tests | camera detail enum | ACTIVE |
| `chronon3d::presets::MotionErrorCode` | Presets | Preset lookup detail | zero direct constructions outside preset adapter | preset error tests | motion detail enum | ACTIVE |
| `chronon3d::media::video::MediaProbeErrorCode` | Media | Probe detail | zero direct constructions outside media adapter | media probe tests | probe detail enum | ACTIVE |
| `chronon3d::media::video::VideoSinkError` | Media | Sink detail | zero direct constructions outside sink adapter | sink lifecycle tests | sink detail enum | ACTIVE |
| `chronon3d::media::ConversionError` | Media | Conversion detail | zero direct constructions outside conversion adapter | conversion tests | conversion detail enum | ACTIVE |
| `chronon3d::trace::TraceError` | Trace | Trace detail | zero direct constructions outside trace adapter | trace contract tests | trace detail enum | ACTIVE |
| `chronon3d::runtime::ExecutionFailure` | Executor | Frame compatibility ABI | zero direct constructions outside frame adapter | backend failure propagation | execution detail enum | ACTIVE |
| `chronon3d::media::DecodeFailureReason` | Media | Decode detail | zero direct constructions outside decode adapter | decode failure tests | decode detail enum | ACTIVE |
| `chronon3d::PropsErrorReason` | Composition | Props validation detail | zero direct constructions outside props adapter | props validation tests | props detail enum | ACTIVE |
| `chronon3d::render_plan::ValidationIssueKind` | Plan | Validation detail | zero direct constructions outside plan adapter | render-plan validation corpus | validation detail enum | ACTIVE |
| `chronon3d::runtime::RuntimeBuildError::Code` | Runtime | Build detail | zero direct constructions outside runtime adapter | runtime build tests | build detail enum | ACTIVE |
| `chronon3d::runtime::PreparationError::Code` | Runtime | Preparation detail | zero direct constructions outside preparation adapter | preparation tests | preparation detail enum | ACTIVE |
| `chronon3d::assets::AssetPreflightErrorCode` | Assets | Asset preflight detail | zero direct constructions outside asset adapter | asset preflight tests | preflight detail enum | ACTIVE |
| `chronon3d::assets::MeshLoadErrorCode` | Assets | Mesh detail | zero direct constructions outside mesh adapter | mesh load tests | mesh detail enum | ACTIVE |
| `chronon3d::scene::CameraFailurePolicy` | Scene | Policy detail | zero direct constructions outside scene adapter | camera policy tests | policy enum | ACTIVE |
| `chronon3d::graph::FrameExecutionPath` | Executor | Path detail | zero direct constructions outside executor adapter | execution-path tests | path enum | ACTIVE |
| `chronon3d::cache::CacheRemovalReason` | Cache | Eviction detail | zero direct constructions outside cache adapter | cache eviction tests | cache detail enum | ACTIVE |
| `chronon3d::runtime::AsyncEncoderSinkState` | Encoder | Sink state detail | zero direct constructions outside encoder adapter | encoder lifecycle tests | sink state enum | ACTIVE |
| `chronon3d::runtime::ResourcePreparationError` | Runtime | Resource detail | zero direct constructions outside resource adapter | resource preparation tests | resource detail enum | ACTIVE |
| `chronon3d::runtime::GpuMemoryPressure` | Runtime | Memory pressure detail | zero direct constructions outside memory adapter | pressure/device-loss tests | memory detail enum | ACTIVE |

## Exit gate

- Architecture gate rejects direct `chronon::Error{}` construction outside the
  canonical header and rejects production `catch (...) { continue; }` paths.
- Backend failure propagation reaches `JobSession::Failed` and the API keeps
  the structured payload.
- Cancellation and device loss use the same taxonomy.
- Native→pipe downgrade is never silent: the final resolver decision carries
  a reason and increments its downgrade counter; the render receipt now
  persists both fields.
- Every row above reaches zero direct constructions outside its adapter before
  the enum is deleted.
