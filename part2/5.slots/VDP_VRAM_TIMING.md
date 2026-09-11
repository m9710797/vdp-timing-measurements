# V9938 VRAM timing and arbitration

This document was written with assistance from an AI coding agent.

This document specifies the timing behaviour of V9938 command-engine and CPU
VRAM accesses. It is intended as an implementation specification: it states
what to emulate, without explaining how the rules were established.

## 1. Scope and conventions

The exact scope is:

- V9938 bitmap modes, in particular screen 5 and screen 8;
- V9938 G1/G2/G3 CPU slots (commands remain bitmap-only);
- display disabled, sprites disabled, and sprites enabled;
- command-engine access timing;
- CPU VRAM request timing, buffering, and arbitration with commands;
- the default 1368-cycle line, plus the known R#9 and R#18 effects;
- the border/display transition.

Text, undocumented, and MSX1 modes require other slot tables and are outside
this specification. G1/G2/G3 CPU timing is included in §7.1; V9938 commands
remain bitmap-only.

All times are VDP master-clock cycles. Slot tables contain the `/RAS` falling
edge, i.e. the start of a VRAM access. Absolute slot times repeat every line:
for every table entry `s`, slots exist at `s + n * LINE`.

`ioAccessTime` means the timestamp at which the Z80 emulation issues the
I/O-port read or write to port `#98`. This specification assumes the openMSX
timing convention, where that timestamp is at the start of T2 of the Z80 I/O
machine cycle. An emulator using a different callback point must translate its
timestamp to this convention. Convert it to the VDP's integer master-clock
coordinate before applying the rules below.

`/CAS` normally falls one cycle after `/RAS`. The following accesses have a
two-cycle RAS-to-CAS delay:

- display disabled: RAS 1324 and 1334, hence CAS 1326 and 1336;
- sprites disabled: RAS 1322 and 1332, hence CAS 1324 and 1334.

## 2. Overview

The VDP uses two cooperating schedulers:

- A CPU VRAM request is accepted or discarded, then assigned to a CPU-legal
  slot. A running command does not influence that assignment.
- The command engine becomes eligible after a command-specific delay, then
  uses the first command slot not occupied by the CPU path.

CPU accesses have priority over command accesses. A CPU access can therefore
delay a command, but a command never delays a CPU access. If a command loses a
slot, it continues at the next command slot without restarting its delay.

The CPU request path has one waiting position. An accepted access never moves.
There can briefly be two future CPU access events while one is entering the
grant pipeline and the next occupies the waiting position; this is not a
two-entry request queue.

The details are divided as follows:

- section 3: exact slot tables;
- section 4: memory-cycle distance and line stalls;
- section 5: command-engine timing;
- sections 6 and 7: CPU requests, buffering, and packed-slot dummies;
- sections 8–10: non-default line timing and display boundaries;
- section 11: self-contained reference pseudocode.

## 3. Default slot tables

These tables apply to a centred display (`R#18 = 0`) and `R#9 S1,S0 = 0,0`,
for which `LINE = 1368`.

### 3.1 Display disabled

There are 154 command slots. All 154 are also CPU-legal.

```
   0,    8,   16,   24,   32,   40,   48,   56,   64,   72,
  80,   88,   96,  104,  112,  120,  164,  172,  180,  188,
 196,  204,  212,  220,  228,  236,  244,  252,  260,  268,
 276,  292,  300,  308,  316,  324,  332,  340,  348,  356,
 364,  372,  380,  388,  396,  404,  420,  428,  436,  444,
 452,  460,  468,  476,  484,  492,  500,  508,  516,  524,
 532,  548,  556,  564,  572,  580,  588,  596,  604,  612,
 620,  628,  636,  644,  652,  660,  676,  684,  692,  700,
 708,  716,  724,  732,  740,  748,  756,  764,  772,  780,
 788,  804,  812,  820,  828,  836,  844,  852,  860,  868,
 876,  884,  892,  900,  908,  916,  932,  940,  948,  956,
 964,  972,  980,  988,  996, 1004, 1012, 1020, 1028, 1036,
1044, 1060, 1068, 1076, 1084, 1092, 1100, 1108, 1116, 1124,
1132, 1140, 1148, 1156, 1164, 1172, 1188, 1196, 1204, 1212,
1220, 1228, 1268, 1276, 1284, 1292, 1300, 1308, 1316, 1324,
1334, 1344, 1352, 1360
```

