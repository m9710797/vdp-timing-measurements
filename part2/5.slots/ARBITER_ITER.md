# CPU/command arbiter — iteration log

This document was written with assistance from an AI coding agent.

Working notes while refining the mixed (CPU+command) slot model.
Settled command engine: `FINDINGS4.md`. Combined write-up: `FINDINGS5.md`.
This file is the scratch log; rules are only promoted if they stay plausible
in hardware and do not break `noCpu` / 2013.

Oracle occupy = observed CPU RAS (not `/CSR`), unless a test says otherwise.

---

## Starting point (before this log)

Sprites-off leftovers after skip-CPU-RAS, FINDINGS4 waits, from observed last:

- **Dummy `R..` (35 packed-cand skips):** all in the ~32-cycle T-window of the
  next CPU RAS (“maybe pending”). Same window as 49 packed **hits**. Cannot
  cut on T-window alone. Occupying the dummy *tag* perfects HMMV/YMMM.
- **Idle packed (62):** 58/58 are last packed, `Δ=32` (LMMM `R.s→R.d`), next
  CPU RAS at **`C+26`**. Rule **P3**: skip that candidate. LMMM sprOff
  798→856/861 steps, 0→1/5 files; other commands unchanged.
- **HMMM newline idle:** `Δ=128`, not P3.
- Occupying all packed `C` with `Tmin≤C<S` over-blocks (67→56 files).

Scores below are 2026 mixed `*rdCpu*`/`*wrCpu*` (no `-stop-`), FINDINGS4 waits,
from observed last, occupy = observed CPU RAS. **84 files**, 12544 engine RAS.
Baseline (CPU RAS only): **67/84** files, **12447/12544** steps.

`./fit_2026 --mismatch` in `part2/5.slots/`.

---

## How a rule is judged

1. **Does it make sense in hardware?** Local VRAM sequencer, no trace-wide
   lookahead, must not need `/CSR`. Prefer predicates on (last RAS, candidate,
   next CPU RAS, Δ).
2. **How to test?** Score mixed files; require `noCpu` 508/508 unless the
   predicate is false there. Check 2013 later if a rule is promoted.
3. **Can it generalize?** If a constant is command-specific (Δ=32, +26, +64),
   try the weaker/stronger neighbour and see what breaks.

---

## Iteration 1 — P3 neighbours (P4–P6)

| Rule | Predicate (packed candidate `C`) | Mixed files | LMMM sprOff | Keep? |
|------|----------------------------------|------------|-------------|-------|
| P3 | last packed, Δ=32, CPU at `C+26` | 68/84 12505 | 1/5 856/861 | yes, seed |
| P4 | tight (`edist==Δ`) and CPU at `C+26` | = P3 | = P3 | redundant |
| P5 | last packed, Δ=32, **next CPU after last is after `C`** | **70/84 12508** | 3/5 859/861 | **best skip** |
| P6 | `s→d` and CPU at `C+26` | = P3 | = P3 | Δ=32 already selects LMMM dest-read |

**P4 vs P3:** every packed *hit* has `edist > Δ` (tight=0/1367). Idle Δ=32
packed-to-packed *is* tight. So “tight ∧ CPU+26” is exactly P3’s idle set.
Tightness cannot mark dummies (also tight=0).

**P5 vs P3:** leftover LMMM idle has CPU at **`C+58`**, not `C+26` (command
takes `C+26`, CPU the next CPU-legal). P5 catches those. See iteration 5 for
why this is not “any CPU in the file”.

---

## Iteration 2 — “just skip packed if CPU at +26” (P8, P10)

| Rule | Mixed | Harm |
|------|-------|------|
| P8 skip packed if CPU at `C+26` (no other cond) | 64/84 12496 | HMMV/HMMM/YMMM maybe-hits (43) |
| P10 skip if CPU+26 and slack | 63/84 | all packed hits are slack; ≈ P8 minus P3 idles |

**Rejected.** Packed +6 with CPU at +26 is often a *real* command access
(maybe-hit 43/49). Yielding whenever CPU wants `C+26` is too coarse.

---

## Iteration 3 — drop the CPU condition (P12) and local windows (P11, P14)

| Rule | Mixed | noCpu | Note |
|------|-------|-------|------|
| P12 last packed, Δ=32, **ignore CPU** | 69/84 | **508/508, fires=0** | geometry never occurs in noCpu |
| P11 last packed, Δ=32, CPU in `(C, C+64]` | 69/84 12507 | n/a (needs CPU) | weaker than P5 on LMMM |
| P14 P11 + Δ=128 if CPU already in `(last, C]` | 69/84 12508 | skip-Δ=128-always also 508/508 | newline +1 HMMM step |

