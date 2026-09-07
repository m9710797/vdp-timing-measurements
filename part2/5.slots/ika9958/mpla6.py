#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""
Two-counter model of the slot lattice.

IKA9958_st.sv makes `hcntr` the master horizontal counter: it counts 0..340 on
the phiL falling edge (341 ticks) and is reset by `hpla[0]` = (hcntr == 339)
delayed one tick.  `hadd = hcntr + H` (H = R#18[3:0], signed) and
`ST.hadd_eq23 = (hadd == 23)`, and *that* is what reloads `bpc` to 0x1E3 in
IKA9958_pla.sv.  So the line period comes from `hcntr`, not from `bpc`, and the
phase between the two counters is fixed by the reload rather than free.

`bpc` itself is built the way BPLA.kicad_sch draws it: a plain 9-bit ripple
counter, not the Verilog's sr4-delayed carry.

341 ticks is 1364 cycles.  The measured line is 1368, so one tick per line must
be longer than the other 340.  `stretch` is the index of that tick; the extra 4
cycles are appended to it.
"""
from mpla3 import srf, LINE


class Hcntr:
    """IKA9958_st.sv, verbatim."""

    def __init__(self, h=0):
        self.h = h
        self.hcntr = 0
        self.hpla0_z = 0
        self.hadd = 0
        self.eq23 = 0

    def step(self):
        """Advance one phiL tick; return this tick's (hcntr, hadd_eq23)."""
        hcntr, eq23 = self.hcntr, self.eq23
        rst = self.hpla0_z
        hpla0 = 1 if (hcntr == 339 and not rst) else 0
        self.eq23 = 1 if self.hadd == 23 else 0
        self.hadd = (hcntr + self.h) & 0x1FF
        self.hcntr = 0 if rst else (hcntr + 1) & 0x1FF
        self.hpla0_z = hpla0
        return hcntr, eq23


class Pla6:
    """One phiL tick per step(). Same decode as mpla3, different state."""

    def __init__(self, mode, h=0):
        self.mode = mode
        self.hc = Hcntr(h)
        self.bpc = 0
        self.z = 0
        self.bpla_z = {}
        self.gt042 = self.gt072 = 0
        self.he_z = self.he_long_z = 0
        self.gt055 = self.gt056 = self.gt057 = self.gt058 = 0
        self.gt059 = self.gt061 = self.gt062 = self.gt063 = self.gt064 = 0
        self.av_w_spr = 1 if mode == 'sprOn' else 0
        self.av_wo_spr = 1 if mode == 'sprOff' else 0
        self.av_common = self.av_w_spr or self.av_wo_spr

    bpla_of = None  # bound below

    def step(self):
        bpc, z = self.bpc, self.z
        self.cur = (self.hc.hcntr, bpc)
        bpla = self.bpla_of(bpc)
        bz = self.bpla_z
        c = z & 7
        z3 = (z >> 3) & 1

        ac_2         = c != 2
        ac_0_3       = c not in (0, 3)
        ac_lo7       = (c != 7) and not z3
        ac_hi_3_7    = (c not in (3, 7)) and z3
        ac_lo_not0   = (c != 0) and not z3
        ac_hi_not0_4 = (c not in (0, 4)) and z3
        ac_456       = c in (4, 5, 6)
        ac_even      = not (z & 1)

        A, W, O = self.av_common, self.av_w_spr, self.av_wo_spr
        g55, g56, g57, g58 = self.gt055, self.gt056, self.gt057, self.gt058

        m = {}
        m[3]  = ac_2         and A and not g58 and not bz.get(32, 0)
        m[4]  = ac_lo7       and W and not g55
        m[5]  = ac_hi_3_7    and W and not g55
        m[6]  = ac_even      and A and not g57
        m[7]  = ac_even      and O and not g55
        m[-1] = ac_even      and O and not g55 and not bz.get(30, 0)
        m[8]  = ac_even      and not A and not g56
        m[9]  = m[10] = 0
        m[11] = W and bz.get(29, 0)
        m[12] = ac_0_3       and A and not g58 and not bz.get(32, 0)
        m[13] = ac_lo_not0   and W and not g55
        m[14] = ac_hi_not0_4 and W and not g55
        m[24] = ac_456       and W and not g55

        gt075 = (not ((bpla[41] or bpla[42]) and not bpla[43])
                 and not bpla[47] and not bpla[48] and not bpla[49])
        gt076 = ((not self.gt072) and not bpla[47] and not bpla[48]
                 and not bpla[49])
        gt077 = ((self.gt059 or not bpla[44]) and ((bpc & 1) or self.gt063)
                 and not self.gt072 and not bpla[48])
        gt078 = (((bpc & 1) or self.gt063)
                 and ((bpc & 1) or self.gt064 or bz.get(43, 0)))
        if self.mode == 'g123':
            # MPLA 15..22 and CPU output 26. G1/G2/G3 deliberately share
            # these terms; G3's sprite-side output does not alter CPU slots.
            ac_15 = c not in (1, 4, 7)
            ac_17 = (c != 7) and z3
            ac_20 = c not in (2, 5, 7)
            m[15] = ac_15 and not g58
            m[16] = ac_lo7 and not g55 and not bz.get(30, 0)
            m[17] = ac_17 and not g55
            m[18] = ac_even and not g57
            m[19] = bool(bz.get(29, 0))
            m[20] = ac_20 and not g58
            m[21] = ac_lo_not0 and not g55
            m[22] = ac_hi_not0_4 and not g55
            cpu_t = not gt075
        elif self.mode == 'sprOn':
            cpu_t = W and not gt076
        elif self.mode == 'sprOff':
            cpu_t = O and not gt077
        else:
            cpu_t = (not A) and not gt078

        if self.mode == 'g123':
            ras1 = int(any(m[i] for i in (15, 16, 17, 18, 19)))
            ras0 = int(any(m[i] for i in (18, 20, 21, 22)))
        else:
            ras1 = int(any(m[i] for i in (3, 4, 5, 6, 7, 8, 9, 10, 11)))
            ras0 = int(bool(m[14] or m[13] or m[12] or m[8] or m[6] or m[-1]))
        cpu = int(bool(cpu_t and not (bpla[84] or bpla[85])))
        spr = int(bool(m[24]))
        out = (ras1, ras0, cpu, spr)

        # --- next state: bpc as a plain ripple counter -------------------
        _, he = self.hc.step()
        he_long = he or self.he_z
        gt033 = 1 if (not self.gt042 and not self.he_long_z) else 0
        if self.he_z:
            nbpc = 0x1E3
        elif gt033:
            nbpc = 0x1D1
        elif bz.get(2, 0):
            nbpc = 0x000
        else:
            nbpc = (bpc + 1) & 0x1FF

        self.gt055 = srf(g55, bpla[29], bpla[30])
        self.gt056 = srf(g56, bpla[29], bpla[31])
        self.gt057 = srf(g57, bpla[32], bpla[31])
        self.gt058 = srf(g58, bpla[30], bpla[32])
        nb = self.bpla_of(nbpc)
        self.gt059 = srf(self.gt059, nb[30], nb[32])
        self.gt061 = srf(self.gt061, nb[34], nb[33])
        self.gt062 = srf(self.gt062, nb[36], nb[35])
        self.gt063 = srf(self.gt063, nb[38], nb[37])
        self.gt064 = srf(self.gt064, nb[40], nb[39])

        self.bpla_z = bpla
        self.z = bpc & 0xF
        self.bpc = nbpc
        self.gt042 = 0 if bpla[6] else 1
        self.gt072 = 1 if ((bpla[41] or bpla[42]) and not bpla[43]) else 0
        self.he_z, self.he_long_z = he, he_long
        return out


from mpla3 import Pla as _P3
Pla6.bpla_of = _P3.bpla_of


def trace(mode, h=0, warm=4000, span=1400):
    """One line of ticks, aligned so tick 0 is hcntr == 0."""
    p = Pla6(mode, h)
    hist = []
    for _ in range(warm + span):
        o = p.step()
        hist.append((p.cur[0], o))
    idx = [i for i in range(warm, len(hist)) if hist[i][0] == 0]
    s, e = idx[0], idx[1]
    return [hist[i][1] for i in range(s, e)]


if __name__ == '__main__':
    for mode in ('dispOff', 'g123', 'sprOff', 'sprOn'):
        t = trace(mode)
        print(f"{mode:8s} ticks {len(t):4d}  cpu {sum(x[2] for x in t):4d}"
              f"  spr {sum(x[3] for x in t):3d}")
