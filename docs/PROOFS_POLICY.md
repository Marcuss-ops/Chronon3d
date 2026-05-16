# Proofs Policy

This rule determines what can be included in the project's proofs.

## Basic Rule

A proof is added only if it validates a feature that has a real foundation in the project core.

By "real foundation," I mean:

- Public API present in `include/chronon3d/...`
- Real implementation present in `src/...`
- Verifiable behavior without creating a second parallel logic

If something does not have this foundation, it must not become a proof.

## What Can Be Added

- Proofs that test a real feature of the core
- Proofs for camera, layer, mask, text, image, depth, effects, graph, cache
- Small and targeted proofs with a clear technical purpose
- Proofs that serve to prevent regressions

## What Should Not Be Added

- Aesthetic demos with no link to a core feature
- Scenes that duplicate logic already present elsewhere
- Files that introduce a second visual base only for convenience
- Proofs used as a container for new application logic
- "Premium," "style," "theme," or "visual preset" variants if there is no core feature to validate

## Practical Rule

Before adding a proof, ask yourself:

1. Does the feature already exist in the core?
2. Is the proof only verifying it, not reimplementing it?
3. Can I cover the same thing with a smaller test?

If the answer to any of these questions is no, the proof should not be added.

## Where Logic Belongs

- The real logic belongs in `include/chronon3d/...` and `src/...`
- Proofs should only consume that base
- `Operations/` remains a lightweight wrapper, not a development base

## Goal

Fewer proofs, but more useful ones.
Every proof must serve to validate the core, not to create another layer of the project.
