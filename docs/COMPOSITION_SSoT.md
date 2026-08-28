# Composition single source of truth

`chronon3d::Composition` is the sole composition model in the core.

```text
external JSON/IPC/SDK DTO
        ↓ boundary conversion
chronon3d::Composition
        ↓ compile boundary
CompiledComposition
        ↓ frame evaluation
EvaluatedCompositionFrame
```

`CompositionDefinition` is retained only as a deprecated source/ABI boundary
alias for `Composition`; it is not a second storage model. `CompiledComposition`
retains the canonical `Composition` in `composition`. The legacy `definition`
member remains an ABI/source compatibility view and points to the same object.

`CompositionSpec`, `CompositionInput`, `CompositionMetadata`, and
`ResolvedCompositionSpec` are boundary values: they carry metadata or external
request data and must not be persisted as an alternative core composition.
Conversions happen at registry/API/IPC/render-plan boundaries only.