**P12 fires=0 on noCpu** is the important negative. Packed-to-packed Δ=32
(cand is the next packed, 32 after last packed) does not appear unless a
previous access was already forced onto packed — i.e. mixed CPU stole the
CPU-legal pair-start (`C−6`). In noCpu the command stays on CPU-legal slots
for LMMM `R.s→R.d`, so last is never packed on that step.

Skipping Δ=32 packed without a CPU predicate is therefore *safe* for noCpu
only because it never fires, not because the engine would survive it.

**P14 newline:** one HMMM sprOff idle, last packed, Δ=128, CPU already fired
at r630 in the wait, skip packed r700, command takes r726 (`C+26`). Not a
dummy. Independent of P5.

---

## Iteration 4 — dummy `R..` vs packed maybe-hits

35 dummies, 49 maybe-hits. Tried to split them:

| Feature | Dummy | Maybe-hit | Splits? |
|---------|-------|-----------|---------|
| T-window class | 35/35 maybe | 49/49 maybe | no |
| CPU at `C+26` | 34/35 | 43/49 | no |
| last packed | 5/35 | 8/49 | no |
| tight wait | 0/35 | 0/49 | no |
| Δ | 24, 36, 46, 60 | 24, 36, 46, 60 | no |
| `C − Tmin` | **always 21** | **always 21** | no |
| `Tmax − C` | 10 (34) or 38 (1) | 10 (43) or 38 (6) | no |
| slack `edist−Δ` | 2 or 4 | 2 or 4 | no |
| cmd / kind / rd vs wr / RAS | heavy overlap (YMMM `w→s` Δ=36 lastC cpu26 is 24 vs 27) | | no |

**Cannot predict `R..` from these.** Occupying the observed tag still
perfects HMMV+YMMM and most HMMM (78/84 files) but LMMM stays 0/5 (idles,
not dummies). Same keys appear as both dummy and real hit — the tag is
doing work no local rule has replaced.

---

## Iteration 5 — restating P5 (hardware, not lookahead)

P5 as coded: last packed, cand packed, Δ=32, `Snext > C` where `Snext` is
the **first CPU RAS after `last`**.

That is **not** “any CPU later in the capture”. Counterexample
(`lmmm-rdCpu-1`, with dummy occupy + a +64 window):

```
s→d Δ=32 last=5724 r252P  cand=5788 r316  obs=5788
CPU Snext=5782 = C−6   (pair-start of 316)
```

A later CPU exists in `(C, C+64]`, so a +64 window **false-skips** this hit
(83/84). P5 does **not** skip: `Snext=5782 < C`, CPU already took `C−6` in
the gap, packed `C` is the overflow — grant it. Score with dummy occupy:
**84/84** (see C below).

**Hardware reading:** after a packed access, dest-read wait 32 lands on the
next packed. In sprites-off that packed is +6 after a CPU-legal.

- If a CPU RAS already sits in `(last, C]` (typically at `C−6`): CPU used
  the pair-start; command **takes** packed `C`.
- If the next CPU RAS is still after `C`: CPU has not been served in this
  cell; command **skips** packed `C` (idle). Command often then uses `C+26`;
  CPU uses `C+26` or `C+58`.

noCpu: this last-packed Δ=32 candidate never appears (P12 fires=0), so P5
does not disturb 508/508.

P3 is the special case where that future CPU is exactly `C+26`. P5 also
covers `C+58` (next CPU-legal after the following packed).

---

## Iteration 6 — Δ=60 last packed (P15/P18) and newline polarity

LMMM leftovers after P5 (without dummy occupy) are `w→s` Δ=60, last packed,
CPU at `C+26` (two steps). Same signature is an HMMM **maybe-hit** (5) and
HMMM dummy (3). Skipping Δ=60 lastP cpu26 (P18) helps LMMM (4/5) and **hurts
HMMM** (3/6→1/6). **Rejected** as a shared rule.

Newline (Δ=128) polarity is **opposite** to P5: CPU already in `(last, C]`
→ **skip** packed landing. Short packed-chain wait: CPU in the gap → **take**
packed. Do not merge them into one “CPU during wait” bit.

---

## Iteration 7 — dummy oracle + skip rules (ceiling)

Occupy observed CPU RAS **and** observed `R..`, plus skip rules:

