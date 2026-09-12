# Same-SHA CI pre-execution diagnostic — `a16c86de2`

## Verdict

BLOCKED before runner acquisition. This is not a production-code or test
failure.

## Observed GitHub state

- Repository: `Marcuss-ops/Chronon3d`
- Commit: `a16c86de2178270852075526a0dd27b96721a5d1`
- `Chronon CI` run: `34642652573`
- `Chronon Nightly` run: `34642652602`
- `actions/permissions`: `enabled=true`, `allowed_actions=all`
- Every non-skipped job was created with `runner_id=0`,
  `runner_group_id=0`, an empty `runner_name`, and `steps=[]`.
- Jobs started and completed between `20:08:50Z` and `20:08:57Z`; the run
  logs ZIP is empty.
- The same symptom affects Linux, Windows, build, test, sanitizer, native
  video, and documentation jobs, so it is independent of a runner label or a
  repository command.

The repository has no self-hosted runners registered, which is expected for
the current `ubuntu-latest`/`windows-latest` hosted labels and does not explain
the all-workflow failure.

## Local workflow validation

- `actionlint v1.7.12`: no findings for `.github/workflows/*.yml`.
- YAML parse/shape validation: all 14 workflow files valid; all contain a
  `jobs` map.
- No workflow or production-code change is justified by this evidence.

## Exit gate

Re-run the same SHA after the GitHub Actions dispatch/runner service is
available. The gate is satisfied only when a job has `runner_id > 0`,
non-empty steps, and the required same-SHA checks are green.
