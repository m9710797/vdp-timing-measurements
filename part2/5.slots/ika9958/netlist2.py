#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""
Gate-level netlist extraction from the IKA9958 KiCad schematics, with pin roles.

Pin names exist on the sequential cells (D, EN, CLK, RST, phi1, phi2, Q, ~Q) but
not on the combinational ones, where inputs are ordered top to bottom so that the
AOI/OAI cells keep their AND/OR grouping. Some library cells declare their Q pins
as inputs (DLPEN), so side is used rather than the declared electrical type.
"""
import re
import math
from collections import defaultdict


def parse(text):
    toks = re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+', text)
    pos = 0

    def rd():
        nonlocal pos
        t = toks[pos]
        pos += 1
        if t == '(':
            out = []
            while toks[pos] != ')':
                out.append(rd())
            pos += 1
            return out
        if t.startswith('"'):
            return t[1:-1].encode().decode('unicode_escape')
        return t
    out = []
    while pos < len(toks):
        out.append(rd())
    return out[0]


def find(n, tag):
    return [c for c in n if isinstance(c, list) and c and c[0] == tag]


def first(n, tag):
    f = find(n, tag)
    return f[0] if f else None


def at_of(n):
    a = first(n, 'at')
    return (float(a[1]), float(a[2]), float(a[3]) if len(a) > 3 else 0.0)


def key(x, y):
    return (round(x, 3), round(y, 3))


class Union:
    def __init__(self):
        self.p = {}

    def find(self, x):
        self.p.setdefault(x, x)
        while self.p[x] != x:
            self.p[x] = self.p[self.p[x]]
            x = self.p[x]
        return x

    def join(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.p[ra] = rb


def norm(nm):
    """Normalise a pin name: strip KiCad overbar markup and greek phi."""
    nm = nm.replace('~{', '').replace('}', '')
    nm = nm.replace('\u03a6', 'PHI').replace('\u00ce\u00a6', 'PHI')
    return nm.strip()


def load(path):
    root = parse(open(path, encoding='utf8', errors='replace').read())

    libpins = {}
    for sym in find(first(root, 'lib_symbols'), 'symbol'):
        name = sym[1].split(':')[-1]
        pins = []
        for unit in find(sym, 'symbol'):
            for p in find(unit, 'pin'):
                x, y, r = at_of(p)
                nm = first(p, 'name')
                pins.append({'etype': p[1], 'style': p[2], 'x': x, 'y': y,
                             'name': norm(nm[1]) if nm else ''})
        # side decides direction: right-hand pins are outputs
        for p in pins:
            p['is_out'] = p['x'] > 0
        libpins[name] = pins

    u = Union()
    for tag in ('wire', 'bus'):
        for w in find(root, tag):
            xs = [(float(c[1]), float(c[2]))
                  for c in find(first(w, 'pts'), 'xy')]
            for a, b in zip(xs, xs[1:]):
                u.join(key(*a), key(*b))

    names = defaultdict(set)
    for tag in ('label', 'global_label', 'hierarchical_label'):
        for l in find(root, tag):
            x, y, r = at_of(l)
            names[key(x, y)].add(l[1])

    cells = []
    for sym in find(root, 'symbol'):
        lid = first(sym, 'lib_id')
        if not lid:
            continue
        lib = lid[1].split(':')[-1]
        if lib not in libpins:
            continue
        x0, y0, rot = at_of(sym)
        mir = first(sym, 'mirror')
        mirror = mir[1] if mir else None
        ref = None
        for p in find(sym, 'property'):
            if p[1] == 'Reference':
                ref = p[2]
        a = math.radians(rot)
        pins = []
        for lp in libpins[lib]:
            cx, cy = lp['x'], -lp['y']
            # KiCad's mirror is in sheet coordinates, after symbol rotation.
            # Applying it here in local coordinates misplaces pins whenever a
            # symbol is both rotated and mirrored (notably MI U633 at /RAS).
            rx = cx * math.cos(a) + cy * math.sin(a)
            ry = -cx * math.sin(a) + cy * math.cos(a)
            if mirror == 'y':
                rx = -rx
            elif mirror == 'x':
                ry = -ry
            pins.append({**lp, 'k': key(x0 + rx, y0 + ry)})
        cells.append({'lib': lib, 'ref': ref, 'pins': pins,
                      'rot': rot, 'mirror': mirror})

    net_names = defaultdict(set)
    for k, ns in names.items():
        net_names[u.find(k)] |= ns

    def netof(k):
        r = u.find(k)
        ns = net_names.get(r)
        return sorted(ns, key=len)[0] if ns else f"~{r[0]}_{r[1]}"

    for c in cells:
        # inputs ordered top to bottom in library coordinates
        ins = sorted([p for p in c['pins'] if not p['is_out']],
                     key=lambda p: -p['y'])
        outs = [p for p in c['pins'] if p['is_out']]
        c['i'] = [(p['name'], netof(p['k'])) for p in ins]
        c['o'] = [(p['name'] or ('~Q' if p['style'] == 'inverted' else 'Q'),
                   netof(p['k'])) for p in outs]
    return cells, netof


if __name__ == '__main__':
    import sys
    cells, netof = load(sys.argv[1])
    drv = defaultdict(list)
    ld = defaultdict(list)
    for c in cells:
        for _, n in c['o']:
            drv[n].append(c)
        for _, n in c['i']:
            ld[n].append(c)
    print(f"cells {len(cells)}")
    print(f"multi-driven: {[n for n,d in drv.items() if len(d)>1]}")
    und = sorted({n for c in cells for _, n in c['i'] if n not in drv})
    print(f"undriven inputs ({len(und)}):")
    for n in und:
        print("   ", n, "->", [c['lib'] for c in ld[n]])
