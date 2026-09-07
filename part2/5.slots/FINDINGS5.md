# VDP command and CPU slot timing

This document was written with assistance from an AI coding agent.

**Superseded by [`FINDINGS7.md`](FINDINGS7.md)** for the current model
(including the sprites-on padding, the pin vs lookahead split, and
`rdCpu222`). This file is the discovery write-up that led there. FINDINGS4
stays frozen.

Stand-alone write-up of the slot model. Command-engine waits, stretch, and
sprites-on +1 are those of [`FINDINGS4.md`](FINDINGS4.md) (kept frozen). This
file restates them compactly, **replaces FINDINGS4 §6** with a general
packed-start +1, and adds CPU VRAM accesses and CPU/command arbitration.

| | 2013 | 2026 |
|--|------|------|
| Machine | Philips **NMS 8250** | Philips **NMS 8280** |
| Data | `8.final-analysis/` | `part2/5.slots/` (txt) + `part2/1.vcd/` |
| Clocks | One 21.47727 MHz VDP crystal. CPU = VDP `/6` | Separate crystals. VDP ~21.33 MHz PAL |
| CPU request times | Exact **72 / 252 / 3060** VDP cycles (40 I/O + loop), unknown phase `φ0` | `/CSR` / `/CSW` on the refresh-interpolated VDP clock. Default burst: intra-burst gaps **71 or 72** (beat of two crystals), loop ~250.5. Sparse `rdCpu222` (§14): **37 T / 67 T** wrap. Edge→arbiter delay `δ` unknown; the *sum* from rising `/CSx` to slot decision is ~**26** memory cycles (§11) |

Bitmap screen 5 (screen 8 shares the slot maps). Line period = **1368** VDP
cycles. All slot times and waits below are **RAS** (openMSX numbering). The
published `.txt` rows are **CAS**; conversion is §1.

Command start `S` is not fitted: traces are mid-command.

Fitters (do not change `fit_2013.cc` behaviour):

- `8.final-analysis/cpu_scratch_2013.cc` — 2013 CPU grid + HMMV
- `part2/5.slots/fit_2026.cc --nocpu` — 2026 command engine
- `part2/5.slots/fit_2026.cc --scratch` — 2026 occupancy + engine with observed CPU RAS skipped
- `part2/5.slots/fit_2026.cc --mismatch` — mixed packed leftovers, dummy window, beat-phase
- Scratch log (not promoted): [`ARBITER_ITER.md`](ARBITER_ITER.md)

---

# I. Command engine (no CPU)

Data: `part2/5.slots/scr5-{dispOff,sprOff,sprOn}-*-noCpu-*.txt` (including
`*nx??-?d.txt`). **508** traces, **76766** engine-to-engine transitions.

After an access at RAS time `t`, wait `Δ` **memory cycles**, then take the first
command slot whose **engine-distance** from `t` is ≥ `Δ`. Engine-distance is
wall-clock VDP cycles minus 2 for each stretched memory cycle in `(t, next]`
(§3). With sprites on, every `Δ` is 1 larger (§4). If `t` is packed +6,
`Δ` is 1 larger again (§6).

## 1. RAS vs CAS

`/RAS` falling is when the row address is driven (access start). `/CAS`
normally falls **1** VDP cycle later.

| | RAS (openMSX) | CAS (`.txt` rows) |
|--|----------------|-------------------|
| Typical slot | `r` | `r+1` |
| Stretched slots, display off | 1324, 1334 | **1326, 1336** |
| Stretched slots, sprites off | 1322, 1332 | **1324, 1334** |
| Sprites on (no stretch in the command table) | `r` | `r+1` |

On those two stretched slots, `/CAS` follows `/RAS` by **2** cycles (~100 ns vs
~50 ns in the VCDs). Same slots, not a 1-cycle table error.

To map a `.txt` time to RAS: subtract 1, or subtract 2 when the CAS row is
1326/1336 (dispOff) or 1324/1334 (sprOff). After that, every engine access in
the 2026 `noCpu` set lands on the openMSX slot tables.

## 2. Slot tables

Command slots are the openMSX bitmap tables in `VDPAccessSlots.cc` (RAS):

| Mode | Command slots / line |
|------|----------------------|
| dispOff (display off / vertical border) | 154 |
| sprOff (bitmap, sprites off) | 88 |
| sprOn (bitmap, sprites on) | 31 |

Sprite count and size do not add command slots. Screen 5 and 8 share these maps.

Display-off RAS grid (including refresh and dummy reads) is 166 pulses per line:
**163×8 + 2×10 + 1×44 = 1368**. The two 10-cycle gaps are `1324→1334` and
`1334→1344`. Sprites-off has the same gaps two cycles earlier
(`1322→1332→1342`). Sprites-on has no command slot in that region (last slots
1264, 1330).

The 44-cycle hole (dispOff `120→164`) is five empty memory cycles, not a stretch.

### 2.1 Packed +6 (sprites-off only)

In **sprites-off** bitmap mode, part of the command table is not a uniform 8-cycle
grid. During the active display the VDP inserts extra command/CPU opportunities
as **pairs six cycles apart**: after a command slot the DRAM bus is idle for 6
clocks, and that idle window is itself a command slot.

**Definition:** a *packed +6* slot is a command-table RAS `S` whose predecessor
in that table is `S−6`. It is still a legal **command** slot. In the rest of
this file, *CPU-legal* means `command_slots` minus these packed +6 times.

This geometry exists **only in sprites-off**. Display-off command slots are 8
apart (or 16 at refresh holes, 10 at the two stretches). Sprites-on has no
adjacent command slots 6 apart. Occupancy counts of packed +6 in those two
modes are **zero**; leftover class §13 cannot occur there.

**Why only sprites-off.** In the active display the VDP groups memory cycles
in 32-cycle cells: `182+32k` is a sprite-fetch when sprites are on and spare
when they are off; `188+32k` is spare in both modes; `194+32k` is the display
burst (one `/RAS`, four `/CAS`). With sprites off the first becomes spare too,
which is the 6-cycle pair — and why only that table has them. Refresh takes
the middle cycle once per 128, which is why the list has **25** entries, not
32. It is a property of the **slot grid**, not of the traffic: in sprites-off
CPU captures the first of the pair is idle ~32% of the time, and the CPU still
does not take the second.

Sprites-off has **88** command slots per line, of which **25** are packed +6
and **63** are CPU-legal. None of the 25 is a stretched slot (CAS = RAS+1).

The 25 packed +6 RAS times, with the CPU-legal first-of-pair in parentheses:

```
(182, 188)  (214, 220)  (246, 252)
(310, 316)  (342, 348)  (374, 380)
(438, 444)  (470, 476)  (502, 508)
(566, 572)  (598, 604)  (630, 636)
(694, 700)  (726, 732)  (758, 764)
(822, 828)  (854, 860)  (886, 892)
(950, 956)  (982, 988) (1014,1020)
(1078,1084) (1110,1116) (1142,1148)
(1206,1212)
```