| Combo | Files | Notes |
|-------|-------|-------|
| B: `R..` only | 78/84 12482 | HMMV/YMMM 6/6; HMMM 5/6; LMMM 0/5 |
| C5: `R..` + P5 | 83/84 12543 | LMMM 5/5; HMMM still 5/6 (newline) |
| C3: `R..` + P3 + newline | 81/84 12541 | LMMM 2/5 — still need P5’s `C+58` |
| C11: `R..` + CPU in `(C,C+64]` + newline | 83/84 12543 | false-skip `C−6` case; 1 LMMM miss |
| C96: window +96 | 83/84 12542 | worse (more false skips) |
| **C: `R..` + P5 + newline** | **84/84 12544** | complete **oracle** occupancy |

So a closed mixed model, *if* dummies were known, is:

1. Occupy CPU RAS (never packed +6).
2. Occupy dummy `R..` (always packed +6, address `0x1FFFF`).
3. **P5:** last packed, cand packed, Δ=32, first CPU after last is after cand
   → skip cand.
4. **Newline idle:** last packed, cand packed, Δ=128, a CPU RAS in `(last, C]`
   → skip cand.

Without (2), best is **P15 = P5 + newline: 70/84 12509**. The 14 files left
are dummy/`R..` (HMMV, YMMM, most of HMMM) plus two LMMM Δ=60 idles that
dummy occupy happens to straighten out in combo C.

---

## Best rules worth keeping (not promoted to FINDINGS5 yet)

**Keep**

- **P5** (restated): packed-chain dest-read; skip extra packed if CPU not
  already served in `(last, C]`. Test: mixed 70/84 without dummies; 83/84
  with dummy occupy (C5); noCpu untouched.
- **Newline idle:** Δ=128, last packed, CPU already in the wait, skip packed
  landing. +1 HMMM step; required for 84/84 with dummy occupy.
- **Dummy occupy** as an *observed* fact, not a predicted rule. 35 packed
  maybe-skips vs 49 maybe-hits; no local split found.

**Reject / do not generalize**

- Skip every packed with CPU at +26 (P8/P10).
- Skip packed if any CPU in `(C, C+64]` (false-skips overflow hits).
- Skip packed Δ=32 with no CPU predicate (P12): safe on noCpu only because
  it never fires; wrong on mixed hits with Δ=32 (79 packed hits have Δ=32,
  all “never” pending).
- Shared Δ=60 last-packed skip (hurts HMMM, helps LMMM).

---

## Still open

1. **Predict dummy `R..` without the tag.** Same maybe-window, same Δ, same
   CPU+26, as real packed hits. Next place to look: DRAM row/page vs CPU
   data vs command data; VCD around a dummy vs a maybe-hit on the same RAS
   (e.g. YMMM `w→s` Δ=36 lastC cpu26). `/CSR` is still ±1–2 — do not treat
   it as gospel.
2. **LMMM Δ=60 idle** without dummy occupy (two steps). Combo C absorbs
   them; a stand-alone rule collides with HMMM hits.
3. **2013:** P5/newline untested (no sprOff HMMM/LMMM there). Dummy `R..`
   may be 8280-only.
4. **CPU side:** still 2013 17/17; 2026 CPU from `/CSR` not revisited this
   round.

---

## Scoreboard (mixed 2026, occupy CPU RAS, from observed last)

```
A    CPU RAS only                         67/84   12447/12544
P3   lastP Δ=32 CPU at C+26               68/84   12505
P5   lastP Δ=32 Snext > C                 70/84   12508
P15  P5 + newline idle                    70/84   12509
B    CPU RAS + observed R..               78/84   12482
C5   R.. + P5                             83/84   12543
C    R.. + P5 + newline                   84/84   12544   (oracle)
```

---

## Iteration 8 — why the next probes (before running)

Without dummy tags the model is **70/84** (P5 + newline). The 14 imperfect
files are not a smear of random misses: combo C showed they are **exactly**
(1) dummy `R..` on packed +6 and (2) two LMMM Δ=60 idles that dummy-occupy
straightens. So the next change is only worth trying if it targets one of
those populations with a hardware story, not another Δ-constant.

**Where the model is already good.** Display-off and sprites-on mixed: 100%
with CPU-RAS occupy alone (no packed +6 in those tables). Sprites-off LMMV:
100%. Packed-to-packed LMMM dest-read: P5. Command-only: 508/508. That is
the FINDINGS4 engine plus “CPU-legal vs overflow packed.”

