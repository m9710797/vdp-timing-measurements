#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""
Memory PLA with the three bpc reloads' delays made independent.

The horizontal-blanking run and the active-display run are phased by different
reloads, so their relative phase is set by the difference of those reloads'
delays. The Verilog gives all three one register stage; this lets each be 0 or 1
tick so the relative phase can be tested.

  d2  delay on the bpla[2]  (bpc == 0x1F6) reload to 0x000
  d6  delay on the bpla[6]  (bpc == 0x12F) reload to 0x1D1
  dh  delay on the hadd_eq23        reload to 0x1E3
"""
from mpla3 import srf

LINE = 1368


class Pla4:
    def __init__(self, mode, hfire=0x1DF, d2=1, d6=1, dh=1):
        self.mode, self.hfire = mode, hfire
        self.d2, self.d6, self.dh = d2, d6, dh
        self.bpc = 0
        self.z = 0
        self.bpla_z = {}
        self.p2 = [0, 0]        # pipelines for the three load triggers
        self.p6 = [0, 0]
        self.ph = [0, 0]
        self.sr4 = [0, 0, 0, 0]
        self.hi_ci = 0
        self.gt072 = 0
        self.gt055 = self.gt056 = self.gt057 = self.gt058 = 0
        self.gt059 = self.gt061 = self.gt062 = self.gt063 = self.gt064 = 0
        self.av_w_spr = 1 if mode == 'sprOn' else 0
        self.av_wo_spr = 1 if mode == 'sprOff' else 0
        self.av_common = self.av_w_spr or self.av_wo_spr

    from mpla3 import Pla as _P
    bpla_of = _P.bpla_of

    def step(self):
        bpc, z = self.bpc, self.z
        self.cur_bpc = bpc
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
        m[11] = W and bz.get(29, 0)
        m[12] = ac_0_3       and A and not g58 and not bz.get(32, 0)
        m[13] = ac_lo_not0   and W and not g55
        m[14] = ac_hi_not0_4 and W and not g55
        m[24] = ac_456       and W and not g55

        gt076 = (not self.gt072) and not bpla[47] and not bpla[48] \
            and not bpla[49]
        gt077 = ((self.gt059 or not bpla[44]) and ((bpc & 1) or self.gt063)
                 and not self.gt072 and not bpla[48])
        gt078 = ((bpc & 1) or self.gt063) and \
            ((bpc & 1) or self.gt064 or bz.get(43, 0))
        if self.mode == 'sprOn':
            cpu_t = W and not gt076
        elif self.mode == 'sprOff':
            cpu_t = O and not gt077
        else:
            cpu_t = (not A) and not gt078

        ras1 = int(any(m.get(i) for i in range(3, 12)))
        ras0 = int(bool(m[14] or m[13] or m[12] or m[8] or m[6] or m[-1]))
        cpu = int(bool(cpu_t and not (bpla[84] or bpla[85])))
        spr = int(bool(m[24]))
        out = (ras1, ras0, cpu, spr)

        # --- next state, with the three reloads independently delayed -----
        t2 = 1 if bpla[2] else 0
        t6 = 1 if bpla[6] else 0
        th = 1 if bpc == self.hfire else 0
        ld2 = t2 if self.d2 == 0 else self.p2[self.d2 - 1]
        ld6 = t6 if self.d6 == 0 else self.p6[self.d6 - 1]
        ldh = th if self.dh == 0 else self.ph[self.dh - 1]

        if ldh:
            nbpc = 0x1E3
        elif ld2:
            nbpc = 0x000
        elif ld6:
            nbpc = 0x1D1
        else:
            nbpc = (((bpc >> 4) + self.hi_ci) & 0x1F) << 4 | ((bpc + 1) & 0xF)

        reload_any = ldh or ld2 or ld6
        n4 = [0, 0, 0, 0]
        n4[0] = 1 if (bz.get(0, 0) and not reload_any) else 0
        n4[1] = 0 if reload_any else self.sr4[0]
        n4[2] = 0 if reload_any else self.sr4[1]
        n4[3] = 0 if reload_any else self.sr4[2]
        n_hi_ci = 0 if reload_any else self.sr4[3]
        n72 = 1 if ((bpla[41] or bpla[42]) and not bpla[43]) else 0

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

        self.p2 = [t2, self.p2[0]]
        self.p6 = [t6, self.p6[0]]
        self.ph = [th, self.ph[0]]
        self.bpla_z = bpla
        self.z = bpc & 0xF
        self.bpc, self.sr4 = nbpc, n4
        self.hi_ci, self.gt072 = n_hi_ci, n72
        return out


def trace(mode, hfire, d2, d6, dh, warm=2200, span=1400):
    p = Pla4(mode, hfire, d2, d6, dh)
    hist = []
    for i in range(warm + span):
        o = p.step()
        hist.append((p.cur_bpc, o))
    idx = [i for i in range(warm, len(hist)) if hist[i][0] == 0]
    if len(idx) < 2:
        return None
    s, e = idx[0], idx[1]
    return [hist[i][1] for i in range(s, e)]