Packed RAS only: **188, 220, 252, 316, 348, 380, 444, 476, 508, 572, 604, 636,
700, 732, 764, 828, 860, 892, 956, 988, 1020, 1084, 1116, 1148, 1212**.

That is eight groups of three pairs, repeating every **128** from 182
(`{0,6}`, `{32,38}`, `{64,70}`), plus a truncated last group that only keeps
`(1206, 1212)`. Each group also has a **lone** CPU-legal slot at relative
**96** (278, 406, 534, 662, 790, 918, 1046, 1174) that is *not* packed. Left
border (`6…118`, `162`, `170`) and end-of-line (`1266…1366`) are 8-apart or
stretch, not packed.

**CPU never uses them (both measurement sets).** Every CPU VRAM RAS was checked
against the list above:

| Set | CPU RAS on packed +6 | CPU RAS in sprites-off (where packed slots exist) |
|-----|----------------------|-----------------------------------------------------|
| 2013 `8.final-analysis` `*cpu{read,write}*` | **0 / 1141** | **0 / 204** |
| 2026 `5.slots` `*rdCpu*` / `*wrCpu*` (stop + command) | **0 / 12846** | **0 / 4346** |

CPU accesses always land on the first of the pair, or on an unpaired CPU-legal
slot. Commands **do** use the +6 (2013 sprites-off HMMV: 24 / 85 engine writes;
2026 sprites-off: 1436 / 4608).

**No other exclusive slots.** Union of all CPU VRAM vs command-engine RAS, per
mode, every command-table slot:

| Mode | 2026 (covering set) | 2013 |
|------|---------------------|------|
| dispOff (154) | all 154 used by **both** CPU and command | 137 both; 16 cmd-only and 1 CPU-only (`628`, *n*=2) are coverage holes — 2026 has both on all of them |
| sprOff (88) | **63** CPU-legal: all used by both. **25** packed +6: command only (CPU=0). No CPU-only slots | CPU still 0 on all packed +6. Other CPU-only / cmd-only / unused rows are undersampling (8 sprOff files) |
| sprOn (31) | all 31 used by **both** | 29 both; `188` CPU-only (*n*=8) and `170` cmd-only (*n*=10) — 2026 has both on both slots |

So the only command-table slots that are exclusive to one client are the
sprites-off packed +6 (command, never CPU). There are **no** CPU-only slots.
Dummy `R..` (2026 sprOff, **333** in the `.txt` parse; ~335 counting from
VCDs) also sit on those same 25 packed times; those slots still take command
accesses in other traces. The dummy is the CPU path being granted a packed
slot it cannot carry (§13.1).

## 3. Stretched memory cycles

Two memory cycles per line last **10** VDP clocks instead of 8. Those extra **4**
clocks exist on the wire. The command engine’s delay counter **does not count
them**: a wait that spans both stretches needs 4 more real cycles to complete.
This is a property of the line, not of whether the command uses those two slots.

```
104 W → 164 R     Δ = 60   does not cross the stretches → engine count 60 → slot taken
1292 W → 1360 R   Δ = 68   crosses both → 1352 is only 56 engine cycles → skipped
```

The 44-cycle hole **does** count. HMMV with `Δ=48` from 120 skips 164 (+44) and
takes 172 (+52).

If stretch completion `s` lies in `(from, to]`, subtract 2 from the distance.
Display off: completions at **1334** and **1344**. Sprites off/on: **1332** and
**1342**.

Sprites-on has no command or CPU slot on those two RAS pulses (sprite fetches
do). Applying the stall in all three bitmap tables, together with §4, is one
consistent parameterisation.

## 4. Sprites on: +1 on every step

With §3, the steps that still differ with sprites on all need **exactly one
extra cycle**. One rule: **sprite rendering adds 1 to every command-engine
delay.** That forces LMMM source→dest to **32** and HMMM/LMMM newline to
**128**, matching the other two modes. It is not a slot-position effect.

## 5. Wait parameters

`Δ` is the engine wait (work + arbitration), in memory cycles, **before** the
sprites-on +1. Any integer in a plateau produces the same next slot on these
maps. `L` is extra wait on a rectangle line-break: newline `Δ` = mid-line `Δ` +
`L`.

### 5.1 Plateaus (three-mode intersection)

Sprites-on bands have the +1 taken off before intersecting.

| Command | Pattern | Mid-line | Newline | `L` if mid and newline independent |
|---------|---------|----------|---------|-------------------------------------|
| **HMMV** | `W` | P ∈ [45, 48] | [103, 104] | [55, 59] |
| **LMMV** | `R.d ↔ W.d` | Pw ∈ [21, 24]; Pr ∈ [71, 72] | [129, 132] | [57, 61] |
| **YMMM** | `R.s ↔ W.d` | Pw ∈ [21, 24]; Pr ∈ [33, 38] | [103, 104] | [65, 71] |
| **HMMM** | `R.s ↔ W.d` | Pw ∈ [21, 24]; Pr ∈ [59, 60] | **128** | {68, 69} |
| **LMMM** | `R.s → R.d → W.d` | Prs = **32**; Prd ∈ [21, 24]; Pwd ∈ [59, 60] | **128** | {68, 69} |
| **LINE** | `R ↔ W` | Pw ∈ [21, 24]; Pr ∈ [81, 84] | [119, 120] | [35, 39] |

HMMM/LMMM newline is a **point** (128). LMMM `R.s → R.d` is **32** in all modes
(sprites-on via the +1).

### 5.2 Preferred representatives

Heuristics only pick a point inside §5.1 (similar steps share `L` / `Pw` / `Pr`;
then even, then highest power of 2). They do not improve the fit: this tuple and
the right-edge tuple (HMMV 48+56, …) both mispredict the same **32 / 76766**
before packed-start +1 (§6).

| Command | Mid-line | `L` | Newline |
|---------|----------|-----|---------|
| **HMMV** | P=**46** | **58** | **104** |
| **LMMV** | Pw=**24**, Pr=**72** | **58** | **130** |
| **YMMM** | Pw=**24**, Pr=**36** | **68** | **104** |
| **HMMM** | Pw=**24**, Pr=**60** | **68** | **128** |
| **LMMM** | Prs=**32**, Prd=**24**, Pwd=**60** | **68** | **128** |
| **LINE** | Pw=**24**, Pr=**84** | **36** | **120** |

```
HMMV  : W 46 (+58)
LMMV  : R 24  W 72 (+58)
YMMM  : R 24  W 36 (+68)
HMMM  : R 24  W 60 (+68)
LMMM  : R 32  R 24  W 60 (+68)
LINE  : R 24  W 84 (+36)
```