### 3.2 Bitmap mode, sprites disabled

There are 88 command slots:

```
   6,   14,   22,   30,   38,   46,   54,   62,   70,   78,
  86,   94,  102,  110,  118,  162,  170,  182,  188,  214,
 220,  246,  252,  278,  310,  316,  342,  348,  374,  380,
 406,  438,  444,  470,  476,  502,  508,  534,  566,  572,
 598,  604,  630,  636,  662,  694,  700,  726,  732,  758,
 764,  790,  822,  828,  854,  860,  886,  892,  918,  950,
 956,  982,  988, 1014, 1020, 1046, 1078, 1084, 1110, 1116,
1142, 1148, 1174, 1206, 1212, 1266, 1274, 1282, 1290, 1298,
1306, 1314, 1322, 1332, 1342, 1350, 1358, 1366
```

The following 25 entries are **packed slots** and are command-only:

```
188, 220, 252, 316, 348, 380, 444, 476, 508, 572, 604, 636,
700, 732, 764, 828, 860, 892, 956, 988, 1020, 1084, 1116,
1148, 1212
```

Each packed slot is six cycles after another command slot. The CPU-legal table
is the 88-entry table with those 25 entries removed, leaving 63 entries.

### 3.3 Bitmap mode, sprites enabled

All 31 command slots are CPU-legal:

```
  28,   92,  162,  170,  188,  220,  252,  316,  348,  380,
 444,  476,  508,  572,  604,  636,  700,  732,  764,  828,
 860,  892,  956,  988, 1020, 1084, 1116, 1148, 1212, 1264,
1330
```

## 4. Memory-cycle distance

Command delays and CPU lookahead are expressed in memory-cycle time, not
unconditionally in wall-clock cycles. Define:

```cpp
memoryCycleDistance(a, b) =
    (b - a) - sum(extra padding completed in the interval (a, b])
```

Use absolute, unwrapped times, so `b > a`. Padding repeats each line.
For comparisons that can point backward, define:

```cpp
signedMemoryCycleDistance(a, b) =
    b >= a ?  memoryCycleDistance(a, b)
           : -memoryCycleDistance(b, a)
```

For the default 1368-cycle line:

| mode | padding completions |
|------|---------------------|
| display disabled | +2 at row 1334, +2 at row 1344 |
| sprites disabled | +2 at row 1332, +2 at row 1342 |
| sprites enabled | +2 at row 1330, +1 at row 1337, +1 at row 1348 |

The total padding is four cycles in every mode. The 44-cycle gap from row 120
to 164 in the display-disabled table is not padding and counts in full.

Padding belongs to each interval independently. Restart the delay after every
engine access; do not permanently remap the line coordinate.

## 5. Command engine

### 5.1 Access sequence and delays

The notation is `ACCESS wait (+minor)`: after `ACCESS`, wait `wait` memory
cycles before the next access. Add the parenthesized amount when the command
also advances one unit in its minor direction on that transition.

```
HMMV  : W 46 (+58)
LMMV  : R 24  W 72 (+58)
YMMM  : R 24  W 36 (+68)
HMMM  : R 24  W 60 (+68)
LMMM  : R 32  R 24  W 60 (+68)
LINE  : R 24  W 84 (+36)
```

For HMMV, LMMV, YMMM, HMMM, and LMMM, a minor-direction advance is the
transition from the end of one command row to the start of the next. For
`LINE`, it is **not** a display-line or rectangle-line boundary: add 36
whenever the line algorithm takes a step in the minor direction.

All addends are independent and cumulative:

```
effectiveWait = base wait
if this transition advances in the minor direction:
    effectiveWait += parenthesized addend
if sprites are enabled:
    effectiveWait += 1
if the preceding engine access used a packed slot:
    effectiveWait += 1
```

The packed-slot rule therefore adds exactly one. For example, HMMV after a
packed slot waits 47 normally, or `46 + 58 + 1 = 105` on a minor-direction
transition. It does not add an additional one merely because the
minor-direction addend also applies.

### 5.2 Selecting the next command slot

After an engine access at absolute RAS time `previous`:

```
delay = delayForCurrentCommandStep()  // Base wait plus any minor-step addend.

if spritesEnabled:
    delay += 1

if previous was a packed slot:
    delay += 1

candidate = first command slot S after previous for which
            memoryCycleDistance(previous, S) >= delay

while candidate is occupied by a CPU access or CPU-path dummy:
    candidate = next command slot

book candidate
```

When an occupied candidate is skipped, continue with the next command slot.
Do **not** restart the command delay from the occupied slot.

The command scheduler must not influence the CPU scheduler. A command booking
is therefore provisional with respect to a later CPU request: if that request
books the same slot, cancel only the command booking and move the command to
the next command slot, without re-arming its delay. An event-driven
implementation may instead defer the final command decision until the
candidate slot is reached.

### 5.3 Command start

The first command access is the first command slot `S` satisfying

```cpp
delay = S0
if spritesEnabled:
    delay += 1   // section 5.1; same extra as every later step
memoryCycleDistance(ioAccessTime, S) >= delay
```

where `ioAccessTime` is the emulator-level R#46 port-write timestamp defined in
section 1. The base `S0` depends on the command, not on the display mode:

```
LMMM  64   // first access: source read
LMMV  88   // dest read
HMMM 100   // source read
YMMM 100   // source read
HMMV 112   // dest write
LINE 112   // dest read
```

These are 18 cycles more than the same thresholds measured from the rising
`/CSW` edge (46, 70, 82, 82, 94, 94), which is the start-of-T2 to pin-rise
offset of section 6.1. Apply the section 5.1 sprites-enabled addend to this
wait as to every other command delay. That is what makes the sprites-on HMMM
launches, which intersect at 83..84 from `/CSW` rather than 82, agree with
display-off. A launch whose `/CSW` edge sits on a cycle boundary can ceil the
other way and miss by one; do not invent an additional startup-only mode term.

## 6. CPU VRAM requests

### 6.1 Request time

Let `ioAccessTime` be the emulator-level port-`#98` access timestamp defined in
section 1, expressed in the same absolute VDP-cycle coordinate as the slot
tables. Schedule the internal CPU VRAM request at:

```cpp
constexpr int CPU_REQUEST_DELAY = 29;
T = ioAccessTime + CPU_REQUEST_DELAY;
```

Reads and writes use the same delay. Display mode and R#18 do not alter it. The
29 cycles include both the remainder of the Z80 I/O operation after the
emulator callback and the VDP's internal request-registration delay.

### 6.2 Slot classes

Each CPU-legal slot has a lookahead returned by `need()` and a release
threshold returned by `threshold()`.
An implementation using the static tables does not need the internal signal
names; annotate the slots as follows:

| mode and slot | class | `need()` | `threshold()` |
|---------------|-------|----------|---------------|
| every display-disabled slot | `Plain` | 16 | +1 |
| every CPU-legal sprites-disabled slot | `Plain` | 16 | +1 |
| sprites-enabled rows 162 and 170 | `Plain` | 16 | +1 |
| sprites-enabled rows 188, 220, 252, 316, 348, 380, 444, 476, 508, 572, 604, 636, 700, 732, 764, 828, 860, 892, 956, 988, 1020, 1084, 1116, 1148, 1212 | `Late` | 18 | −1 |
| sprites-enabled rows 28, 92, 1264 and 1330 | `Late` | 18 | −1 |

`need()` is evaluated for the candidate slot. `threshold()` is evaluated for
the previously booked CPU slot.

Equivalently, if the implementation already exposes the slot attribute `ras0`:

```
int need(CpuSlot slot)
{
    return 16 + 2 * slot.ras0;
}

int threshold(CpuSlot slot)
{
    return 1 - 2 * slot.ras0;
}
```

### 6.3 Accepting or discarding a request

The CPU path has one request buffer. Let `previousCpuSlot` be the slot booked
for the preceding accepted request. For the first request after reset there is
no threshold check. Otherwise:

```cpp
accept request iff
    signedMemoryCycleDistance(previousCpuSlot->time, T)
        >= threshold(*previousCpuSlot)
```

This must use signed memory-cycle distance, not raw wall-clock subtraction.
In particular, a request three wall-clock cycles before sprites-enabled row
1330 is only one engine cycle before it because two RCC padding cycles complete
there. The comparison therefore uses `-1`, without a sprite-specific rule.

If the request is discarded:

- retain the already booked access;
- do not schedule another access;
- do not advance the VRAM address;
- do not alter `previousCpuSlot`.

This comparison is also the busy rule for a request arriving before an
already booked access. Do not add a second queue entry, overwrite the booked
slot, or reschedule it.

The inequality is the authoritative representation of the one-entry request
buffer. In a class with a negative threshold, a new request can be accepted
shortly before `previousCpuSlot`; this can leave two future access events in
the scheduler for a brief period. Do not reject such a request merely because
the older event has not fired yet.

If the request is accepted, select:

```
S = first CPU-legal slot at or after T for which
    memoryCycleDistance(T, S) >= need(S)
```

Book `S` immediately. Once booked, it never moves. At `S`, perform the CPU
VRAM access even if the command engine also wants the slot; the command engine
must skip it.

The comparison is inclusive (`>=`) in both formulas.

## 7. Packed-slot dummy access

In sprites-disabled bitmap mode the CPU itself never accesses a packed slot.
A packed slot `C` is the continuation tick of a two-tick-high CPU-slot
waveform whose rising edge is at `P = C - 6`. The grant circuit responds only
to the rising edge, so `C` is never a separate CPU grant. In a narrow request
window, however, `C` is reserved with a dummy read before the CPU is served at
the next CPU-legal slot.

After booking a real CPU access at `S`, let `C` be the immediately preceding
command slot. If all conditions below hold:

- `C` is packed;
- `S` is the next rising-edge CPU slot after `C` (normally `C + 26`;
  `C + 54` for packed row 1212);
- `-22 < T - C <= -18`;

then:

1. reserve `C` against the command engine;
2. perform a dummy VRAM read from address `0x1FFFF` at `C`;
3. leave the real CPU operation booked at `S`;
4. do not treat the dummy as completion of the CPU request and do not advance
   the CPU VRAM address for it.

CI simulation supplies a likely mechanism: after missing `C-6`, the request
buffer becomes `waiting` at `C` over `-22 < T-C <= -18` (`need = 18`). The
published command/address ownership path is absent, so the connection from
`waiting` to command suppression and `0x1FFFF` cannot be traced end to end.

