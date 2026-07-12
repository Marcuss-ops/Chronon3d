# Chronon3D — Working Build Host (WBH) Cert Sequence Protocol

> **Scope:** unified WBH execution protocol for the 5 remaining First-Principles Product Check tests (Test #4 Determinism + Test #8 Text V1 cert Step 8+9 + Test #9 Text V1 cert Step 11 finale + Test #13 resource limits + Test #14 Tracking) + the 11/11 active sections machine-verification. Per AGENTS.md §honest-limitation, this protocol runs on a working build host (vcpkg installed + chronon3d_cli built in canonical `linux-dashboard-dev` + rot-cascade resolved), NOT on the canonical env-blocked VPS. ALL 6 steps are mandatory; the protocol fails if any step is missing the canonical FAIL-LOUD contract.
>
> **Status (2026-07-12):** OPEN/DEFERRED — env-block on VPS per TICKET-BUILD-ROT-CASCADE-CAMERA 409-error rot + TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV glm/magic_enum missing. Canonical binary `build/chronon/linux-dashboard-dev/apps/chronon3d_cli/chronon3d_cli` MISSING on this VPS; alt binary `build/chronon/linux-fast-dev-cond/apps/chronon3d_cli/chronon3d_cli` (610 MB, executable) lacks the user-spec subcommands. See `docs/CURRENT_STATUS.md` §Stato per area per the per-test DEFERRED state.
>
> **Tickets consolidated by this protocol:** TICKET-CERT-STEP10-NEGATIVE-FONT-REBUILD-WBH (STEP 1 NOW UNBLOCKED per TICKET-DOCTEST-DISCOVER-TESTS-ROT-FIX) + TICKET-CERT-STEP8-9-WBH-VERIFY + TICKET-DETERMINISM-VP2-EXTENSION + TICKET-DOCTEST-DISCOVER-TESTS-TIMEOUT-RECOVERY (recovery test for per-TEST_CASE TIMEOUT 30/120 from the STEP 1 rot-fix) + 4 docs-only cat-5 chores (e45ca40b, 476327b2, 6e15de31, 2f842e1d) + the 2 chore commits just landed (b383eb1d selftest defensive fix + 88ad0d9 5-file rot-fix). See `docs/FOLLOWUP_TICKETS.md` §Open Blockers + §Recently Closed for the per-ticket row states.
>
> **Cross-references:** AGENTS.md §honest-limitation + §Cat-3 (zero new public SDK API) + TICKET-GATE-SUBJECT-RANGE closure 2026-07-12 (subject envelope pattern) + the canonical `tools/check_first_principles_fail_loud.sh` (Test #7 FAIL-LOUD canonical gate) for the `[INFO] ${GATE_NAME}: ...` addizionale pattern.

## Pre-conditions (run on WBH; ALL must PASS before continuing)

### Pre-0: vcpkg bootstrap + glm/magic_enum/doctest install

```bash
# 0a. apt install ffmpeg + GNU time + PIL + jq + python3-yaml (per Test #11 + #8 dependencies)
apt install ffmpeg time python3-pil jq python3-yaml

# 0b. vcpkg bootstrap
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
~/vcpkg/vcpkg install glm magic_enum doctest
export CMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake
# verify: glm + magic_enum + doctest installed
ls ~/vcpkg/installed/x64-linux/include/glm/  # expect: glm.hpp
ls ~/vcpkg/installed/x64-linux/include/magic_enum/  # expect: magic_enum.hpp
ls ~/vcpkg/installed/x64-linux/include/doctest/  # expect: doctest.h
```

### Pre-1: fetch + checkout main + rebase

```bash
cd /path/to/Chronon3d
git fetch origin
git checkout main
git pull --ff-only origin main
git log -n 5 --oneline   # verify the 2 chore commits (b383eb1d + 88ad0d9) are present
git config --local --get branch.main.rebase   # must be: true
```

### Pre-2: cmake configure (the STEP that was BLOCKED on VPS by the doctest_discover_tests rot — should now succeed)

```bash
cmake --preset linux-content-dev
# expect: "Build files have been written to: build/chronon/linux-content-dev" + exit 0
# the rot-fix in 88ad0d9 unblocks this step
```

### Pre-3: build chronon3d_cli + chronon3d_sabotage_tests + chronon3d_text_golden_tests

```bash
cmake --build build/chronon/linux-content-dev --target chronon3d_cli chronon3d_sabotage_tests chronon3d_text_golden_tests --parallel "$(nproc)"
# expect: exit 0 + 3 binaries at:
#   build/chronon/linux-content-dev/apps/chronon3d_cli/chronon3d_cli
#   build/chronon/linux-content-dev/tests/chronon3d_sabotage_tests
#   build/chronon/linux-content-dev/tests/chronon3d_text_golden_tests
```

### Pre-4: rot-cascade resolution (per TICKET-BUILD-ROT-CASCADE-CAMERA)

Per the prior session's diagnostic, the WBH must have the rot-cascade resolved (TICKET-BUILD-ROT-CASCADE-CAMERA + TICKET-CONTENT-TEXT-CAMERA-V1-ROT + TICKET-TEXT-LEGACY-POSITION-ROT) BEFORE running the cert sequence. The canonical verification command:

```bash
cmake --build .tmp/chronon-builds/linux-fast-dev --target chronon3d_dev_fast 2>&1 | tee /tmp/host-build-df1e09d9-v3.log
# expect: 0 errors remaining (was 409 in prior session; rot-fix in 75d6e66b reduced to 408)
# if errors remain: per the rot-cascade baseline doc, apply the per-file matrix
```

## Step 1 — Test #13 (resource limits, after the 6th file + rot-fix commits)

### 1a. smoke-test the 6th file selftest (verify the WORK dir + eval/bash fix from b383eb1d works)

```bash
bash tests/tools/selftest_check_resource_limits.sh
# expect: 4/4 scenarios PASS (PASS_happy + FAIL_oggi + FAIL_finale + PRECOND_NO_PYTHON verified pre-rotation;
#         the path-mismatch fix from 88ad0d9 ensures the run_scenario first-args match the gate's expected paths)
# if FAIL: this is the cache-ratio rot documented in the test history — needs a separate fix-forward commit
#          (the pre-existing gate rot at tools/check_resource_limits.sh LEAK-phase cache ratio)
```

### 1b. run the actual gate

```bash
bash tools/check_resource_limits.sh
# expect: GATE_PASS + [INFO] check_resource_limits: ... per AGENTS.md INFO-level diagnostic style
# if FAIL: GATE_FAIL with the specific envelope that breached
```

### 1c. wire the gate into the orchestrator (Test #13 section)

```bash
# EDIT tools/first_principles_product_check.sh: replace the `== Resource limits ==` TODO stub
# with the canonical 2-line wireup:
#   echo "== Resource limits ==       "  # Test #13
#   bash "$SCRIPT_DIR/check_resource_limits.sh"
# Bump the trailing [INFO] summary line from 10/10 → 11/11 active sections wired
```

### 1d. cat-5 ticket transitions

- TICKET-CERT-STEP10-NEGATIVE-FONT-REBUILD-WBH → DONE (after 1a + 1b PASS)
- TICKET-CERT-SEQUENCE-WBH-PROTOCOL (this protocol) → PARTIAL (Step 1 done; Steps 2-6 pending)
- TICKET-DOCTEST-DISCOVER-TESTS-TIMEOUT-RECOVERY → forward-point decision per Step 2 ctest timing

## Step 2 — Test #4 (Determinism)

### 2a. run the determinism ctest surface

```bash
ctest --test-dir build/chronon/linux-content-dev -R 'Determinism' --output-on-failure
# expect: 16 tests / 5x5 matrix (P2-C coverage) PASS per `tests/deterministic_tests.cmake`
```

### 2b. run the brute-determinism 60-frame × threads {1,2,8} × {cold,warm} matrix

```bash
ctest --test-dir build/chronon/linux-content-dev -R 'brute_determinism' --output-on-failure
# expect: 20x{1,2,8}threads x {cold,warm}cache x 60-frame all PASS (TICKET-DETERMINISM-BRUTE-17)
```

### 2c. spec-variant decision (TICKET-DETERMINISM-VP2-EXTENSION)

```bash
# Per the prior session's §honest SPEC-VARIANT finding:
#   user spec centroid (600..1320, 300..780) is LOOSER than prior DoD §9 lock (|cx-960|<110, |cy-540|<110)
# The spec-variant DECISION is future-ADR-gated per TICKET-DETERMINISM-VP2-EXTENSION
# On the WBH, surface the spec-variant finding to the user + capture the chosen direction
```

## Step 3 — Test #8 (Text V1 cert Step 8+9)

### 3a. precondition: ffmpeg + ffprobe functional

```bash
bash tools/check_ffmpeg_required.sh
# expect: GATE_PASS + [INFO] check_ffmpeg_required: ffmpeg+ffprobe present at <version>
```

### 3b. PIL counter-test for ae_08_glow_pulse --frame 15

```bash
chronon3d_cli --version  # verify the canonical binary
chronon3d_cli list | grep ae_08_glow_pulse  # verify composition is registered
chronon3d_cli video ae_08_glow_pulse --frame 15 -o /tmp/ae_08_text_check.png
```

### 3c. Python PIL assertion against user-spec bounds

```bash
python3 -c "
from PIL import Image
img = Image.open('/tmp/ae_08_text_check.png')
w, h = img.size
print(f'img={w}x{h}')
assert w >= 800, f'bw={w} < 800 (spec)'
assert h >= 100, f'bh={h} < 100 (spec)'
print('PIL bounds check PASS')
"
```

### 3d. 5-render loop in /tmp/chronon-text-determinism/ + sha256 UNIQUE==1

```bash
mkdir -p /tmp/chronon-text-determinism
for i in 1 2 3 4 5; do
  chronon3d_cli video ae_08_glow_pulse --frame 15 -o /tmp/chronon-text-determinism/render_${i}.png
done
sha256sum /tmp/chronon-text-determinism/render_*.png | awk '{print $1}' | sort -u | wc -l
# expect: 1 (UNIQUE==1, determinism)
```

### 3e. threads {1, 8} paired render + sha256 equality

```bash
CHRONON3D_THREADS=1 chronon3d_cli video ae_08_glow_pulse --frame 15 -o /tmp/chronon-text-determinism/t1.png
CHRONON3D_THREADS=8 chronon3d_cli video ae_08_glow_pulse --frame 15 -o /tmp/chronon-text-determinism/t8.png
[ "$(sha256sum /tmp/chronon-text-determinism/t1.png | awk '{print $1}')" = "$(sha256sum /tmp/chronon-text-determinism/t8.png | awk '{print $1}')" ] && echo 'threads-determinism PASS' || echo 'threads-determinism FAIL'
```

## Step 4 — Test #9 (Text V1 cert Step 11 finale)

```bash
ctest --test-dir build/chronon/linux-content-dev --output-on-failure -R 'TextVisibleInk|TextNoClip|TextClipBounds|TextCompleteness|TextAlign|TextWrapping|TextUnicode|TextGoldenGaps|TextStyleProps|TextTypewriter|TextDeterminism|TextLongText|TextEdgeCases|TextAntiFalseGreen|TextGlowSmoke|TextRotateZ|TextTransforms'
# expect: 17 patterns / 21 sub-cases PASS (TICKET-TEXT-V1-CERT-STEP11-DEFERRED → DONE)
# 10/10 zero-conditions satisfied: 0 fail + 0 Not Run + 0 missing goldens + 0 unresolved fonts +
#   0 glyph_count=0 + 0 empty alpha bbox + 0 involuntary clipping + 0 edge-touching +
#   0 render-repeat diff + 0 thread-diff + inspect-text PASS
```

## Step 5 — Test #14 (Tracking)

```bash
# The 21 ctest entries from Step 4 cover most of the text-tracking surface
# Additional tracking-specific tests:
ctest --test-dir build/chronon/linux-content-dev -R 'TextTrk|Tracking' --output-on-failure
# expect: tracking tests PASS
```

## Step 6 — 11/11 active sections (orchestrator end-to-end)

```bash
bash tools/first_principles_product_check.sh
# expect: FIRST_PRINCIPLES_PRODUCT_PASS exit 0 + [INFO] line shows 11/11 active sections wired
# if 10/11: Test #13 still TODO stub — needs orchestrator wireup commit (Step 1c above, this protocol)
```

## Post-protocol — cat-5 ticket transitions on WBH

| Ticket | Transition | Trigger |
|--------|-----------|---------|
| TICKET-CERT-STEP10-NEGATIVE-FONT-REBUILD-WBH | → DONE | After Step 1b PASS |
| TICKET-CERT-SEQUENCE-WBH-PROTOCOL | → DONE | After Step 6 PASS (11/11) |
| TICKET-TEXT-V1-CERT-STEP11-DEFERRED | → DONE | After Step 4 PASS (21/21 ctest) |
| TICKET-CERT-STEP8-9-WBH-VERIFY | → DONE | After Step 3 PASS (3-block spec) |
| TICKET-DETERMINISM-VP2-EXTENSION | ADR + DONE | After Step 2c spec-variant decision |
| TICKET-DOCTEST-DISCOVER-TESTS-TIMEOUT-RECOVERY | ADR + DONE | Per Step 2 ctest timing analysis |
| TICKET-TEXT-V1-CERT-STEP8-9-DEFERRED | → DONE | After Step 3 PASS |
| TICKET-TEXT-V1-CERT-STEP7-INSPECT-TEXT-DEFERRED | → DONE | After Step 4 PASS (inspect-text JSON contract verified) |
| TICKET-COMMAND-INSPECT-TEXT-OVERRIDE-CONSOLIDATION | → ADR-decided + DONE | After Step 4 PASS (BSOT-remove OR belt-and-suspenders per ADR) |
| TICKET-TEXT-V1-CERT-STEP10-NEGATIVE-FONT | → DONE | After Step 1b PASS (cat-1 source verified via real FontEngine) |
| TICKET-TEXT-LEGACY-POSITION-ROT | → DONE | After Step 4 PASS (all 200+ sites compile) |
| TICKET-BUILD-ROT-CASCADE-CAMERA | → DONE | After Pre-4 PASS (0 compile errors) |
| TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV | → DONE | After Pre-0 PASS (glm + magic_enum installed) |

## Expected WBH log shape (forensic traceability)

After ALL 6 steps PASS on WBH, the post-protocol cat-5 chore commit should:
1. Update `docs/FOLLOWUP_TICKETS.md` (NEW `TICKET-CERT-SEQUENCE-WBH-PROTOCOL DONE` row in §Recently Closed + 12 ticket rows moved from §Open Blockers → §Recently Closed per the table above)
2. Update `docs/CURRENT_STATUS.md` (11/11 active sections verified; Test #1-#20 all PASS; baseline verde)
3. Update `docs/CHANGELOG.md` (prepend a `docs(cert): WBH cert sequence machine-verified (11/11 active sections)` entry)
4. Create `docs/baselines/main-<sha>-cert-baseline.md` (immutable per AGENTS.md §docs governance)
5. Push to origin/main with SHA-triple selfcheck per the post-push SHA-selfcheck invariant

## Notes

- **Test #13 has a pre-existing gate rot** (cache ratio = 9999 when `cache_after@1 = 0`). The 6th file commit + .cmake rot-fix do NOT unblock this gate rot. If Step 1a/1b fail with the cache ratio 9999, the WBH session should open a fix-forward chore: add a `b == 0 ? 0 : ...` division-by-zero guard in `tools/check_resource_limits.sh` LEAK-phase awk script. This is a separate ticket and NOT in scope for the protocol.
- **Test #8 spec-variant**: the user-spec centroid envelope (600..1320, 300..780) is LOOSER than the prior DoD §9 regression lock (`|cx-960| < 110, |cy-540| < 110`). The spec-variant DECISION is ADR-gated per TICKET-DETERMINISM-VP2-EXTENSION.
- **Test #9 requires the canonical CLI binary** with the `video` subcommand + `ae_08_glow_pulse` composition registered. On WBH with `CHRONON3D_BUILD_CONTENT=ON`, this should be the default.
- **Test #14 (Tracking)** has minimal coverage beyond what Test #4 + Test #9 provide. The TICKET-TRACKING-WORD-CASCADE forward-point (TICKET-VIDEO-COMPLETENESS-MATRIX sub-§11) is the dedicated scope.

## Versioning

- v0.1 (2026-07-12, this document): initial protocol drafted from the user click of the "Continue the Text V1 cert sequence" followup; per ask_user Option "Save WBH protocol to docs/" authorization.
- Future: v0.2 will add sub-§5+§6 video-completeness matrix + §15 portrait safe-area coverage.