**L at a glance:** fills **58**; copy family (YMMM / HMMM / LMMM) **68**; LINE **36**.

## 6. Packed-start +1 (sprites-off)

FINDINGS4 §6 treated 32 leftovers as an HMMM/LMMM newline hack from RAS 188
(wait 130 instead of 128). They are the *visible* special case of a cheaper
rule, tested on every packed-start step, not just that wrap:

**If the previous engine RAS was packed +6, add 1 to `Δ`.**

As if that squeezed memory cycle finished one cycle late. Only sprites-off
has packed slots, so other tables are unchanged.

| Extra cycles after a packed start | Mispredicted of 76766 |
|--|--|
| 0 | 32 |
| **1** | **0** |
| 2 | 0 |
| 3 | 1281 |

The band is **[1, 2]**; use **1**. 1778 `noCpu` transitions start from one of
the 25 packed slots (21 distinct positions, 5 commands, several steps). Adding
1 only *changes* the chosen next slot for the 32; the other packed-start steps
still land on the same slot. Extra 3 is forbidden by 1281 observations.

Those 32 are **RAS 188 → RAS 342** (wall Δ = 154). Slot **316** is at exactly
+128 and is skipped. 18 HMMM + 14 LMMM, 27 files. Mid-line `W → R` from the
same slot 188 takes 252 and matches the model. 188 and 316 are both packed.
Wraps that **do** use +128 start from pair-offset 0, 32, 64, or 96. Offsets 38
and 70 never appear as the last write of a rectangle line in this set.

On `noCpu`, packed-to-packed `Δ=32` (LMMM dest-read) **never happens**: without
CPU the source read stays on CPU-legal slots, so last is never packed on that
step. Under contention it does — that is P5 (§13.3), not this +1. Packed-start
+1 is therefore not “always skip the next packed.”

With representative waits + stretch + sprites-on +1 + packed-start +1:
**0 / 76766** on the 2026 `noCpu` set. 2013 `8.final-analysis` has **no**
sprites-off HMMM/LMMM, so the 32 are untested there; the rest of 2013 `nocpu`
already fitted without the extra cycle, and +1 is silent on those steps.

FINDINGS4 still documents the 188-only hack. This file uses the general rule.

## 7. Trace quality (command-only)

Excluded (`wrong`; `noCpu` files that contain CPU `R.r` / `W.w`):
`scr5-wrong-dispOff-hmmv-noCpu-nx2-6d.txt`,
`scr5-wrong-dispOff-ymmm-noCpu-nx12-1d.txt`,
`scr5-wrong-sprOff-lmmm-noCpu-nx2-3d.txt`.

Corrected and included: `scr5-dispOff-lmmv-noCpu-nx2-3d.txt`,
`scr5-sprOff-lmmv-noCpu-nx8-5d.txt`.

**Misnamed (not used as command-only):** `scr5-sprOn-hmmm-rdCpu-1.txt` is labelled
`rdCpu` but has only `R.s` / `W.d` — no CPU VRAM. Treat as noCpu, or rename.
Same class on the sparse set: `scr5-sprOn-hmmm-rdCpu222-3e.txt` (§14).
Flattening parallel command columns invents fake W→R pairs; it is not a clean
arbiter counterexample.

**Renamed 222 captures:** `scr5-dispOff-hmmv-rdCpu222-7e` / `8e` were first
saved as sprites-off (`1.vcd` / `3.time` still `scr5-sprOff-hmmv-rdCpu222-3e` /
`4e`). Display was off; `5.slots` was renamed. Pair those VCDs with the
dispOff `.txt`. `3.time` still has dummy `R..` on sprites-off packed slots;
`5.slots` dropped them. CPU RAS in both sit on the **dispOff** table.

Some LMMM `*nx*-*d.txt` traces use `W..` instead of `W.d` where source and dest
overlap. Cadence is still `R, R, W`; those files are kept.

## 8. Method (command-only)

For a pair of RAS times `(p, n)`, admissible `Δ` (including sprites-on +1) is

`[engine_dist(p, prev_slot(n)) + 1, engine_dist(p, n)]`.

Intersect over all pairs of one step. Subtract the sprites-on +1 from that
mode’s band before intersecting the three modes. Line wrap vs mid-line is
labelled from the dest (or YMMM/HMMM src) address crossing a 128-byte VRAM line.

---

# II. CPU accesses and arbitration

The command model above is unchanged. CPU VRAM uses a **subset** of the same
command slots. The two machines differ in how request time `T` is observed, not
in the VDP arbiter (as far as 2013 can tell). 2026 adds a rule for **packed +6
while a CPU request is pending** (§13); that is not needed on 2013 (HMMV only,
and predicted CPU already matches oracle). Sparse 37 T CPU captures (`rdCpu222`)
are §14: they confirm occupancy losses and dummy rate, and they do not change
NEED=16.

## 9. Working model (CPU + mux)

```
LINE = 1368
NEED = 16          # engine cycles of lookahead (CPU-legal slots)
BUSY = 2           # ignore CPU posts in [RAS, RAS+2)

command_slots(mode) = bitmap table (RAS)
cpu_slots(mode)     = command_slots minus packed +6     # §2.1; empty unless sprOff

engine_dist(t, s):                    # s > t
    (s - t) - 2 × (stretch completions C with t < C ≤ s)
    # dispOff: C ∈ {1334, 1344}  (mod LINE)
    # else:    C ∈ {1332, 1342}

first_cpu_slot(T):
    earliest S in cpu_slots (repeating every LINE)
    with S ≥ T and engine_dist(T, S) ≥ NEED
```

**CPU path** — one pending buffer. A port access that arrives while the
register is occupied is **lost** (the old request keeps its scheduled slot;
the VRAM pointer does not advance). On 2013 nocmd traces this is
indistinguishable from “overwrite latched data, keep `sched`” (§10.2).
Cancelling an already-granted slot and rescheduling is **not** what hardware
does.

```
occupied = false
sched    = none
last_RAS = none

on CPU post at T, or when time reaches sched:
    if not occupied and last_RAS exists and T < last_RAS + BUSY:
        ignore post                    # not a new request (through CAS)
        return

    if occupied and (no post yet, or sched ≤ T):
        fire CPU access at sched
        last_RAS = sched
        occupied = false
        sched = none
        # then handle T if it is still pending

    if not occupied:
        occupied = true
        sched = first_cpu_slot(T)      # slot is fixed at this moment
    else:
        drop the new post              # lost request; keep sched
```

Sprites-on (2026, 12 T burst) shows **~8% more `/CSx` pulses than VRAM accesses**.
Fitting the buffer as occupied until **~9 cycles before RAS** (not until grant,
not until RAS) predicts ~314 losses vs ~326 observed. Display-off loses
essentially none. 2013 17/17 did not need this window spelled out; it is the
loss mechanism, not a replacement for NEED=16 or BUSY=2. The 37 T `rdCpu222`
set has **zero** simulated losses (§14): those extra pulses were occupancy,
not a second mux.

