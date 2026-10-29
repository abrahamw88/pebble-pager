"""Checks that perfboard-front.svg and perfboard-back.svg match layout.py. Run: python3 verify.py [dir]"""
import re, sys, math
import os
HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, '..', '..', 'docs', 'img')
sys.path.insert(0, HERE)
from layout import *
S = 34.0 / PITCH; OX, OY = 470.0, 630.0
D = sys.argv[1] if len(sys.argv) > 1 else IMG
svg = ''.join(open(os.path.join(D, f)).read() for f in ('perfboard-front.svg', 'perfboard-back.svg'))
F0, B0 = 90 + OX, 1100 + OX                       # board centers of the two views
def pxF(h): return (F0 + (h[0] - XIAO_COL) * PITCH * S, OY - ((7 - h[1]) * PITCH + BOARD_DY) * S)
def pxB(h): return (B0 - (h[0] - XIAO_COL) * PITCH * S, OY - ((7 - h[1]) * PITCH + BOARD_DY) * S)
els = re.findall(r"<(circle|rect|path|g|line)\b([^>]*)>", svg)
def num(a, k):
    m = re.search(r"\b%s='([-0-9.]+)'" % k, a); return float(m.group(1)) if m else None
def near(p, q, t=0.15): return abs(p[0] - q[0]) <= t and abs(p[1] - q[1]) <= t
errs = []
view = lambda x: 'F' if x < 1040 else 'B'
# 1. the hole grid
holes = [(num(a, 'cx'), num(a, 'cy')) for t, a in els if t == 'circle' and "r='3'" in a and "#C4C6CB" in a]
for V, pf in (('F', pxF), ('B', pxB)):
    got = [h for h in holes if view(h[0]) == V]
    want = [pf((c, r)) for c in range(1, COLS + 1) for r in range(1, ROWS + 1) if (c, r) not in SCREW_HOLES]
    miss = [w for w in want if not any(near(w, g) for g in got)]
    if len(got) != len(want) or miss: errs.append('%s view: %d holes drawn, %d expected, %d missing' % (V, len(got), len(want), len(miss)))
# 2. lead markers (grey circle r4.4, gold 12x12 square, 24x13 XIAO pin) at exactly the lead holes
lead_holes = set(PINS) | {h for (n, v, a, b, na, nb) in PARTS for h in (a, b)}
marks = []
for t, a in els:
    if t == 'circle' and "r='4.4'" in a: marks.append((num(a, 'cx'), num(a, 'cy'), 'lead'))
    if t == 'rect' and re.search(r"width='12' height='12'", a) and '#D9B45A' in a: marks.append((num(a, 'x') + 6, num(a, 'y') + 6, 'joint'))
    if t == 'rect' and re.search(r"width='24' height='13'", a): marks.append((num(a, 'x') + 12, num(a, 'y') + 6.5, 'joint'))
for V, pf in (('F', pxF), ('B', pxB)):
    got = [m for m in marks if view(m[0]) == V and 100 < m[1] < 1100]   # not the key's sample marks
    for h in lead_holes:
        p = pf(h); hits = [m for m in got if near(p, m)]
        if len(hits) != 1: errs.append('%s view: lead %s %s has %d markers' % (V, h, (PINS.get(h) or ('?',))[0], len(hits)))
        else:
            side = PINS[h][3] if h in PINS else ('B' if any(n in BACK_PARTS and h in (a, b) for (n, v, a, b, na, nb) in PARTS) else 'F')
            want = 'lead' if ((V == 'B') == (side == 'B')) else 'joint'
            if hits[0][2] != want: errs.append('%s view: lead %s is a %s, expected a %s' % (V, h, hits[0][2], want))
    if len(got) != len(lead_holes): errs.append('%s view: %d lead markers for %d leads' % (V, len(got), len(lead_holes)))
# 3. wires: every route is drawn in each view at the mirrored place, solid on the near side, dotted on the far side
paths = []
for t, a in els:
    if t == 'path':
        d = re.search(r"d='M([^']*)'", a)
        if d and 'stroke-dasharray' in a and "0.1 6" in a: kind = 'dot'
        elif d and "stroke-width='3.6'" in a: kind = 'solid'
        else: continue
        pts = [tuple(map(float, s.split())) for s in d.group(1).replace('L', ' L').split(' L') if s.strip()]
        paths.append((kind, pts))
for V, pf in (('F', pxF), ('B', pxB)):
    for w in WIRES:
        for side, pts in w[2]:
            want = [pf(p) if isinstance(p, tuple) and True else p for p in pts]
            # the hole-unit points convert with the same formula as P() in the generator
            want = [(( (F0 if V == 'F' else B0) + (1 if V == 'F' else -1) * (p[0] - XIAO_COL) * PITCH * S), OY - ((7 - p[1]) * PITCH + BOARD_DY) * S) for p in pts]
            kind = 'solid' if ((V == 'B') == (side == 'B')) else 'dot'
            if not any(k == kind and len(q) == len(want) and all(near(a, b, 0.2) for a, b in zip(q, want)) for k, q in paths):
                errs.append('%s view: wire %s (%s side) not drawn as %s' % (V, w[0], side, kind))
# 4. resistor bodies and the screw holes / posts
bodies = [(re.search(r"translate\(([-0-9.]+) ([-0-9.]+)\) rotate\((\d+)\)", a)) for t, a in els if t == 'g' and 'translate' in a and 'rotate' in a]
bodies = [(float(m.group(1)), float(m.group(2)), int(m.group(3))) for m in bodies]
for V, pf in (('F', pxF), ('B', pxB)):
    for (n, v, a, b, na, nb) in PARTS:
        if n in BACK_PARTS: continue
        pa, pb = pf(a), pf(b); mid = ((pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2); rot = 90 if a[0] == b[0] else 0
        if not any(near(mid, (x, y), 0.2) and r == rot for x, y, r in bodies): errs.append('%s view: body of %s misplaced' % (V, n))
circ = [(num(a, 'cx'), num(a, 'cy'), num(a, 'r')) for t, a in els if t == 'circle']
for V, pf in (('F', pxF), ('B', pxB)):
    for h in SCREW_HOLES:
        if not any(near(pf(h), (x, y)) and abs(r - SCREW_HOLE_R * S) < 0.1 for x, y, r in circ): errs.append('%s view: screw hole %s missing' % (V, h))
    for (kx, ky, kr) in keepouts():
        p = (( (F0 if V == 'F' else B0) + (1 if V == 'F' else -1) * kx * S), OY - ky * S)
        if not any(near(p, (x, y)) and abs(r - kr * S) < 0.1 for x, y, r in circ): errs.append('%s view: post/support at (%.1f, %.1f) missing' % (V, kx, ky))
# 5. the XIAO, USB-C, battery are centered on the same axis in both views
for V, c in (('F', F0), ('B', B0)):
    xs = [(num(a, 'x'), num(a, 'width')) for t, a in els if t == 'rect' and abs((num(a, 'width') or 0) - 17.8 * S) < 0.2]
    xs = [x for x in xs if view(x[0]) == V]
    if not xs or abs(xs[0][0] + xs[0][1] / 2 - c) > 0.2: errs.append('%s view: XIAO not centered on the board axis' % V)
print('\n'.join(errs) if errs else 'ALL CHECKS PASS: holes, %d leads, %d wire legs, %d resistors, screws and posts match the layout in both views' % (len(lead_holes), sum(len(w[2]) for w in WIRES), 7))
