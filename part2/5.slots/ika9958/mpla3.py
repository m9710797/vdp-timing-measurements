#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""
Memory PLA on the 2-cycle grid, for graphics modes G4..G7.

The Memory PLA emits two RAS requests per phiL tick (`vram_ras_rq[1:0]`, the
comment in the source calls them the first and the second request), so the VRAM
access grid is 2 VDP cycles and each phiL tick holds two sub-slots. `phiL_PCEN`
fires on the phiL rising edge and `phiL_NCEN` on the falling edge, 2 cycles
apart, which is the same 2-cycle grid.

Everything here is a transcription of IKA9958_pla.sv. The conditions named
`ac_*` are read off `PLA.Bpc.z_m8c`, a one-hot of the *registered* bpc[2:0], so
they lag `bpc` itself by one tick; that lag is modelled explicitly.
"""

LINE = 1368


def srf(q, s, r):
    if s and r:
        return 0
    return 1 if s else (0 if r else q)


class Pla:
    """One phiL tick per call to step(). Returns the tick's four outputs."""

    def __init__(self, mode, hfire=0x1DF):
        self.mode = mode
        self.hfire = hfire
        self.bpc = 0
        self.z = 0                  # PLA.Bpc.z, registered bpc[3:0]
        self.bpla_z = {}
        self.gt042 = self.gt044 = self.hi_ci = self.gt072 = 0
        self.sr4 = [0, 0, 0, 0]
        self.he_z = self.he_long_z = 0
        # NCEN latches
        self.gt055 = self.gt056 = self.gt057 = self.gt058 = 0
        # PCEN latches
        self.gt059 = self.gt061 = self.gt062 = self.gt063 = self.gt064 = 0

        self.av_w_spr = 1 if mode == 'sprOn' else 0
        self.av_wo_spr = 1 if mode == 'sprOff' else 0
        self.av_common = self.av_w_spr or self.av_wo_spr

    def bpla_of(self, bpc):
        return {
            0: (bpc & 0xF) == 9,
            2: bpc == 0x1F6, 3: bpc == 0x1E6, 5: bpc == 0x1D3,
            6: bpc == 0x12F, 8: bpc == 0x123, 10: bpc == 0x113,
            12: bpc == 0x103,
            29: bpc == 0x1E3, 30: bpc == 0x100, 31: bpc == 0x1EB,
            32: bpc == 0x1F0, 33: bpc == 0x140, 34: bpc == 0x1D1,
            35: bpc == 0x1EB, 36: bpc == 0x000, 37: bpc == 0x108,
            38: bpc == 0x1DF, 39: bpc == 0x1EC, 40: bpc == 0x0FF,
            41: bpc == 0x1F1,
            42: (bpc & 0x107) == 0x001, 43: (bpc & 0x11F) == 0x011,
            44: (bpc & 7) == 1,
            47: (bpc & 0x1CF) == 0x107, 48: bpc in (0x1EC, 0x1EE),
            49: bpc == 0x1D7, 84: bpc == 0x1FC, 85: bpc == 0x1FE,
        }

    def step(self):
        bpc, z = self.bpc, self.z
        self.cur_bpc = bpc
        bpla = self.bpla_of(bpc)
        bz = self.bpla_z
        c = z & 7                      # the one-hot position of z_m8c
        z3 = (z >> 3) & 1

        # --- ac_* conditions, from PLA.Bpc.z (registered) ---------------
        ac_2         = c != 2                       # 1111_1011...
        ac_0_3       = c not in (0, 3)              # 1111_0110...
        ac_1_4_7     = c not in (1, 4, 7)           # 0110_1101...
        ac_2_5_7     = c not in (2, 5, 7)           # 0101_1011...
        ac_lo7       = (c != 7) and not z3          # 0000_0000_0111_1111
        ac_hi_3_7    = (c not in (3, 7)) and z3     # 0111_0111_0000_0000
        ac_lo_not0   = (c != 0) and not z3          # 0000_0000_1111_1110
        ac_hi_not0_4 = (c not in (0, 4)) and z3     # 1110_1110_0000_0000
        ac_456       = c in (4, 5, 6)               # 0111_0000_0111_0000
        ac_even      = not (z & 1)                  # 0101_0101...
        ac_odd       = bool(z & 1)                  # 1010_1010...

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
        m[9]  = 0
        m[10] = 0
        m[11] = W and bz.get(29, 0)
        m[12] = ac_0_3       and A and not g58 and not bz.get(32, 0)
        m[13] = ac_lo_not0   and W and not g55
        m[14] = ac_hi_not0_4 and W and not g55
        m[23] = 0                                   # G6/G7 only
        m[24] = ac_456       and W and not g55

        # --- the three CPU-slot terms -----------------------------------
        gt076 = (not self.gt072) and not bpla[47] and not bpla[48] \
            and not bpla[49]
        gt077 = ((self.gt059 or not bpla[44]) and ((bpc & 1) or self.gt063)
                 and not self.gt072 and not bpla[48])
        gt078 = ((bpc & 1) or self.gt063) and \
            ((bpc & 1) or self.gt064 or bz.get(43, 0))
        if self.mode == 'sprOn':
            m[27], m[28], m[29] = (W and not gt076), 0, 0
        elif self.mode == 'sprOff':
            m[27], m[28], m[29] = 0, (O and not gt077), 0
        else:
            m[27], m[28], m[29] = 0, 0, ((not A) and not gt078)

        ras1 = int(any(m[i] for i in range(3, 12)))
        ras0 = int(bool(m[14] or m[13] or m[12] or m[9] or m[8] or m[6]
                        or m[-1]))
        # mpla[25] (text) and mpla[26] (G1..G3) are zero in G4..G7
        cpu = int(bool((m[29] or m[28] or m[27])
                       and not (bpla[84] or bpla[85])))
        spr = int(bool(m[24]))
        out = (ras1, ras0, cpu, spr)

        # --- next state --------------------------------------------------
        he = 1 if bpc == self.hfire else 0
        he_long = he or self.he_z
        gt033 = 1 if (not self.gt042 and not self.he_long_z) else 0
        ld = gt033 or bz.get(2, 0) or self.he_z
        if ld:
            nlo = (self.he_z << 1) | (self.he_z or gt033)
            nhi = ((1 if (self.he_z or gt033) else 0) * 0b111 << 2) \
                | (self.he_z << 1) | gt033
        else:
            nlo = (bpc + 1) & 0xF
            nhi = ((bpc >> 4) + self.hi_ci) & 0x1F
        nbpc = ((nhi & 0x1F) << 4) | (nlo & 0xF)

        n42 = 0 if bpla[6] else 1
        n4 = [0, 0, 0, 0]
        n4[0] = 1 if (bz.get(0, 0) and not self.he_long_z) else 0
        n4[1] = 0 if (self.he_z or self.gt044) else self.sr4[0]
        n4[2] = 0 if self.he_z else self.sr4[1]
        n4[3] = 0 if self.he_z else self.sr4[2]
        n_hi_ci = 0 if self.he_z else self.sr4[3]
        n72 = 1 if ((bpla[41] or bpla[42]) and not bpla[43]) else 0

        # NCEN latches see the pre-advance bpc, like every other NCEN register
        self.gt055 = srf(g55, bpla[29], bpla[30])
        self.gt056 = srf(g56, bpla[29], bpla[31])
        self.gt057 = srf(g57, bpla[32], bpla[31])
        self.gt058 = srf(g58, bpla[30], bpla[32])
        # PCEN latches fire on the next rising edge, so they see the new bpc
        nb = self.bpla_of(nbpc)
        self.gt059 = srf(self.gt059, nb[30], nb[32])
        self.gt061 = srf(self.gt061, nb[34], nb[33])
        self.gt062 = srf(self.gt062, nb[36], nb[35])
        self.gt063 = srf(self.gt063, nb[38], nb[37])
        self.gt064 = srf(self.gt064, nb[40], nb[39])

        self.bpla_z = bpla
        self.z = bpc & 0xF
        self.bpc, self.gt042, self.sr4 = nbpc, n42, n4
        self.hi_ci, self.gt072 = n_hi_ci, n72
        self.he_z, self.he_long_z = he, he_long
        return out


def trace(mode, hfire=0x1DF, warm=2000, span=1200):
    p = Pla(mode, hfire)
    hist = []
    for i in range(warm + span):
        o = p.step()
        hist.append((p.cur_bpc, o))
    # one period: between successive returns of bpc to 0
    idx = [i for i in range(warm, len(hist)) if hist[i][0] == 0]
    if len(idx) < 2:
        return None
    s, e = idx[0], idx[1]
    return [hist[i][1] for i in range(s, e)]