**Command path** — separate buffer, never drops. After an engine access at `t`:

```
Δ = FINDINGS4 wait for that step
if sprites on:     Δ += 1
if t is packed +6: Δ += 1              # §6; sprites-off only
take earliest S in command_slots (including packed +6)
    with S > t, engine_dist(t, S) ≥ Δ,
    and S is not a fired CPU RAS
    and S is not a dummy R.. on packed +6     # observed tag, or §13.2 if T known
```

If that candidate `C` is packed +6, apply §13 (P5 / newline / dummy grant)
before taking it. If the command loses a slot it takes the **next** slot, it
does not re-arm the full `Δ`.

**Arbitration, as this model states it:**

- CPU never fires on packed +6. If the CPU path wins a packed slot, the cycle
  is a dummy read and the CPU RAS is the next CPU-legal slot (§13.1).
- CPU RAS and command RAS never coincide.
- A running command does not delay CPU: `first_cpu_slot` ignores the command.
- CPU wins every contested CPU-legal slot because it is already scheduled from
  `T`; the command only takes leftovers.

## 10. Why these CPU rules (2013)

2013 is the right place to fix the arbiter: CPU and VDP share a crystal, so the
Z80 I/O train is an exact VDP-cycle lattice. The only unknown is the phase
`φ0` of that lattice relative to RAS (one integer in `0 … 3059` per capture).

CPU posts: 40 × `IN A,(n)` / `OUT (n),A` at **72** VDP cycles, then **252** to
the next burst (3 T left of the last I/O + 30 T loop + 9 T to the next post).
Period **3060**. The value 72 never appears as a VRAM gap; it is a Z80 fact.

Data: every `8.final-analysis/*cpu{read,write}*.txt` except `*nocpu*`. Tags
`R.c` / `W.c` are CPU VRAM; `W.e` is HMMV. Refresh `R.r` and sprite fetches are
ignored.

### 10.1 Occupancy (before any timing model)

All CPU RAS in these files sit on **CPU-legal** slots: **1141 / 1141**, including
**0 / 204** in sprites-off on packed +6 (§2.1). Mixed HMMV engine writes **do**
use packed +6 (screen-8 sprites-off: 24 / 85 writes). CPU and command never
share a RAS.

That is the empirical content of “CPU can only use the well-known CPU+command
slots”: the packed second-of-pair is command-only. The 2026 set repeats the
same occupancy (§12).

Documented dummy reads in blanking (dispOff 1236/1244/1252/1260, sprOff
1242/1250/1258 in CAS-ish published tables) are **outside** the CPU/command
table. They are not this population. (2026 sprites-off has a *different* dummy
population on packed +6; §13.)

### 10.2 Search, not assumption

On the 10 `*nocmd*cpu*` files, each with its own `φ0`, a grid over

- slot set: CPU-legal vs full command table
- distance: engine vs wall-clock
- `NEED` 1…32, holdoff `BUSY` 0…4
- overwrite: keep scheduled slot / reschedule / drop the new request

has a **six-way plateau**, all **10 / 10** perfect:

| Slots | Distance | NEED | BUSY | Overwrite |
|-------|----------|------|------|-----------|
| CPU-legal | engine | **16** | **2** | keep *or* drop-new |
| CPU-legal | engine | 15 | 3 | keep *or* drop-new |
| CPU-legal | engine | 14 | 4 | keep *or* drop-new |

What that pins down:

- Packed +6 must **not** be in `first_cpu_slot` (full command table never
  reaches 10/10).
- The 16-cycle check is **engine-distance** (stretch subtracted). Wall-clock
  tops out at 9/10.
- After a CPU RAS there is a **2-cycle holdoff**, or an equivalent NEED+BUSY
  pair (16+2 is the documented split: 16-cycle allocation, then CAS).
- If a new request arrives while one is pending, the **scheduled slot does not
  move**. Keep-slot and drop-new are indistinguishable on these traces (Z80 is
  not fast enough to force a visible reschedule). Reschedule is not on the
  plateau. On 2026 sprites-on, drop-new is the one that matches the extra
  `/CSx` pulses; overwrite that *cancels* a granted slot scores far worse
  (70 wrong vs 5 on a 2013 fit that used that alternative).

The same CPU parameters score **7 / 7 mixed files at 100%** if the command is
ignored. A running HMMV never steals or delays a CPU slot.

### 10.3 CPU + HMMV together

2013 mixed traces are HMMV only (screen 5/8, display off / sprites off /
sprites on, read and write bursts). FINDINGS4 HMMV (`P=46`, newline `104`,
sprites-on +1). First observed `W.e` is seeded.

Skip occupied **CPU RAS** only (not dummy tags, not pending windows):

| Occupied set | CPU score | HMMV score |
|--------------|-----------|------------|
| Predicted CPU (`φ0` fit, §9) | **17 / 17** files, every RAS | **7 / 7** mixed |
| Oracle (observed CPU RAS) | — | **7 / 7** |

So on 2013, predicted CPU and oracle CPU are the same for the command: the CPU
model is exact, and skipping fired CPU RAS is enough for HMMV, including the
sprites-off file where the command itself uses packed +6.

That is the **100% 2013** statement: every CPU-containing capture in
`8.final-analysis` that this fitter parses, nocmd and HMMV, matches §9.

## 11. Request time `T` on the two machines

**2013.** `T = φ0 + burst×3060 + k×72`. `φ0` absorbs everything between the
Z80 I/O cycle and the arbiter. No `/CSR` in those captures.

**2026.** Do **not** fit a 72/252 lattice on the VDP timeline. The same Z80
loop is not 72 VDP cycles when the CPU crystal is independent. Intra-burst
`/CSR` gaps are **71 or 72** (typically alternating; same-gap doubles when
the fraction wraps). Loop ~**250.5**. Independent ±1 on neighbouring posts
would produce 70/73; those almost never happen. The uncertainty is **where
the extra/missing cycle sits** — one analog phase for the burst — not a free
±1 on every request.

Integer `δ` in `T = floor(t2) + δ` shifts every request the same amount and
**preserves all gaps**. `T_i = floor(t2_i + ε)` with a fractional `ε` slides
the 71/72 doubles (a +0.1 step changes a subset of posts, never all of them).

Request time is taken from `/CSR` (reads) or `/CSW` (writes) in the matching
`.vcd`, converted onto the VDP clock (refresh interpolation, or `/RAS` grid):

```
T = floor(t2) + δ          # integer fit
T_i = floor(t2_i + ε)      # beat-phase; one ε per file
```