**Where it is not.** Sprites-off YMMM 0/6, HMMV 3/6, HMMM 3/6 without the
`R..` tag — first miss is almost always “pred packed, hardware dummy or
later.” Stop traces (`*-stop-*`) have the **same** `R..` with **no command**,
so dummy is a CPU-path phenomenon, not a command-vs-CPU tie-break of a
command access. Mixed maybe-hits are the opposite: command *does* take that
packed slot while CPU still fires at `C+26`.

**Probe D (dummy as CPU D16 on the full table).** FINDINGS5 already guessed:
the arbiter runs the 16-cycle check on the **full** command table (packed
included); CPU cannot actually cycle packed +6, so you get `R..` at `C` and
CPU RAS at `C+26`. Packed is +6 after pair-start `C−6`. NEED=16 then gives a
**6-cycle** T-window where `C−6` is too close for the CPU-legal table
(`dist(T,C−6)<16`) but packed `C` is far enough (`dist(T,C)≥16`):

`C−22 < T ≤ C−16`.

Outside that window, first CPU slot is `C−6` or `C+26` without granting `C`.
This is worth testing because earlier T-window work used the **CPU-legal**
LUT, so `C−Tmin` was identically 21 for dummy and hit — it could not see
this 6-cycle sliver. Check: (a) `stop`: whenever CPU RAS is at `C+26` and `C`
is packed, is `C` always dummy? (b) mixed dummy vs maybe-hit: `/CSR` `T`
relative to `C−16` (diagnostic only, ±1–2). (c) `C−6` occupancy (FINDINGS5:
never CPU).

If (a) is always-dummy in stop, mixed hits are command **overriding** a
CPU-granted dummy. Then a CPU-only predictor will over-block mixed hits
(that is P8). If `/CSR` `T` splits dummy vs hit on the 6-cycle window, the
mechanism is understood even if we still refuse `/CSR` in the model.

**Probe G1 (newline polarity).** The HMMM newline skip is CPU **during** the
wait, not at `C−6`. P5 **take** is CPU **at** `C−6`. Unify: skip packed `C`
if some CPU RAS in `(last, C]` is **not** the pair-start `C−6`. Worth
trying only if packed **hits** almost never have a mid-wait CPU other than
`C−6` (otherwise it is P8 in disguise). Replaces the Δ=128 special case if
the hit count is ~0.

**Not worth trying again.** Skip-all-packed-if-CPU+26 (P8); Tmin/Tmax cuts
on the CPU-legal window; Δ=60 last-packed (hurts HMMM).

---

## Iteration 8 — results

**LUT.** On sprites-off, `first_slot` on the **full** command table (NEED=16)
and on the CPU-legal table (NEED=16) both grant `C+26` from CPU-legal, but
the full table grants packed `C` exactly for `T−C ∈ {−21,…,−16}`. Matches
the 6-cycle packed gap (`C−6` fails NEED=16, `C` passes).

**Stop (no command).** Dummy is **not** “whenever CPU is at `C+26`”:
326 such CPU RAS, only 42 dummies (rest empty). Dummy is a **rare** CPU-path
event. 42/44 dummies have CPU at +26; 2 at +54.

**C-6.** Every dummy and every maybe-hit has `C−6` empty. No split.
FINDINGS5 “never CPU” holds; “or command” does not show up on these steps.

**G1 rejected.** 95 packed **hits** have a CPU RAS in `(last, C]` other than
`C−6`. The newline leftover is 1 idle of that shape. Generalising newline
to all mid-wait CPU **destroys** real packed grants (G1 59/84). Newline
stays a Δ=128 exception.

**P15 leftovers** are exactly 14 sprites-off files, all with `R..` present
(HMMV 3, YMMM 6, HMMM 3, LMMM 2). Same list as “dummy-tag would help.”

---

## Iteration 9 — dummy is an early D16 grant of packed +6

Fitted `/CSR` arming `T` (`vdp_cpu_posts`, per-file δ; still ±1–2, diagnostic
and then a ceiling score):

| | `T−C` |
|--|--|
| mixed dummy | **−21, −20, −19**, two at −18 |
| mixed packed hit, CPU at `C+26` | starts at **−18**, then −17… |
| stop dummy | **−21, −20, −19**, two at −18, one −8 |
| stop empty (CPU at `C+26`, no dummy) | starts at **−18**, never −21…−19 |

