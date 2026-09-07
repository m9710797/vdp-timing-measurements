#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""
Gate-level simulator for the IKA9958 CI arbiter cone.

One time step is one VDP cycle. phiL has a period of 4 cycles. Combinational
logic (including the SR latches, which are real combinational loops here) is
settled to a fixed point each step; level-sensitive latches take part in that
settling while they are transparent, so a transparent latch behaves as a wire.

Every cell's non-inverting function is computed once as `core`, and each output
net then takes `core` or `~core` according to the pin's declared style. That is
what makes Q and ~Q come out right without special-casing each cell.
"""
import sys
from collections import defaultdict
from netlist2 import load

# --- cell functions, all expressed as the non-inverting "core" ---------------


def core_of(lib, i):
    if lib in ('INV', 'INVP', 'INVOD', 'INVD', 'BUF', 'BUFC', 'BUFD', 'BUFP',
               'BUFCP1'):
        return i[0]
    if lib in ('NAND2', 'NAND2P', 'NAND2OD', 'NAND3', 'AND2', 'AND3'):
        return int(all(i))
    if lib in ('NOR2', 'NOR2P', 'NOR3', 'NOR4', 'NOR5', 'NOR6', 'OR2', 'OR3'):
        return int(any(i))
    if lib == 'AOI21':
        return int((i[0] and i[1]) or i[2])
    if lib == 'AOI211':
        return int((i[0] and i[1]) or i[2] or i[3])
    if lib == 'AOI22':
        return int((i[0] and i[1]) or (i[2] and i[3]))
    if lib == 'AOI221':
        return int((i[0] and i[1]) or (i[2] and i[3]) or i[4])
    if lib == 'OAI31':
        return int((i[0] or i[1] or i[2]) and i[3])
    raise KeyError(lib)


COMB = ('INV', 'INVP', 'INVOD', 'INVD', 'BUF', 'BUFC', 'BUFD', 'BUFP',
        'BUFCP1', 'NAND2', 'NAND2P', 'NAND2OD', 'NAND3', 'AND2', 'AND3',
        'NOR2', 'NOR2P', 'NOR3', 'NOR4', 'NOR5', 'NOR6', 'OR2', 'OR3',
        'AOI21', 'AOI211', 'AOI22', 'AOI221', 'OAI31')
SEQ = ('DLPEN', 'DLPRPEN', 'VY_DDL_small', 'VY_DSR_small', 'DFFNR')


class Sim:
    def __init__(self, path, roots, stop):
        cells, netof = load(path)
        drv = defaultdict(list)
        for c in cells:
            for p in c['pins']:
                if p['is_out']:
                    drv[netof(p['k'])].append(c)
        # cone
        from collections import deque
        cone, seen, netseen = [], set(), set(roots)
        q = deque(roots)
        while q:
            n = q.popleft()
            for c in drv.get(n, []):
                if id(c) in seen:
                    continue
                seen.add(id(c))
                cone.append(c)
                for p in c['pins']:
                    if p['is_out']:
                        continue
                    m = netof(p['k'])
                    if m in netseen or m.startswith(stop):
                        continue
                    netseen.add(m)
                    q.append(m)
        self.cells = []
        for c in cone:
            ins = sorted([p for p in c['pins'] if not p['is_out']],
                         key=lambda p: -p['y'])
            outs = [p for p in c['pins'] if p['is_out']]
            self.cells.append({
                'ref': c['ref'], 'lib': c['lib'],
                'in': [(p['name'], netof(p['k'])) for p in ins],
                'out': [(p['style'], netof(p['k'])) for p in outs],
            })
        self.net = defaultdict(int)
        self.state = {}          # ref -> latch/flop state
        self.prev_clk = {}
        self.inputs = {}

    def named(self, name):
        """role -> net value, for the named-pin cells"""
        pass

    def step(self, drive):
        """drive: dict of boundary net -> value. Settles, then clocks."""
        self.net.update(drive)
        for it in range(400):
            changed = False
            for c in self.cells:
                lib = c['lib']
                iv = [self.net[n] for _, n in c['in']]
                names = {nm: self.net[n] for nm, n in c['in'] if nm}
                if lib in COMB:
                    core = core_of(lib, iv)
                elif lib in ('VY_DDL_small', 'DLPEN'):
                    en = names.get('PHI1', names.get('EN'))
                    st = self.state.get(c['ref'], 0)
                    if en:
                        st = names['D']
                    self.state[c['ref']] = st
                    core = st
                elif lib == 'DLPRPEN':
                    st = self.state.get(c['ref'], 0)
                    if names.get('RST'):
                        st = 0
                    elif names.get('EN'):
                        st = names['D']
                    self.state[c['ref']] = st
                    core = st
                elif lib == 'VY_DSR_small':
                    m, s = self.state.get(c['ref'], (0, 0))
                    if names.get('PHI1'):
                        m = names['D']
                    if names.get('PHI2'):
                        s = m
                    self.state[c['ref']] = (m, s)
                    core = s
                elif lib == 'DFFNR':
                    core = self.state.get(c['ref'], 0)
                else:
                    raise KeyError(lib)
                for style, n in c['out']:
                    v = (1 - core) if style == 'inverted' else core
                    if self.net[n] != v:
                        self.net[n] = v
                        changed = True
            if not changed:
                break
        else:
            raise RuntimeError("combinational logic did not settle")

        # edge-triggered cells, after the level logic has settled
        for c in self.cells:
            if c['lib'] != 'DFFNR':
                continue
            names = {nm: self.net[n] for nm, n in c['in'] if nm}
            clk = names['CLK']
            prev = self.prev_clk.get(c['ref'], clk)
            st = self.state.get(c['ref'], 0)
            if prev and not clk:                 # negative edge
                st = names['D']
            if not names.get('RST', 1):          # ~RST active low
                st = 0
            self.state[c['ref']] = st
            self.prev_clk[c['ref']] = clk