`δ` / `ε` and falling vs rising edge are searched per file. Edges closer than
20 VDP cycles are dropped (analyzer ringing at the ~15-cycle pulse width).
Scoring treats a **D16 tie** as a hit: if `first_cpu_slot(T−1)` or
`first_cpu_slot(T+1)` differs from `first_cpu_slot(T)`, either slot counts;
the simulator still emits `first_cpu_slot(T)`. The capture can start in the
middle of a `/CSx` pulse; that is §11.1, and it is **not** handled as “no
falling edge ⇒ ignore this pulse.”

The bus can measure only the **sum** of `/CSx`→arbitration delay and the
16-cycle lookahead. From rising `/CSx`, the latest request that still won a
slot is **26.19 ± 0.38** memory cycles on display-off (142 slots, 3991
samples). Packed +6 slots are **~2 cycles earlier** (28.01 ± 1.41). NEED=16
is the lookahead on the VDP side; `δ` is when the VDP *sees* the pin. For
emulation the sum matters; the pins cannot split it.

Do **not** treat `/CSR`→`T` as gospel (±1–2 cycles, async crystals). Prefer
**observed CPU RAS** (oracle) when testing arbiter rules.

With integer-`δ` `T`, §9 CPU path on 2026 `rdCpu`/`wrCpu` traces (stop +
command): **12787 / 12846 (99.5%)** D16, 76 / 115 files perfect. Stop
1878 / 1891 (99.3%); mixed 10909 / 10955 (99.6%). Good enough that leftover
**command** errors can be studied with observed CPU RAS, so they are not
blamed on `δ`. A 2026 CPU-slot fit that uses `/CSx` with one offset per
capture still has an ~1.8% residue; more than half of that is moving a
recorded edge by less than one cycle — the async floor, not a third arbiter
rule. Beat-phase `ε` is the right *noise model* for that floor; it is not a
dummy classifier (§13.6). A smaller, non-async cluster of that residue sits
on the **open left edge** of the `/CSx` window (§11.1). The 37 T set (§14)
isolates that residue: interior I/Os match NEED=16, leftover CPU RAS are
decoder / incomplete `/CSx`, not a new wait.

### 11.1 Open left of a 2026 `/CSx` capture

The analyzer only records **edges**. Two different holes at t = 0; neither
is a special case in the fitter today.

**(a) `/CSx` already low at dumpvars `#0`.** 19 / 115 `rdCpu`/`wrCpu` VCDs.
A real I/O pulse is **56 samples ≈ 15 VDP cycles**. 18 of those 19 deassert
2 samples later (capture started in the last ~0.5 cycle of the pulse);
`scr5-sprOff-line-wrCpu-2` deasserts 56 samples later (started at the
falling).

| Decoder | Already-low pin at `#0` |
|--|--|
| Pace-fit (`decode26`) | Rising `0→1` only. Initial state is not assumed idle. The truncated pulse **is** a request, timestamped at the observed rising. The falling that started it is not in the file and is not used. |
| `fit_2026.cc` | `csr`/`csw` start at **1**. An already-low pin records a **synthetic falling at t = 0**. That edge was not captured. For a 2-sample remnant the true falling was ~15 cycles earlier. |

Rising-`T` therefore *does* see the truncated pulse (its end). Falling-`T`
invents a start at capture begin. Neither decoder waits for a complete
falling→rising pair, and neither treats “already active” as “`T` unknown,
buffer may already be occupied.”

This is **not** the START leftover cluster below: every file in that cluster
has `/CSx` **idle** at `#0`. The 19 already-low files are a disjoint set
(some perfect, some leftover elsewhere). A truncated pulse does not line
up with a missing first RAS.

**(b) A complete previous I/O finished before t = 0** (pin already 1 at
`#0`). The first observed CPU RAS then has no `/CSx` in the analog train;
the first recorded pulse grants `obs[1]`.

Pace-fit + D16-tie, NEED=16, BUSY=2, 115 files: **52** perfect, **209**
leftover RAS. **20** files have a leftover in the first ~72 cycles (**26**
events — 12% of leftovers, 32% of leftover *files*).

Two families:

| | Family B (11 files) | Family A (~8 files, mostly dispOff) |
|--|--|--|
| First `/CSx` at `#0` | Idle | Idle |
| First analog gap | 12 T-states (~71.5) | ~108 cycles ≈ **18 T-states**, then 12 |
| First obs RAS vs first analog `/CSx` | RAS **before** `/CSx` | RAS ~72 after the (ghost) analog[0] |
| One extra burst I/O at −12 T | grants `obs[0]` exactly | does not |

Family B example, `scr5-sprOn-hmmm-wrCpu-3` (also `stop`, so no command):

```
first analog /CSx  T = -889  →  first_cpu_slot = -860
observed RAS              -924, -860, -796, …
phantom −12 T        T = -960  →  first_cpu_slot = -924
```

The phantom RAS fires *before* the first recorded `/CSx`, so nothing in the
captured train is lost. Same prepend on all 115 files: perfect **52 → 59**,
leftover **209 → 196**, **0** files worse. A second prepended I/O does
nothing more. That is the last OUT of the burst that was already running
when `/CSx` recording started.

Family A: the pace-fit rounds the 18 T setup/loop-entry gap to 24 T and
plants analog[0] ~22 cycles *before* the real first edge. That ghost would
RAS ~72 cycles before `obs[0]` (clipped out of the scoring window); the
next request grants `obs[1]`. No legal `T` *before* analog[0] maps to
`obs[0]` under NEED=16, and inventing an I/O in the 18 T gap contradicts
the VCD (no edge there). Skipping the first observed RAS “fixes” these
files — that is a **scoring window**, not a VDP rule.

**Command accesses do not explain (b).** `first_cpu_slot` ignores the
engine. The same first-RAS miss appears on `stop` traces. In the `.txt`,
80 / 115 files the first CPU RAS is the first access in the file; a
command before that window would not change which CPU slot is predicted.
Dropping the first `/CSx` as “setup / `#99`” **hurts** (52 → 22 perfect).
Seeding the CPU buffer already holding `obs[0]` matches “skip first RAS”
on family A and is **not** a possible prior CPU post under NEED=16.

Do **not** invent posts in the fitter. Family B is a reason the left edge
of `/CSx` is open, same idea as command traces starting mid-command
(seed from the first *observed* engine RAS). Falling-`T` should not treat
dumpvars 0 as a falling edge at t = 0.

On the 37 T set the same two holes appear, plus a third that only scores as a
CPU miss when a **1-sample** `/CSR` spike sits 0–24 cycles before the first
RAS (§14.3). A 2-sample already-low remnant (`sprOff-hmmv-rdCpu222-5e`) *does*
line up with a START leftover there: rising + typical `δ` lands past the
grant window of `obs[0]`. That is still (a), not a VDP skip.

## 12. 2026 command + CPU: what already fits

Occupancy (RAS), 17 stop + 99 mixed files, CPU ∩ command RAS = **0**:

| | CPU-legal | packed +6 | off-table |
|--|-----------|-----------|-----------|
| CPU RAS | **12846 / 12846** | 0 | 0 |
| Dummy `R..` | 0 | **333 / 333** (sprites-off only) | 0 |
| Command | dispOff/sprOn: all legal | sprOff: **1436 / 4608** packed | 0 |

So 2026 occupancy matches 2013 (§2.1): **0** CPU RAS on packed +6 in both
years; commands may use packed +6 on sprites-off; nobody leaves the command
table.

Engine files, mixed `*rdCpu*`/`*wrCpu*` (no `-stop-`): **84** files, **12544**
engine RAS. `--mismatch`. Scores use FINDINGS4 waits (the 188 newline hack
where it fires). Packed-start +1 (§6) is the `noCpu` replacement for that
hack; mixed A–C were not re-run with +1 on *every* packed start. Occupy =
observed CPU RAS unless a row says otherwise.

| Model | Files | Steps |
|--|--|--|
| A — CPU RAS only | 67/84 | 12447 |
| P5 — last packed, Δ=32, `Snext > C` | 70/84 | 12508 |
| P15 — P5 + newline | 70/84 | 12509 |
| B — CPU RAS + observed `R..` | 78/84 | 12482 |
| D6+P5 — fitted `T−C ≤ −19` (CSR diagnostic) | 78/84 | 12536 |
| D6+P5+newline | 79/84 | 12537 |
| C — `R..` + P5 + newline (oracle occupy) | **84/84** | **12544** |

Older `--scratch` tables that say 85 files include
`scr5-sprOn-hmmm-rdCpu-1` (§7). Occupy-`R..` without P5 is 78/84 files but
**fewer** steps than D6+P5 (12482 vs 12536): the tag is occupancy, not a wait
rule.

| Command × mode | Files perfect (CPU RAS / CPU+`R..` / n) |
|----------------|------------------------------------------|
| HMMV dispOff, sprOn; LMMV all three modes; YMMM dispOff, sprOn; HMMM dispOff; LMMM dispOff, sprOn; LINE dispOff | **all** with CPU RAS only |
| HMMV sprOff | 3 / **6** / 6 |
| YMMM sprOff | 0 / **6** / 6 |
| HMMM sprOff | 3 / 5 / 6 |
| LMMM sprOff | 0 / 0 / 5 |
| HMMM sprOn | 5 / 5 / 6 (`scr5-sprOn-hmmm-rdCpu-1`, §7) |

**Display-off mixed is 100%** with the 2013 joint model (skip CPU RAS). There
are no packed +6 slots in display-off, so leftover class §13 cannot occur.
The 37 T set confirms it on CPU RAS as well: **20/20** display-off files,
**751/751** (§14). Sprites-on is 100% except the misnamed file (also no packed
+6). Sprites-off
**LMMV** is 100% without occupying dummies (LMMV waits rarely land on a packed
+6 the CPU has reserved).

That is the “fits well” part: the 2013 arbiter plus FINDINGS4 is already the
right description whenever the command’s next slot is a **CPU-legal** slot.

## 13. 2026 sprites-off: packed +6 while CPU is pending

The remaining mixed misses all go **the same way**: the model wants a packed
slot, it stays empty or is `R..`, hardware uses a **later** slot. (Stretch /
EOL “hardware earlier than the model” was a Python `n_stretch` bug; with the
C++ stretch count those wraps already hit.)

Two populations of unused packed +6, **only sprites-off**, **only when CPU
VRAM is active** (0 in `noCpu`; `stop` has the same dummies with no command).
They are not one rule. The squeezed cycle has **two** ends:

- Back: engine wait **+1** after a packed access (§6).
- Front: packed slots are decided **~2 cycles earlier** than CPU-legal ones,
  and if the CPU path wins them they cannot carry a CPU access.

### 13.1 Dummy `R..` (333, all `0x1FFFF`)

Every unclassified `R.. 0x1FFFF` that is not part of the blanking four-slot
block sits on one of the 25 packed slots. The next CPU-legal slot is +26
(323) or +54 (10); that slot **is** a CPU RAS in 332 / 333. `C−6` is empty
or a command, never CPU. Zero of these dummies sit anywhere else; zero CPU
RAS sit on packed +6.

The VDP **does** grant the packed slot to the CPU path. That memory cycle
cannot carry a CPU access, so it strobes `0x1FFFF` and the CPU is served one
slot later.

**Not every CPU at `C+26` produces a dummy.** Stop traces (no command): 326
CPU RAS whose predecessor in the table is packed `S−26`, only **42** dummies.
Dummy is rare, not “CPU nearby.” Mixed files: 35 dummy skips vs 49 packed
**hits** with CPU at `C+26` in the same ~32-cycle T-window of the next CPU
RAS. Occupying the observed `R..` tag perfects HMMV/YMMM (model B / C); it is
an oracle, not a predictor.

### 13.2 Dummy vs hit: packed decided 2 cycles earlier

On the full command table, NEED=16 grants packed `C` for arming `T` with
`T−C ∈ {-21…−16}` while the CPU-legal table already grants `C+26`. Hardware
is stricter on packed:

| | `T−C` |
|--|--|
| mixed dummy | **−21, −20, −19**, two at −18 |
| mixed packed hit, CPU at `C+26` | starts at **−18** |
| stop dummy | same early cluster |
| stop empty (CPU at `C+26`, no dummy) | starts at **−18**, **never** −21…−19 |

Stop, cut `−22 < T−C ≤ −19`: dummy **29**, empty **0**. `first_slot` on the
full table with **NEED=19** is exactly `{−21, −20, −19}`.

Same fact from the pin: display-off lead 26.19 ± 0.38; packed-pair slots
28.01 ± 1.41. Uniform 26-cycle lead predicts **472** dummies vs 335 observed;
the extra 2 cycles predicts **336**.

**D6** (diagnostic, needs fitted `/CSR` `T`): skip packed `C` if CPU RAS is
`C+26` and `T−C ∈ (−22, −19]`. Plus P5: **78/84** files, **12536** steps —
same file count as occupy-`R..`, more steps. The −18 overlap is a 1-cycle
tie (2 mixed dummies, 3 mixed hits). Independent ±1 on each `T` does **not**
turn NEED=16 into that cut (hits still grant packed). Majority ±1 with
NEED=19 **is** D6.

Do not put `/CSR` into the promoted model. D6 is the mechanism ceiling:
dummy is an **early** packed grant, not “any pending CPU.”

### 13.3 Idle packed +6 — P5 (no `/CSR`)

Every sprites-off step whose unconstrained FINDINGS4 candidate `C` is packed
+6, versus the next observed CPU RAS `S`. Invert `first_cpu_slot` to a
T-window `[Tmin, Tmax]` for that `S`. `/CSR` is not used.