**Stop, tight cut `−22 < T−C ≤ −19`:** dummy **29**, empty **0**.
Late part of the 6-cycle LUT window (`−18…−16`): dummy 2, empty 29.

So the LUT NEED=16 window is **too wide**. `first_full` with **NEED=19**
grants packed `C` exactly at `T−C ∈ {−21,−20,−19}` — the same cut.
Interpretation: the CPU 16-cycle check on packed +6 behaves like NEED=19
(16 plus ~3: one stretch and/or `/CAS` vs RAS, or `/CSR` bias). CPU-legal
slots stay NEED=16. That is why `C−6` is skipped (too close) and packed `C`
is granted only if the request is **early** in the cell.

**D6** (uses fitted `T`, not a closed no-CSR rule): skip packed `C` if CPU
RAS is `C+26` and `T−C ∈ (−22, −19]`.

| Model | Files | Steps | vs previous |
|--|--|--|--|
| P15 (no dummy tag, no CSR) | 70/84 | 12509 | — |
| D4 (full 6-cycle window) + P5 | 73/84 | 12515 | false-skips hits at −18…−16 |
| **D6 + P5** | **78/84** | **12536** | same *files* as occupying `R..` (78/84 12482), **more steps** |
| D6 + P5 + newline | 79/84 | 12537 | +1 HMMM newline |
| Occupy observed `R..` only | 78/84 | 12482 | tag, no mechanism |
| Oracle `R..` + P5 + newline | 84/84 | 12544 | tag + skips |

D6 is the first dummy predictor that is **not** the `R..` tag and that
**does not over-block** mixed hits the way P8/D4 did. It still needs an
arming `T` (fitted `/CSR`). Overlap at `T−C = −18` is two dummies and three
hits — the ±1–2 we were warned about. Stop traces, which have no command,
show the same early/late split, so this is not a mixed-file artefact.

**What is now explained.** Dummy `R..` on packed +6 is the CPU path winning
a D16 check on the extra slot when the request is early enough that packed
`C` meets ~NEED=19 and the pair-start `C−6` does not meet NEED=16. DRAM
strobes `0x1FFFF` because the CPU cannot actually use +6; the real CPU RAS
is `C+26`. Command may use packed `C` when `T` is later (no dummy grant).

**What is not closed.** (1) Predict that early `T` without `/CSR`. (2) Five
files still imperfect with D6+P5+newline (likely LMMM Δ=60 idles + the
−18 overlap + unmatched posts). (3) Do not put `/CSR` into the promoted
model yet; D6 is a mechanism ceiling, P5 still stands without it.

---

## Scoreboard (updated)

```
A     CPU RAS only                            67/84   12447/12544
P5    lastP Δ=32 Snext > C                    70/84   12508
P15   P5 + newline                            70/84   12509
B     CPU RAS + observed R..                  78/84   12482
D6+P5 fitted T-C≤-19 (CSR diagnostic)         78/84   12536
D6+P5+nl                                      79/84   12537
C     R.. + P5 + newline (oracle occupy)      84/84   12544
```

---

## Iteration 10 — async T ±1 / ±2 (this question)

Previous D6 treated fitted `/CSR` `T` as exact. The 8280 CPU and VDP clocks
are not locked; FINDINGS5 already says the edge→arbiter delay is about
±1–2 VDP cycles. The CPU path already scores a **D16-tie** (`first_slot(T±1)`).
This round does the same for the dummy grant: count how many of
`{T−1, T, T+1}` make `first_full(T+d) = C` (`ngrant` 0..3).

**Does NEED=16 plus slop replace NEED=19?** No.

| | ngrant±1 = 0 | 1 (tie) | 2 | 3 (all) |
|--|--|--|--|--|
| mixed dummy, NEED=16 | 0 | 0 | 6 | 24 |
| mixed hit, NEED=16 | 11 | 9 | 12 | 11 |
| mixed dummy, NEED=19 | 0 | **2** | 11 | 17 |
| mixed hit, NEED=19 | 40 | **3** | 0 | 0 |
| stop dummy, NEED=19 | 1 | 2 | 10 | 19 |
| stop empty, NEED=19 | 274 | 7 | **0** | **0** |

NEED=16 still grants packed `C` for many **hits** (11+12 with ngrant≥2).
Async ±1 does not turn that into a dummy-only window. The extra slot is
not “NEED=16 smeared by two cycles.”

NEED=19: hits almost never have ngrant≥2. The messy `T−C = −18` cases are
exactly **ngrant=1** (2 dummies, 3 hits mixed; 2 dummy + 7 empty on stop).
That is the async edge, same shape as a CPU D16-tie.

