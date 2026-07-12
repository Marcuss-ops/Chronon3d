# Test 15 — Product One-Pager

> **Audience**: non-developer (marketer, founder, content lead, designer)
> **Goal (PASS)**: the value is evident without explaining the architecture
> **No** repository paths · **No** architecture diagrams · **No** SDK terms · **No** C++ / OMP / CMake references

---

## What we want to make

> A 5-second cinematic glowing intro clip with a kinetic title + soft fade, suitable for a brand launch reel.

| | **The cloud AI video service you tried last week** | **Chronon3D** |
|---|---|---|
| 🟡 **INPUT**<br>(what you write down) | A prompt like:<br>_"a cinematic glowing intro, kinetic title, soft fade, suitable for a brand launch reel"_ + 3–5 tweaks after the first result | The same brief, written ONCE |
| 🟢 **COMMAND**<br>(what you actually type) | Open browser → log in → paste prompt → click "Generate" → wait 4 min → preview → tweak → click "Generate" again (and again) → download | Type one line:<br>`chronon3d_cli render ProductLaunch --props promo.json`<br>+ press Enter |
| 🎬 **VIDEO FINALE**<br>(what you actually get to publish) | "Close enough" after 3 rounds + 11 min of credit. The glow drifts, you settle for it. | The clip you wrote the brief for.<br>Deterministic; re-running gives the same bytes. |
| ⏱️ **TEMPO**<br>(wall time from idea to publishable file) | **11 min** wall + **11 min** waiting = ~22 min | **~2 min** wall incl. CLI startup + telemetry write |
| 💶 **COSTO**<br>(who pays for this single video) | **$0.45** cloud GPU credit + your **$0.42** monthly subscription slice ≈ **$0.87** | **$0.05** cloud GPU (one-shot) or **$0.00** if you run it on your laptop |
| | **Total: $0.87 / video, 22 min / video** | **Total: $0.05 / video, 2 min / video (~$0.15 / hour of editorial throughput)** |

---

## What "the value is evident" looks like in practice

> Pretend you typed both into a Monday morning Slack channel:
>
> **Manager** → "We need a launch teaser by EOD. Options?"
> **You** → (paste the 5-element table above, no further explanation)
> **Non-dev manager** → "Use whatever is faster, we have 4 more of these this week."
> **You** → (use Chronon3D; ship at 09:42; spend 8 minutes on remaining 4 teasers; EOD)

> If **the manager needs to ask "wait, what does that command mean?"** → the one-pager failed → go back & simplify the **COMMAND** row.
> If **the manager asks "is it safe to put this on our YouTube channel?"** → the one-pager is fine → proceed.

---

**Last updated**: 2026-07-12
