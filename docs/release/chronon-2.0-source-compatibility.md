# Chronon 2.0 source compatibility matrix

This records the currently inspected source pairing for the producer, template
catalogue and renderer. It is an integration matrix, not a release freeze or
a claim that CI ran for these revisions.

| Component | Revision inspected | Boundary and expected contract | Local evidence |
|---|---|---|---|
| C3D / Chronon3d | `2e3cb0795e94f363119ddfda42bbeca5b25f6423` | Renderer/runtime; CMake project `0.1.0`, public C ABI `2`, RenderPlan `chronon.render-plan.v3` | Focused renderer and C ABI tests recorded in the P0 evidence files |
| CM3D / ChrononMotion3d | `2ba2a57bcb40e184900beaaf15d577216eed3c76` | Producer emits RenderPlan V3; optional adapter consumes C3D public headers | Producer adapter and 386 producer checks were run locally against the inspected C3D checkout; no same-SHA CI receipt |
| ChrononTemplate | `77f1850e6e408d0d03a419396c37439597bbdc2f` | Template recipes consume ChrononMotion and validate/render through C3D CLI/public headers; live adapter test is optional | Source wiring inspected in `CMakeLists.txt`; no same-SHA CI receipt |

## Compatibility rules

- CM3D is the owner of authored time, hierarchy, rig and camera. It serializes
  those decisions into RenderPlan V3; C3D owns execution and pixels.
- Template emits the same RenderPlan V3 contract and does not own a renderer
  or text layout implementation.
- A producer revision is compatible only when its emitted plans pass the C3D
  V3 decoder/validator and the focused consumer fixtures for the feature being
  changed.
- ABI `2` describes the C-facing SDK boundary; it is not a substitute for the
  RenderPlan schema version.

The revisions above are a local inspected pairing, not a supported-version
range. CI same-SHA results and a release compatibility range remain separate
gates.