**Scores (skip packed if CPU at `C+26` and ngrant≥min, plus P5):**

| Policy | Files | Steps |
|--|--|--|
| NEED=16 any/maj/all + P5 | 71–73/84 | worse than D6 |
| NEED=19 **any** + P5 | 78/84 | 12535 |
| NEED=19 **maj (≥2)** + P5 | **78/84** | **12536** |
| D6 (center `T−C≤−19`) + P5 | 78/84 | 12536 |
| NEED=19 all + P5 | 73/84 | 12525 |

Majority ±1 with NEED=19 **is** D6. “Any” catches the 2 dummy ties and
false-skips the 3 hit ties (net −1 step). “All” drops edge dummies.

**Verdict.** Taking ±1–2 into account **explains** the −18 overlap; it does
**not** improve the deterministic engine score and does **not** let us drop
the early/NEED=19 cut back to ordinary NEED=16. A deterministic model still
has to pick a side of a 1-cycle tie. D6/majority is the “don’t skip on
tie” choice (same as not occupying a D16-tie dummy). Treating ties as
free in scoring would hide 5 steps; the command engine still has to emit
one RAS.

---

## Iteration 11 — beat-phase `T_i = floor(t2_i + ε)` (one ε per file)

Iteration 10 jittered each arming `T` independently. Consecutive fitted
posts are almost only **71 or 72** (1692+1939), with 71-then-72 and
72-then-71 equal (1480 each) and rare same-gap doubles (156 / 393). Independent
±1 on neighbours would make 70/73; those almost never happen. The uncertainty
is **where the extra/missing cycle sits** — one analog phase for the burst.

Integer `δ` in `T = floor(t2) + δ` shifts every request the same amount and
**preserves all gaps**. `T_i = floor(t2_i + ε)` with a fractional `ε` does
not: only requests whose `{t2}` is near 1−{ε} cross an integer.

Sweep `ε ∈ [δ−2, δ+2]` at 0.1 (δ = D16-best integer). Dummy grant still
NEED=19 packed `C` with CPU at `C+26`. Occupy observed CPU RAS. Engine
**P5 + newline**. 28 sprOff files have `/CSR`; other modes are P5+nl only.

**The floor model does slide 71/72 doubles** (not a global δ+1):

- +0.1 from integer δ: **269/3095** posts change `T`; **27/28** files
  partial, **0** all-shift.
- Per file the doubles move, e.g. `ymmm-rdCpu-1` 71-71: 2 at δ=10 → 9 at
  ε=9.8. Totals at δ vs δ+0.5 barely move (142 vs 145) because files
  slide in opposite directions. Comparing δ±2 is meaningless (both
  integers → identical gaps).

**Engine scores (P5+newline + NEED=19 dummy):**

| Policy | Files | Steps |
|--|--|--|
| ε = integer δ (= D6+P5+nl) | **79/84** | **12537** |
| ε best for CPU D16 | 78/84 | 12536 |
| ε best for engine (oracle phase) | **82/84** | **12542** |
| ε worst in the ±2 window | 70/84 | 12504 |
| dummy if ANY ε in [ε_D16±1] grants | 79/84 | 12537 |
| dummy if MAJ | 78/84 | 12536 |
| dummy if ALL | 72/84 | 12520 |

Without newline, integer was 78/84 12536 and oracle 81/84 12541
(`hmmm-rdCpu-1` is the newline step).

22/28 sprOff files have ε_D16 ≠ integer δ, but only by **0.31** on
average. Engine-oracle ε is **1.52** away — it is not the D16 phase.

Oracle still misses one step each:

- `scr5-sprOff-ymmm-wrCpu-2.txt` 210/211, best ε = **10** (integer; the
  ±2 sweep cannot help)
- `scr5-sprOff-ymmm-wrCpu-3.txt` 206/207, best ε = 9.8

**Verdict.** The 71/72 beat is real and `floor(t2+ε)` is the right noise
model (iteration 10’s per-request ±1 is not). It does **not** give a
dummy rule without `/CSR`: the D16-best fractional ε is slightly *worse*
than integer D6; voting over a ±1 band is D6 or worse; the oracle phase
that picks dummy grants from the engine RAS recovers 3 files and still
leaves two YMMM leftovers that are not a phase error. Dummy is not “NEED=16
smeared by beat phase,” and it is not fully “NEED=19 at the wrong ε.”
