This is a writeup of the VDP-VRAM-timing investigation I did back in 2013 (or what I still remember from it).

There are 8 steps:

# Measurement Description

Description of the test/measurements we want to execute, see `vdp-analyze.txt`. Summary:
* setup screen mode
* screen enabled/disabled, sprites enabled/disabled
* exeucte a VDP command or not (which command)
* read or write VRAM via the CPU

We make sure all VRAM regions are distinct (non-overlapping), so that based on the VRAM address we can figure out what the purpose of the access was. The regions are:
* show (bitmap) display pixels
* render sprites (sprite pattern, sprite attributes)
* source and destination regions for VDP command
* read or write region for CPU access

In this directory you'll also find the source code of the test -program `testprog.asm` (assembled to `name.com`) and the BASIC program `test.bas` (or `test.asc`) to run the test.

The main idea is to run the test, and while it's executing capture the VDP pins via a logic analyzer (you'll find more detail in `VDP-analyze.txt` and `testprog.asm`). From these traces we can then figure out the order and timing of the VRAM access pattern.

# Raw Measurements

On 2013/01/19 in Nijmegen we performed the measurements. The result is a bunch of `.vcd` files.

Note this is raw measurement data. The filename describes the mesurement. E.g. `screen5sprites16x16HMMVcpuread.vcd` means: the test was done in screen5, with sprites enabled (16x16 sized), during the tests a HMMV command was executing, and we were read from VRAM via the CPU.

Occasionally there's a mistake in the filename, these mistakes only became apparent during later steps in the analysis. Or in some cases the filename has a suffix like `wrong_range`. In later steps these mistakes are either corrected, or the wrong measurement was dropped.

# Initial Analysis

During this step I developed tools to parse a `.vcd` file, and interpret the `RAS`, `CAS`, `R/W` and `A7..0` pins (the bus between VDP and VRAM). The result is output like:
```
    0:  W.v 0x00001
   22:
   26:  R.. 0x0d61c
   50:
   54:  R.. 0x0363f
   78:
   82:  R.v 0x07930
   92:  Rbv 0x07931
  100:  Rbv 0x07932
  110:  Rbv 0x07933
  120:  Rbv 0x07934
  128:  Rbv 0x07935
  138:  Rbv 0x07936
  148:  Rbv 0x07937
  170:
  174:  R.. 0x0d620
  198:
```
* 1st column is cycle number (absolute value is arbitrary, it's the moment we started the capture, but that can be in an arbitrary position in the frame).
* 2nd column has a 3-character code like `R.v`. First character `R` (read) or `W` (write). Second character is `.` (normal access) or `b` (a burst access). Third character is `.` or `v` (indicating the value of the `VDS` pin).
* 3rd column the (physical) address that read or written.

# Extract Reads and Writes

Similar to the previous step, but the output has a slightly different format (but very similar information content).

The previous step was mostly focussed on understanding the `.vcd` files. Now the focus has shifted to the logical meaning of the traces (which VRAM address are read or written at what time). The information is now stored in `.txt` files rather than `.vcd` files.

# Re-time per line

Our measurements could only capture short durations of about 250us, that's approximately 4 display lines in length. That's a partial first line, then 3 full lines and a partial ending line.

A very plausible hypothesis is that the access pattern repeats from line-to-line. We know a display lines takes 1368 cycles. So in this step we 'wrap' our access at 1368 cycles, so that access at the same relative offset within a line are now next to each other (so it becomes clear which accesses repeat every line).

The start of our measurement is arbitrary (likely not the beginning of a line). In this step we made an attempt to guess where the start of a line might be, and format our data like that (so a partial initial line, then usually 3 full lines and a partial last line). But see also next step.

# Synchronize measurements

The previous steps where all done on individual measurements. I think in this step I tried to cross-reference the measurements to get the cycle-offset within a line consistent across measurements (so all files choose a consistent 'start of line').

There also seems to be some overlap with the next step already. (The output files already contain some annotations that are only described in the next step).

# Annotate VRAM access

In this step we annotate the VRAM access to:
* bitmap data read
* sprite (pattern or attribute) data read
* command engine read or write
* CPU read or write

We mostly do this based on the VRAM address (see initial step). Or based on common sense we sometimes had to fix some mistakes (e.g. we did a measurement with screen enabled, but we happened to capture data in the vertical refresh (=access pattern as-if screen is disabled)).

The notation we use for an access is a 3-character code:
* 1st: `R` (read) or `W` (write)
* 2nd: `.` (normal) or `b` (burst), turns out only bitmap reads use burst access.
* 3rd: `v` (bitmap read), `s` (sprite read), `e` (command engine read or write), `c` (CPU read or write), `r` (refresh read), `p` (other, unknown, dummy read?).

In this step I started to write some conclusions:
* `slots.txt`: VRAM access slots for display rendering.
* `slots2.txt`: VRAM access slots for CPU and command engine access.

# command engine and cpu access slots

In this step we figure out the mechanism for:
* `slots3.txt`: how does the CPU or command engine get access slots.
* `slots4.txt`: What's the timing of the different VDP commands.