| Outcome | n | `Tmax ≤ C < S` (must pending) | `Tmin ≤ C < S` (maybe) | else |
|---------|---|-------------------------------|------------------------|------|
| Command **takes** `C` | 1367 | **0** | 49 | 1318 |
| Skip, slot is `R..` | 35 | **0** | **35** | 0 |
| Skip, slot **idle** | 62 | **0** | 58 | 4 |

Nothing sits in “must pending”. Occupying every packed `C` with
`Tmin ≤ C < S` **over-blocks** (67 → 56 perfect files).

**Idle group (58/58 maybe):** last access packed +6, `Δ = 32` (LMMM
`R.s→R.d`), candidate `C` packed. The 49 maybe-**hits** never have `Δ = 32`
(0/49).

**P5** (keep): last packed, `Δ = 32`, candidate `C` packed, skip `C` iff the
**first CPU RAS after `last` is still after `C`** (`Snext > C`).

- If a CPU RAS already sits in `(last, C]` (typically `C−6`, the pair-start):
  command **takes** packed `C` (overflow).
- If `Snext > C`: command **skips** `C` (idle). Often then uses `C+26`; CPU
  uses `C+26` or `C+58`.

P3 (CPU at `C+26` only) is the special case `Snext = C+26`. A “CPU in
`(C, C+64]`” window **false-skips** the `C−6` overflow hit. P5 is **not**
“any CPU later in the file.”

Skipping Δ=32 packed with **no** CPU predicate (P12) is 69/84 mixed and
fires=0 on `noCpu` only because that geometry does not occur there — not
because the engine would survive it. Packed-start +1 (§6) is **not** P12:
on `noCpu` it only moves the 188→342 wraps.

Example, `scr5-sprOff-lmmm-rdCpu-1.txt`: `R.s` at RAS **2068** (row 700),
first legal command slot **2100** (row 732, packed), idle; CPU at **2126**
(row 758); dest-read at **2132** (row 764). P5 skips 2100 (`Snext=2126 > C`).

Overflow counterexample (`lmmm-rdCpu-1` elsewhere): last packed, cand packed,
CPU at `C−6`; P5 **does not** skip; a +64 window would.

**70/84** files, **12508** steps. `noCpu` stays 508/508 (P5 never fires).

### 13.4 Newline idle (opposite polarity)

HMMM newline: `Δ = 128`, last packed, candidate packed, CPU **already** in
`(last, C]`. Skip the packed landing. One extra HMMM step (P15: 12509).
Polarity is the opposite of P5; do not merge them. Packed-start +1 already
covers 188→342 on `noCpu`; this mixed newline is a CPU already sitting in
the wait, not a squeezed-cycle delay.

### 13.5 What does not split dummy from hits

Same T-window, often CPU at `C+26`, same commands (YMMM `w→s` Δ=36 is 24 dummy
vs 27 hit). `C−Tmin` is always 21. G1 (skip packed if any mid-wait CPU other
than `C−6`) **rejects**: 95 packed **hits** have that shape. P8 (skip all
packed if CPU at `C+26`) hurts HMMV/HMMM/YMMM hits.

### 13.6 Beat-phase does not predict dummy

`T_i = floor(t2_i + ε)`, one `ε` per file, does slide 71/72 doubles. Engine
P5+newline + NEED=19 dummy:

| Policy | Files | Steps |
|--|--|--|
| ε = integer δ (= D6) | 79/84 | 12537 |
| ε best for CPU D16 | 78/84 | 12536 |
| ε best for engine (oracle phase) | 82/84 | 12542 |
| dummy if ANY / MAJ / ALL of ε in [ε_D16±1] grant | 79 / 78 / 72 | 12537 / 12536 / 12520 |

D16-best fractional ε is slightly *worse* than integer D6. Voting a ±1 band
is D6 or worse. The engine-oracle phase sits ~1.5 cycles from δ and still
leaves `ymmm-wrCpu-2` (best ε **is** the integer) and `ymmm-wrCpu-3`. Dummy
is not “NEED=16 smeared by beat phase,” and it is not fully “NEED=19 at the
wrong ε.”

### 13.7 Oracle vs mechanism

Occupying **observed** CPU RAS **and** observed packed `R..`, plus P5 and
newline, is **84/84** (model C). An independent engine check that occupies
the same tags and adds packed-start +1 reports 3 leftover transitions, all
in two mislabelled sprites-on files (`scr5-sprOn-hmmm-rdCpu-1`,
`scr5-sprOn-hmmv-wrCpu-3`, §7).

That is occupancy **oracle**, not a closed no-`/CSR` dummy rule. The
promoted mixed engine without the pin is: skip fired CPU RAS, packed-start
+1, **P5**, newline, and (when `T` is known) D6. Five files still miss with
D6+P5+newline (LMMM Δ=60 fringe, −18 ties, unmatched posts).

## 14. Sparse CPU (`rdCpu222`)

A second 2026 program: 20 × `IN` with `EX (SP),HL ; NOP` between them, same
NMS 8280, same slot tables, same `fit_2026` model (§9). Ignore `adjustN` and
`s0`/`s16`/`s32`/`s48` VCDs.

| | T-states | ×6 (sync 8250) | × 5.96112 (8280) |
|--|----------|----------------|------------------|
| IN → IN | 37 | 222 | **220.56** |
| burst wrap (20 INs) | 67 | 402 | **399.40** |

Pace across the VCDs: **5.96112 ± 0.00004** VDP cycles per Z80 T-state,
residual **~0.15** — same crystal ratio as the 12 T set. Gaps in the VCD are
220/221 and 399/400, not 72/252.

`first_cpu_slot` wait is at most **59 / 75 / 85** (dispOff / sprOff / sprOn).
That is far below 220, so the next IN always arrives with the buffer empty.
**sim-lost = 0.** `FREE_BEFORE=9` is not tested.

Rename: `5.slots/scr5-dispOff-hmmv-rdCpu222-7e.txt` ↔
`1.vcd/scr5-sprOff-hmmv-rdCpu222-3e.vcd` (same for 8e ↔ 4e). See §7.

### 14.1 Scores (`fit_2026`, integer `δ`, D16, 7e/8e paired)

53 txt with CPU tags (54 files minus `sprOn-hmmm-3e`). Typical `δ` **9–14**
on the rising edge.

| | files perfect | CPU RAS |
|--|---------------|---------|
| **display-off** | **20/20** | **751/751 (100%)** |
| sprites-off | 13/16 | 605/608 (99.5%) |
| sprites-on | 14/17 | 617/642 (96.1%) |
| CPU + command | 31/35 | 1322/1326 (99.7%) |
| stop | 16/18 | 651/675 |
| all CPU | 47/53 | 1973/2001 (98.6%) |

Engine (36 files with command tags), skip observed CPU RAS, P5:

| | files | steps |
|--|-------|-------|
| occupy CPU RAS | 35/36 | 5429/5456 |
| occupy packed `R..` too | **36/36** | **5456/5456** |
| HMMV | **18/18** | 2546/2546 (CPU RAS enough; no dummy occupy) |
| HMMM | 17/18 RAS-only; **18/18** with `R..` | `sprOff-hmmm-4e` is the dummy-tag file |

### 14.2 Dummy is geometry, not crowding

Sprites-off packed +6 dummies: **~7%** of CPU RAS (43 / 608), vs **~7.8%** on
the old 12 T reads. Every one has the CPU RAS at **packed+26**. Same rate
whether INs are 72 or 220 apart. Crowding did not create dummies.

### 14.3 Leftover CPU RAS are measurement / decoder

Do **not** change NEED, BUSY, or the slot tables to fit these. Analog `T` for
interior I/Os sits in the NEED=16 grant window (or 1 cycle off: D16). A
“skip first slot” rule fitted to START ±8/±32/±64 **breaks** the clean files.

**Tagging.** `scr5-sprOn-hmmm-rdCpu222-3e`: 49 real `/CSR` pulses (55–56
samples), `.txt` has no `R.r` (only `R.s` / `W.d`). CPU was running; 5.slots
never labelled those reads. Same as `scr5-sprOn-hmmm-rdCpu-1` (§7). Engine
129/129 because those RAS are not in the CPU set.

**1-sample `/CSR` spike.** A real I/O is **56 samples ≈ 15 VDP cycles**.
1-sample spikes are common (**29 / 53** files). `CSX_MIN_GAP=20` already
drops ringing next to a real edge, and `make_posts` drops a grant before
`tmin`. That is enough when the spike is 100–300 cycles from the first RAS
(those files stay perfect). It is **not** enough when the spike sits
**0–24 cycles before** `obs[0]`: the fitter treats it as a request, `T` is a
few cycles late, and the model emits the **next** CPU slot. `/CSR` is idle
at `#0` (not a truncated pulse).

| file | spike → first RAS | leftover (one slot) |
|------|-------------------|---------------------|
| `sprOff-hmmm-2e` | 17 | 78 vs 86 (±8) |
| `sprOff-stop-2e` | 24 | 790 vs 822 (±32) |
| `sprOn-hmmv-2e` | 14 | 476 vs 508 (±32) |
| `sprOn-hmmv-6e` | 0.3 | 892 vs 956 (±64) |

The first RAS belongs to an I/O whose `/CSx` is not in the file (family B).
The rest of each file fits one `δ`. Dropping pulses ≲ 8 samples leaves a
single unmatched first RAS and no extras — open left, not a VDP skip.

**Already-low remnant.** `sprOff-hmmv-5e`: pin low at `#0`, **2-sample**
remnant, first RAS 17 cycles after the rising. The rising is the end of a
real I/O that started before capture (§11.1(a)). Rising + `δ≈10` is **9
cycles past** the grant window for RAS 374 → extra 406. Same one-slot START
leftover.

**Cascade.** `sprOn-stop-3e`: `fit_2026` **13/36**, `δ` hits the search cap
of 40. Drop the 1-sample spike → **36/36**, interior residual 0.16. Integer-`δ`
kept the spike as an extra post.

**Pace-fit vs D16** (already a hit in `fit_2026`): stretch 1360 vs 0
(`dispOff-hmmm-3e`, edist 15 vs NEED=16); dispOff 48 vs 56
(`dispOff-hmmv-1e`); sprites-on wrap 1330 vs 28. Analog `T` is in the window;
`first_slot(T±1)` is the other slot.

### 14.4 What this does not change

The VDP model stays §9. 100% on this set is a `/CSx` decoder problem: drop
sub-width pulses, do not synthesize a falling at `#0`, do not score a RAS
whose complete `/CSx` was never recorded (same policy as command traces
starting mid-command). Do not invent posts.

Still untested / unsolved, as in §15:

- Dummy without the pin / `R..` tag (rate did not split).
- `FREE_BEFORE` and lost requests (need I/O closer than ~85 VDP cycles).
- Family A (18 T first gap) — this program is 37/67 T.
- Packed-start +1 on 2013; command startup `S`.

## 15. What is solid vs open

**Solid (2013 100%; 2026 command-only 100% with §6; 2026 mixed whenever the
next command slot is CPU-legal; 2026 display-off CPU 100% on `rdCpu222`):**

- FINDINGS4 waits, stretch, sprites-on +1, packed-start +1 (§6).
- CPU-legal = command table minus packed +6 (grid property, both years).
- `first_cpu_slot` with engine-distance ≥ 16; one buffer; new post while
  occupied is lost; holdoff `[RAS, RAS+2)`.
- Command skips fired CPU RAS; may use packed +6; never delays CPU; loser
  takes the next slot (no full re-arm).
- P5: last packed, Δ=32, skip packed `C` iff `Snext > C`.
- Dummy `R..` on packed +6 is a CPU-path grant the CPU cannot carry; CPU RAS
  follows at +26 (rarely +54). Rate is the same at 12 T and 37 T (§14.2).
- 2026 sprites-on extra `/CSx` vs RAS on the 12 T burst is occupancy (buffer
  still busy). Confirmed empty-buffer at 37 T: sim-lost = 0 (§14).

**Open:**

1. **Predict dummy without `/CSR` / without the `R..` tag.** D6/NEED=19 /
   packed lead +2 is the mechanism; it still needs arming `T`. Beat-phase
   and per-request ±1 do not replace it. Model C (occupy the tag) is 84/84
   and is not a rule. `rdCpu222` did not split dummy from hits.
2. **Five mixed files** still imperfect with D6+P5+newline (12 T set).
3. **2026 CPU `T`** on the 12 T set is 99.5% D16, ~1.8% residue on a `/CSx`
   state machine — async floor, not a third mux. Register free ~9 before RAS
   is measured for sprites-on losses; 2013 17/17 did not need it; **37 T does
   not test it**. Part of the residue is the open left of `/CSx` (§11.1,
   §14.3): a previous burst I/O (family B), an 18 T first gap (family A), a
   1-sample spike next to `obs[0]`, or a truncated remnant timestamped as a
   normal rising. `fit_2026.cc` still synthesizes a falling edge at t = 0
   when the pin is already low. Interior 37 T I/Os do not need a new NEED.
4. Packed-start +1 untested on 2013 sprites-off HMMM/LMMM (no such traces).
   Dummy `R..` may be 8280-only; 2013 mixed is HMMV only.
5. Command startup `S`; character / text / MSX1 tables — unchanged from FINDINGS4.
   S0/S1 and set-adjust: [`FINDINGS-ADJUST-S10.md`](FINDINGS-ADJUST-S10.md) (parked;
   last sprites-on command slot ±2, 1365 = −3 in HBLANK). Display-off still unmeasured.
