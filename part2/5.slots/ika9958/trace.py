#!/usr/bin/env python3
# This file was written with assistance from an AI coding agent.
"""Print the driving logic of a net, to a given depth."""
import sys
from collections import defaultdict
import netlist2 as N


def load(path):
    cells, netof = N.load(path)
    drv = {}
    for c in cells:
        for p in c['pins']:
            if p['is_out']:
                drv.setdefault(netof(p['k']), []).append((c, p))
    return cells, netof, drv


def sn(n):
    s = n
    for p in ('NET.PLA1.', 'NET.', 'PAD.', 'CLK.', 'GBL.', 'BUS.'):
        if s.startswith(p):
            s = s[len(p):]
            break
    return s.replace('~{', '/').replace('}', '')


def show(net, drv, netof, depth, seen=None, ind=0):
    seen = seen if seen is not None else set()
    pad = '  ' * ind
    if net not in drv:
        print(f"{pad}{sn(net)}   <== PRIMARY")
        return
    if net in seen:
        print(f"{pad}{sn(net)}   <== (loop)")
        return
    seen.add(net)
    for c, p in drv[net]:
        ins = sorted([q for q in c['pins'] if not q['is_out']],
                     key=lambda q: -q['y'])
        desc = ' '.join(f"{q['name'] or '.'}:{sn(netof(q['k']))}" for q in ins)
        star = '' if p['style'] != 'inverted' else '~'
        print(f"{pad}{sn(net)} = {star}{c['lib']}({c['ref']})  [{desc}]")
        if ind < depth:
            for q in ins:
                show(netof(q['k']), drv, netof, depth, set(seen), ind + 1)


if __name__ == '__main__':
    path = sys.argv[1]
    cells, netof, drv = load(path)
    depth = int(sys.argv[2])
    for want in sys.argv[3:]:
        print(f"\n########## {want}")
        # accept either a full net name or a coordinate-style alias
        target = want if want in drv or want not in drv else want
        show(target, drv, netof, depth)
