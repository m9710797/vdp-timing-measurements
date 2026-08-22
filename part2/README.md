13(!) years after the original measurements I did some follow-up measurements.
The trigger was the unexpected behavior of the LMMM command, but only in
sprites-ON mode. See openMSX commit 7dde62b3f07ddc5.

The original measurements mostly focussed on the "display off" mode (rationale:
the goal was to investigate the VDP commands, and in this mode the commands
execute fastest, thus more command VRAM accesses in the measurement set).

In the new measurements I do equal amount of measurements in the 3 modes:
* display off               (later shortened to "dispOff")
* display On, sprites Off   (shortened to "sprOff")
* display On, sprites On    (shortened to "sprOn")

All measurements are stored in files with the following filename convention:
   scr5-[mode]-[command]-[cpu]-[n][suffix].{vcd,txt}
* All measurements are for screen 5 (from the old analysis we learned that all
  bitmap modes anyway behave the same).
* [mode] is one of: dispOff, sprOff, sprOn
* [command] is one of: ymmm, hmmm, hmmv, lmmm, lmmv, line
* [cpu] is one of: noCPU (no CPU access during the command) or rdCpu/wrCpu
* [n] is just a number 1-9, I often repeated the same measurement multiple times
* [suffix] is either nothing "", b or c. I did 3 sets of measurements, more on that below.

The measurements / analysis is spread over these subdirectories:
* 1.vcd: the raw measurements
* 2.rw: decode the A0..A7, RAS, CAS1, CAS2, R/W signals into VRAM read/write actions
* 3.time: convert the timescale to VDP clock cycles. Align to known timings from a VDP display line.
          This step uses knowledge from the old analysis .. we know how to recognize "refresh"
          accesses and use those to convert and align the timing.
* 4.manual-fix-timing: This is a manual inspection step. In some cases the cycles were off-by-one,
                       mostly only in places where we had the extrapolate (rather than interpolate),
                       so at the start/stop boundaries. Sometimes I manually fixed the cycles counts
                       (add or subtract one). In other cases I threw away some measurements at the
                       start/stop. In a few cases the measurement didn't seem to match the description
                       (in the filename), then I threw away the whole measurement.
                       So now I'm fairly confident the measurements are high quality.
* 5.slots: This takes the data from the previous step and only retains the CPU/command access slots.

In this last set of files (step 5.slots) we can see the timing of the VDP commands.
E.g. a HMMV command has at least 48 VDP cycles between two accesses. We can also see that when a block
moves from one line to the next it takes some extra cycles.

But at this point I noticed the measurements only covered a handful of such
"move to next line in the block" events. Especially for the "LMMM" and "LMMV"
commands. So I made a new set of measurements. These have "b" as the suffix, I
did more repetitions for the "LMMM", "LMMV" and "LINE" commands.
I also reduced the width of block commands from 256 to 128 pixels (so 128 accesses
for the YMMM, and Hxxx commands, 256 for Lxxx).

I found that I still did not have sufficient "move to next line" measurements.
So I made a 3rd set, now with "c" suffix in the filename. For these I lowered the
width of the block to only 4 pixels. But because now the commands finish very quickly
it was hard to measure them, so I compensated by increasing the height of the block to
512 pixels. That works, but the disadvantage is that now the source / destination is
no longer disjoint. So we can no longer tell from the VRAM address if it's
inside the source or the destination block. Though by now we understand the VDP commands
already well enough that this isn't a big problem.

Next while analyzing the results, I found that the commands, more specifically
how the VRAM accesses are distributed over the available access slots, quickly
settle into fixed "orbits". That is: even if there are +80 access slots
available per line, the VDP can only use about 20 of them (that's fully
expected), but then in the next line it uses the exact same 20 slots. To get
some more variation in which slots are used (and then hopefully also get more
variation in the distance between the slots, to get tighter bounds on the
command execution timings) I now varied the block with a lot more. The filenames
for this new set of measurements ends with `*nx??-?d.*`.







Preliminary analysis:

IMPORTANT CORRECTION:
After the old set of measurements we located the access slots:
* For dispOff: ... 1317 **1325** **1335** 1345 ...
* For sprOff:  ... 1315 **1323** **1333** 1343 ...
* For sprOn:   ... 1330 ...

In these new sets of measurements we instead get
* For dispOff: ... 1317 **1326** **1336** 1345 ...    (2 slots shifted by 1)
* For sprOff:  ... 1315 **1324** **1334** 1343 ...    (2 slots shifted by 1)
* For sprOn:   ... 1330 ...                           (unchanged)

If I go back to the old measurements, around the position of these shifted
slots, the timing is a bit ambiguous. The old data is also a good fit for the
corrected slots. So when using the old measurement data, we need to interpret
that as-if accesses on slots 1323, 1325, 1333, 1335 instead happened at 1324,
1326, 1334, 1336.



My best fit for the command execution timings, using the union of all
measurement data, and using some heuristics (when there are multiple equivalent
solutions) is:
    HMMV                  : W 46 (+58)
    LMMV                  : R 24 W 72 (+58)
    YMMM                  : R 24 W 38 (+66)
    HMMM                  : R 24 W 62 (+66)
    LMMM (dispOff, sprOff): R 32 R 24 W 62 (+66)
    LMMM (sprOn)          : R 34 R 24 W 64 (+66)