The boundary is not perfectly sharp. Classifying observed dummies by integer
`T-C` gives peaks at −21..−19 with a few at −18; non-dummies start at −18.
Replaying the trellis `.cpureq` request times against the 603 sprites-off
dummy reads (`C - T` in cycles) gives **585 / 603** for the window **18..21**,
**449 / 603** for **19..21**, and **603 / 603** with **134** false positives
for **17..21**. Total error is 29 at the 18 boundary against 155 at 19, so
**−18 is the best integer cutoff**, with about eleven dummies predicted that
did not happen. [`sndpl/openMSX#9`](https://github.com/sndpl/openMSX/pull/9)
implements this window.

Blanking-region dummy reads outside the command/CPU slot table are a separate
display-pipeline behaviour.

## 7.1 G1/G2/G3 CPU slots

V9938 G1, G2 and G3 share this CPU slot table:

```
32, 96, 166, 174, 188, 220, 252, 316, 348, 380, 444, 476, 508,
572, 604, 636, 700, 732, 764, 828, 860, 892, 956, 988, 1020,
1084, 1116, 1148, 1212, 1268, 1334
```

Rows 166 and 174 are `Plain` (`ras0=0`); all other rows are `Late`
(`ras0=1`). Apply the same `need(slot)`, `threshold(previousSlot)`, buffering
and drop rules as in bitmap modes. This table remains active when sprites or
display output are disabled.

This does not enable the V9938 command engine in these modes. V9958 can enable
non-bitmap commands with R#25 bit 6; their command execution timing is outside
this specification.

## 8. R#9 line-length modes

`S1,S0` are R#9 bits 5 and 4. With `S1,S0 = 0,0`, use `LINE = 1368`
and the tables above.

For any of the other three S1/S0 encodings, use `LINE = 1365`. Known bitmap
table changes are confined to the line-end tail:

### Sprites disabled

Rows up to and including 1322 are unchanged. Replace:

```
1332, 1342, 1350, 1358, 1366
```

with:

```
1331, 1339, 1347, 1355, 1363
```

The remaining padding is +1, completed at row 1331.

### Sprites enabled

Rows up to and including 1264 are unchanged. Replace command/CPU row 1330
with row 1329. The remaining padding is +1, completed at row 1329.

### Display disabled

Rows up to and including 1324 are unchanged. Replace:

```
1334, 1344, 1352, 1360
```

with:

```
1333, 1341, 1349, 1357
```

The remaining padding is +1, completed at row 1333.

This display-disabled tail is gate-derived; unlike the two sprite-mode tails,
it has not yet been measured on V9938 hardware.

## 9. R#18 horizontal set-adjust

R#18 does not change the line length, the CPU request delay, or the total
padding. It shifts the line-end timing pattern by four VDP cycles per signed
four-bit horizontal-adjust unit:

```
signedH = signExtend4(R18 & 0x0f)   // 0..7, -8..-1
shift   = 4 * signedH
```

Only the HBLANK tail is affected; the active-display slots remain fixed.
Even adjustments move the two-cycle padding groups by complete memory cycles.
Odd adjustments place the stall halfway between those groups and distribute
the four extra clocks as `+1, +2, +1` over three memory cycles.

For example, in display-disabled mode:

```
H=0:  1324 -10- 1334 -10- 1344
H=1:  1324  -9- 1333 -10- 1343  -9- 1352
H=2:  1324  -8- 1332 -10- 1342 -10- 1352
H=6:  1340  -8- 1348 -10- 1358 -10- 1368
H=-2: 1308  -8- 1316 -10- 1326 -10- 1336
```

An exact implementation can generate every R#9/R#18 combination from the
341-tick Memory-PLA sequence:

```cpp
signedH     = signExtend4(R18 & 0x0f);
stallFirst  = 326 + signedH;
stallCount  = ((R9 >> 4) & 3) == 0 ? 4 : 1;

cycle = 0;
for (tick = 0; tick != 341; ++tick) {
    high = 2 + (tick >= stallFirst && tick < stallFirst + stallCount);
    subslot0Time = cycle;
    subslot1Time = cycle + high;
    emit the tick's slots at the selected sub-slot time;
    cycle += high + 2;
}
```

Apply the same mode origins used by the centred tables: +20 cycles for display
disabled and +18 for either sprite mode, modulo the resulting line length.
`ika9958/stall.py::rows()` is executable reference code. In sprites-disabled
mode it emits all 88 command slots; remove the unchanged 25 packed slots for
the CPU-legal table.

The separate cases are measurement-validated: all 16 R#18 values at S=00, and
all three non-zero S encodings at H=0. Their combination follows directly from
the two independent hardware controls above but has not been measured.

For a first implementation, the VRAM slot scheduler may ignore the effects of
both R#9 and R#18: always use the centred, 1368-cycle slot tables and padding
from sections 3 and 4. The registers' other effects can still be emulated
elsewhere. This approximation only affects VRAM timing in non-default
configurations; it does not change the default arbitration model.

## 10. Border/display transition

The access-table transition is tied to sprite fetching, which starts one line
before the corresponding pixels are displayed.

- Normal border lines use the display-disabled grid.
- In the border line immediately before the display:
  - the beginning of the line uses the sprites-disabled left comb;
  - from cycle 164 onward, use the sprites-enabled grid;
  - sprite attribute and pattern reads are real;
  - bitmap reads are dummy reads from `0x1FFFF`.
- Display lines use the normal selected bitmap grid.
- On the last display line, keep the sprites-enabled grid. Bitmap reads remain
  real, while sprite-attribute and trailing HBLANK sprite reads are dummy.
- In the border line immediately after the display:
  - through the left part of the line, retain the sprites-enabled comb but
    make its accesses dummy;
  - from cycle 164 onward, use the display-disabled grid.

The switch is at the **same position at both borders**, somewhere in
**(126, 162]**: a different **line origin for the access grid**, not a
different number of lines. Per-line calibration shows the pre-display line runs
the sprites-off left comb (6, 14, … 118) and from 164 onward the sprites-on
comb with dummy bitmap reads; the post-display line runs the sprites-on fetch
comb through ~126 (all dummy) and the display-off comb from 164. A global
refresh fit smears this to “at the line boundary”; that was wrong
([`sndpl/openMSX#5`](https://github.com/sndpl/openMSX/pull/5) discussion, 11
Sep 2026).

On the line before the display area, hardware exposes **44** command/CPU slots
to the engine; openMSX currently gives **154** on that line — about **110 slots
too many** for one line per frame. Switching a whole line early would not fix
that; the table must change at cycle **164** inside the line. The renderer and
sprite checker should keep their existing line boundary; only the slot
scheduler needs a separate switch moment. Not implemented in openMSX yet.

One detail even a correct switch does not capture: the left blanking of the
pre-display line uses the sprites-off comb, not the display-off one — fifteen
slots where display-off has sixteen, six cycles later — because that line
fetches no sprite patterns for the border line above it. A fourth table for one
blanking region per frame is not worth it.

## 11. Reference scheduling pseudocode

This section collects the implementation-oriented pseudocode in one place.
The container types are fixed-size state, not general-purpose request queues.

### 11.1 Types, constants, and persistent state

```cpp
using Cycle = int64_t; // Absolute time in VDP master-clock cycles.

constexpr int DEFAULT_LINE = 1368;
constexpr int CPU_REQUEST_DELAY = 29;

enum class CpuSlotClass {
    Plain,
    Late,
};

struct CpuSlot {
    Cycle time;             // Absolute RAS time, including the line number.
    CpuSlotClass cpuClass;  // Selects need() and threshold().
};

struct CommandSlot {
    Cycle time;  // Absolute RAS time, including the line number.
    bool packed; // True for a sprites-disabled packed +6 slot.
};

// Clear all five optional values on reset.
std::optional<CpuSlot> previousCpuSlot;
// Most recently accepted CPU slot; retained even after that access fires.

std::optional<CpuSlot> nextCpuAccess;
// Earliest accepted CPU access that has not fired yet.

std::optional<CpuSlot> followingCpuAccess;
// One later CPU access during the brief grant/waiting overlap.

std::optional<CommandSlot> nextCpuDummy;
// The one possible future packed-slot dummy read.

std::optional<CommandSlot> bookedCommandAccess;
// The next command access; provisional because a later CPU may steal it.
```

There is at most one booked command access and at most one booked dummy. There
can briefly be two future real CPU accesses: the first is already in the grant
pipeline while the second occupies the single waiting position. No dynamic
container or sorting is required.

### 11.2 Timing and slot helpers

```cpp
int need(CpuSlot slot)
{
    switch (slot.cpuClass) {
    case CpuSlotClass::Plain:              return 16;
    case CpuSlotClass::Late:               return 18;
    }
}

int threshold(CpuSlot previousSlot)
{
    switch (previousSlot.cpuClass) {
    case CpuSlotClass::Plain:              return +1;
    case CpuSlotClass::Late:               return -1;
    }
}

int memoryCycleDistance(Cycle from, Cycle to)
{
    // The interval is (from, to]: exclude from, include to.
    return (to - from) - stallCyclesCompletedBetween(from, to);
}

int signedMemoryCycleDistance(Cycle from, Cycle to)
{
    return to >= from ?  memoryCycleDistance(from, to)
                      : -memoryCycleDistance(to, from);
}

bool occupiedByCpuPath(Cycle time)
{
    return (nextCpuAccess       && nextCpuAccess->time       == time) ||
           (followingCpuAccess && followingCpuAccess->time == time) ||
           (nextCpuDummy       && nextCpuDummy->time       == time);
}
```

`stallCyclesCompletedBetween()` applies the mode-specific padding table from
section 4. The slot-iteration helpers use the periodic tables from section 3.

### 11.3 Booking helpers

```cpp
void moveCommandToNextFreeSlotWithoutRearming()
{
    if (!bookedCommandAccess) return;

    CommandSlot slot = nextCommandSlot(*bookedCommandAccess);
    while (occupiedByCpuPath(slot.time)) {
        slot = nextCommandSlot(slot);
    }
    bookedCommandAccess = slot;
}

void bookCpuAccess(CpuSlot slot)
{
    if (!nextCpuAccess) {
        nextCpuAccess = slot;
    } else {
        // Accepted slots are strictly chronological. Before a third access
        // can be accepted, nextCpuAccess must already have fired.
        assert(!followingCpuAccess);
        assert(nextCpuAccess->time < slot.time);
        followingCpuAccess = slot;
    }

    if (bookedCommandAccess &&
        bookedCommandAccess->time == slot.time) {
        moveCommandToNextFreeSlotWithoutRearming();
    }
}

void bookCpuDummy(CommandSlot slot)
{
    assert(!nextCpuDummy);
    nextCpuDummy = slot;

    if (bookedCommandAccess &&
        bookedCommandAccess->time == slot.time) {
        moveCommandToNextFreeSlotWithoutRearming();
    }
}

void bookCommandAccess(CommandSlot slot)
{
    // The command delay was checked by the caller. CPU occupancy can only
    // move this booking later; it does not restart that delay.
    while (occupiedByCpuPath(slot.time)) {
        slot = nextCommandSlot(slot);
    }
    bookedCommandAccess = slot;
}

void maybeBookPackedDummy(Cycle requestTime, CpuSlot cpuSlot)
{
    CommandSlot candidate = commandSlotImmediatelyBefore(cpuSlot.time);
    int margin = requestTime - candidate.time;

    if (candidate.packed &&
        cpuSlot.time == nextCpuSlotAfter(candidate.time) &&
        -22 < margin && margin <= -18) {
        bookCpuDummy(candidate);
    }
}
```

### 11.4 Event handlers

```cpp
void onCpuPortAccess(Cycle ioAccessTime)
{
    Cycle requestTime = ioAccessTime + CPU_REQUEST_DELAY;

    if (previousCpuSlot &&
        signedMemoryCycleDistance(previousCpuSlot->time, requestTime) <
            threshold(*previousCpuSlot)) {
        return; // Discard this request without changing any scheduler state.
    }

    CpuSlot slot = firstCpuLegalSlotAtOrAfter(requestTime);
    while (memoryCycleDistance(requestTime, slot.time) < need(slot)) {
        slot = nextCpuLegalSlot(slot);
    }

    previousCpuSlot = slot;
    bookCpuAccess(slot);
    maybeBookPackedDummy(requestTime, slot);
}

void afterCommandAccess(CommandSlot previous, CommandStep step)
{
    int delay = commandDelay(step); // Includes the minor-step addend when applicable.
    if (spritesEnabled()) delay += 1;
    if (previous.packed) delay += 1;

    CommandSlot slot = nextCommandSlot(previous);
    while (memoryCycleDistance(previous.time, slot.time) < delay) {
        slot = nextCommandSlot(slot);
    }

    bookCommandAccess(slot);
}

void atSlot(Cycle time)
{
    if (nextCpuAccess && nextCpuAccess->time == time) {
        performCpuAccess();
        nextCpuAccess = followingCpuAccess;
        followingCpuAccess.reset();
        // previousCpuSlot intentionally remains unchanged.
    } else if (nextCpuDummy && nextCpuDummy->time == time) {
        readVram(0x1ffff);
        nextCpuDummy.reset();
    } else if (bookedCommandAccess &&
               bookedCommandAccess->time == time) {
        CommandSlot slot = *bookedCommandAccess;
        bookedCommandAccess.reset();
        std::optional<CommandStep> nextStep = performCommandAccess();
        if (nextStep) afterCommandAccess(slot, *nextStep);
    }
}
```

Event processing must preserve already booked CPU priority. If a CPU event and
a command decision refer to the same slot, the command sees that slot as
occupied and advances to the next command slot.

## 12. Known incomplete areas

The following are not fully specified and must not be hidden as exact rules:

1. What the leftover integer in command startup counts. The six command
   thresholds in section 5.3 are measured; they are not derived from the
   published command-control circuitry.
2. Combined non-zero R#18 and non-default R#9 S1/S0 timing is hardware-derived
   but has not been validated by a combined capture.
3. Text and MSX1 access tables. G1/G2/G3 CPU slot classes are circuit-derived
   (section 7.1) but not yet measured on the 8280. The text table's 47-slot
   cyclic structure is circuit-confirmed, but the circuit model retains a global
   one-`phiL` phase ambiguity and supplies no replacement table; CPU classes in
   text mode are unmeasured.
4. The border/display table switch at cycle 164 (section 10): measured and
   specified, but not yet emulated in openMSX. The `0x1FFFF` dummy address on
   packed slots is specified and implemented on
   [`sndpl/openMSX#9`](https://github.com/sndpl/openMSX/pull/9); only the
   command/address ownership path remains absent in the published silicon
   sheets.

For the default 1368-cycle centred bitmap modes, use the rules above without
additional per-mode or per-row exceptions.
