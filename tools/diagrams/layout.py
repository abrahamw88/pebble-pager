"""Pebble Pager board layout on a stock 40 x 60 mm, 14 x 20 hole board with its bottom border cut off.
Holes are (column, row) from the top left, seen from the front (the ring side).

Every connection is an insulated wire lap-soldered onto the leads it joins (bared where the run is short).
A wire on the back is lapped onto a lead on the back where one is there to reach; a XIAO pin is not (its header covers
it), so a wire on the back passes up through a free hole beside it and is lapped onto the pin on the front."""
import math
COLS, ROWS, PITCH = 14, 20, 2.54
XIAO_COL = 8                               # the XIAO (and the USB-C port) is centered on this column
BOARD_DY = -1.5                            # mm: the board, its parts, the battery and the motor sit this far toward the USB end
def xy(h):                                 # hole -> mm from the case center (x right, y up); the ring is centered there
    return ((h[0] - XIAO_COL) * PITCH, (7 - h[1]) * PITCH + BOARD_DY)
BOARD = (-21.27, 18.73, -34.29 + BOARD_DY, -34.29 + 1.27 + 19 * PITCH + 5.87 + BOARD_DY)   # x0, x1, y0, y1 in mm
RING_IN, RING_OUT = 11.65, 18.4
SKIRT_IN = 9.88                            # button 1's cap: a skirt from here out to the ring stops 0.25 mm above the front
BATTERY_BOTTOM = -14.4 + BOARD_DY
SCREW_HOLES = [(1, 1), (14, 1), (1, 13), (14, 13)]   # drilled out to 2.5 mm for M2.3 screws into posts in the case front
SCREW_HOLE_R, POST_R = 1.25, 2.1          # mm; each post ends on the board's front face
SUPPORTS = [(17.0, -30.0 + BOARD_DY, 1.5), (-20.5, -30.0 + BOARD_DY, 1.5)]   # mm, x, y, radius: case-front supports resting on the board's front
MAX_ON_LEAD = 3                            # wires lapped onto one lead, at most

# pins and legs: hole -> (net, leg name, part, side). A net ending in * is a spare leg (joined only inside its switch).
PINS = {}
for i, (l, r) in enumerate(zip(['D6', 'D5', 'D4', 'D3', 'D2', 'D1', 'D0'], ['D7', 'D8', 'D9', 'D10', '3V3', 'GND', '5V'])):
    PINS[(5, 14 + i)] = (l, l, 'XIAO', 'B')
    PINS[(11, 14 + i)] = (r, r, 'XIAO', 'B')
PINS.update({
    (6, 6): ('GND', '', 'Button 1', 'F'), (9, 6): ('GND*', '', 'Button 1', 'F'),
    (6, 8): ('D2*', '', 'Button 1', 'F'), (9, 8): ('D2', '', 'Button 1', 'F'),
    (1, 18): ('D3*', '', 'Button 2', 'B'), (4, 18): ('D3', '', 'Button 2', 'B'),
    (1, 20): ('GND', '', 'Button 2', 'B'), (4, 20): ('GND*', '', 'Button 2', 'B'),
    (1, 14): ('RGND', 'C', 'Q1', 'B'), (1, 15): ('BASE1', 'B', 'Q1', 'B'), (1, 16): ('GND', 'E', 'Q1', 'B'),
    (14, 14): ('MOT-', 'C', 'Q2', 'B'), (14, 15): ('BASE2', 'B', 'Q2', 'B'), (14, 16): ('GND', 'E', 'Q2', 'B'),
})
INTERNAL = [((6, 6), (9, 6)), ((6, 8), (9, 8)), ((1, 18), (4, 18)), ((1, 20), (4, 20))]
PARTS = [
    ('R1', '470 Ω', (6, 15), (9, 15), 'D5', 'BASE1'),
    ('R2', '10 kΩ', (3, 13), (3, 16), 'BASE1', 'GND'),
    ('R3', '1 kΩ', (6, 16), (9, 16), 'D4', 'BASE2'),
    ('R4', '10 kΩ', (13, 16), (13, 19), 'BASE2', 'GND'),
    ('R5', '330 Ω', (10, 17), (7, 17), 'D10', 'RIN'),
    ('R6', '220 kΩ', (6, 19), (9, 19), 'D1', 'BAT'),
    ('R7', '220 kΩ', (4, 19), (1, 19), 'D1', 'GND'),
    ('D1', '1N4148', (12, 15), (12, 18), 'MOT-', '3V3'),     # on the back; stripe at the 3V3 end
]
BACK_PARTS = {'D1'}
PART_USE = {'R1': 'D5 to Q1 base', 'R2': 'Q1 base to ground', 'R3': 'D4 to Q2 base', 'R4': 'Q2 base to ground',
            'R5': 'D10 to the ring data wire', 'R6': 'battery + to D1', 'R7': 'D1 to ground', 'D1': 'across the motor'}

# wires: (name, net, legs); a leg is (side, points); the first and last points are leads, a point where legs meet is a
# free hole the wire passes through, and the points between are only the route
WIRES = [
    ('W1', 'D5', [('F', [(5, 15), (6, 15)])]),
    ('W2', 'D4', [('F', [(5, 16), (6, 16)])]),
    ('W3', 'D3', [('F', [(5, 17), (4, 18)])]),
    ('W4', 'D1', [('F', [(5, 19), (6, 19)])]),
    ('W5', 'D1', [('F', [(5, 19), (4, 19)])]),
    ('W6', 'D10', [('F', [(11, 17), (10, 17)])]),
    ('W7', '3V3', [('F', [(11, 18), (12, 18)])]),
    ('W8', 'BASE1', [('F', [(9, 15), (9.25, 14.45), (3.7, 14.45), (3.7, 13.0), (3, 13)])]),
    ('W9', 'BASE1', [('F', [(3, 13), (2.0, 13.5), (1.6, 14.5), (1, 15)])]),
    ('W10', 'BASE2', [('F', [(9, 16), (9.3, 15.55), (12.6, 15.55), (13, 16)])]),
    ('W11', 'BASE2', [('F', [(13, 16), (14, 15)])]),
    ('W12', 'MOT-', [('F', [(14, 14), (12, 15)])]),
    ('W13', 'GND', [('F', [(11, 19), (11.5, 19.42), (12.5, 19.42), (13, 19)])]),
    ('W14', 'GND', [('F', [(13, 19), (13.62, 18.7), (13.82, 18.3), (13.82, 16.45), (14, 16)])]),
    ('W15', 'GND', [('F', [(3, 16), (1, 16)])]),
    ('W17', 'GND', [('F', [(1, 19), (1, 20)])]),
    ('A', 'D2', [('F', [(5, 18), (6, 18)]), ('B', [(6, 18), (5.62, 17.5), (5.62, 10.4), (9, 8)])]),
    ('B', 'GND', [('B', [(3, 16), (4.0, 15.2), (4.0, 7.0), (6, 6)])]),
    ('C', 'GND', [('B', [(13, 19), (12.45, 18.45), (12.45, 13.176), (9.6, 10.326), (9.6, 5.3), (6.6, 5.3), (6, 6)])]),
    ('D', 'GND', [('B', [(3, 16), (2.0, 16.9), (0.65, 17.4), (0.65, 18.6), (1, 19)])]),
]
WIRE_USE = {'W1': 'D5 to R1', 'W2': 'D4 to R3', 'W3': 'D3 to button 2', 'W4': 'D1 to R6', 'W5': 'D1 to R7', 'W6': 'D10 to R5',
            'W7': '3V3 to the diode', 'W8': 'R1 to R2 (Q1 base)', 'W9': 'R2 to Q1 base', 'W10': 'R3 to R4 (Q2 base)',
            'W11': 'R4 to Q2 base', 'W12': 'Q2 collector to the diode', 'W13': 'ground to R4', 'W14': 'R4 to Q2 emitter',
            'W15': 'R2 to Q1 emitter', 'W17': 'R7 to button 2',
            'A': 'D2 to button 1, on the back', 'B': 'ground from R2 to button 1, on the back', 'C': 'ground from R4 to button 1, on the back', 'D': 'ground from R2 to R7, on the back'}

# off the board: (number, what, net, the lead it is lapped onto, where its number is drawn)
OFFBOARD = [
    (1, 'Ring GND pad', 'RGND', (1, 14), (0.05, 13.55)),
    (2, 'Ring PWR pad', '3V3', (11, 18), (10.45, 18.5)),
    (3, 'Ring DIN pad', 'RIN', (7, 17), (6.5, 17.5)),
    (4, 'Motor red (+)', '3V3', (12, 18), (12.0, 18.62)),
    (5, 'Motor blue (−)', 'MOT-', (12, 15), (11.5, 14.5)),
    (6, 'Capacitor − (striped)', 'GND', (1, 16), (0.05, 16.45)),
]
OFF_NOTE = {2: 'the capacitor\'s + lead solders onto this ring pad too'}
# the BAT+ sense wire: from the XIAO's BAT+ pad, under the XIAO and round the board's bottom edge (row 20.5), onto R6;
# its insulation is too thick to pass up through a hole
SENSE_BACK, SENSE_FRONT = [(8.5, 18), (8.5, 20.5)], [(8.5, 20.5), (9, 20), (9, 19)]
SENSE_LEAD = SENSE_FRONT[-1]

# parts on the back, in hole units: (col0, col1, row0, row1)
BACK_BODIES = {'XIAO': (4.5, 11.5, 12.9, 20.6), 'Button 2': (1.32, 3.68, 17.8, 20.2), 'Q1': (0.27, 1.73, 14.1, 15.9), 'Q2': (13.27, 14.73, 14.1, 15.9), 'D1': (11.63, 12.37, 15.5, 17.5)}
HEADERS = [(4.5, 5.5, 13.5, 20.5), (10.5, 11.5, 13.5, 20.5)]   # the XIAO's header strips sit flat on the back
NETS = ['GND', '3V3', 'D1', 'D2', 'D3', 'D4', 'D5', 'D10', 'BASE1', 'BASE2', 'RGND', 'MOT-', 'RIN', 'BAT']

def r_of(p):
    return math.hypot(*xy(p))
def leads():
    out = {}
    for h, p in PINS.items():
        out[h] = (p[0], p[2] + (' ' + p[1] if p[1] else ''))
    for (n, v, a, b, na, nb) in PARTS:
        out[a] = (na, n); out[b] = (nb, n)
    return out
def ends(w):
    return w[2][0][1][0], w[2][-1][1][-1]
def passes(w):
    return [leg[1][-1] for leg in w[2][:-1]]
def segs(w, side):
    for s, pts in w[2]:
        if s == side:
            for a, b in zip(pts, pts[1:]):
                yield a, b
def seg_dist(p, a, b):
    ax, ay, bx, by, px, py = a[0], a[1], b[0], b[1], p[0], p[1]
    dx, dy = bx - ax, by - ay
    t = max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy or 1)))
    return math.hypot(ax + t * dx - px, ay + t * dy - py)
def crosses(a, b, c, d):
    def o(p, q, r): return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    if {a, b} & {c, d}: return False
    return o(a, b, c) * o(a, b, d) < 0 and o(c, d, a) * o(c, d, b) < 0

def keepouts():                            # (x, y, r) in mm on the front: the screw posts and the supports
    return [xy(h) + (POST_R,) for h in SCREW_HOLES] + SUPPORTS

def check():
    errs = []; L = leads()
    for h in SCREW_HOLES:
        if h in L: errs.append('screw hole %s holds a lead' % (h,))
    spare = {h for h, p in PINS.items() if p[0].endswith('*')}
    for h in list(L) + [p for w in WIRES for p in passes(w)]:
        if not (1 <= h[0] <= COLS and 1 <= h[1] <= ROWS): errs.append('%s off the board' % (h,))
    seen = {}
    for (n, v, a, b, na, nb) in PARTS:
        for h in (a, b):
            if h in PINS or h in seen: errs.append('hole %s holds two leads' % (h,))
            seen[h] = n
        if abs(a[0] - b[0]) + abs(a[1] - b[1]) < 3: errs.append('%s spans fewer than 4 holes' % n)
    # wires: ends on leads of their own net, pass-through holes free and used once
    count, used = {}, {}
    for w in WIRES:
        name, net = w[0], w[1]
        for e in ends(w):
            if e not in L: errs.append('%s ends at %s, which is not a lead' % (name, e))
            elif e in spare: errs.append('%s ends on a spare leg %s' % (name, e))
            elif L[e][0] != net: errs.append('%s (%s) lands on %s, which is %s' % (name, net, e, L[e][0]))
            count[e] = count.get(e, 0) + 1
        for p in passes(w):
            if p in L or p in used: errs.append('%s passes through %s, which is not free' % (name, p))
            used[p] = name
    for (k, what, net, h, at) in OFFBOARD:
        if h not in L or L[h][0] != net: errs.append('off-board %d (%s) lands on %s, not %s' % (k, what, h, net))
        count[h] = count.get(h, 0) + 1
    for w in WIRES:
        for p in passes(w):
            if p in SCREW_HOLES: errs.append('%s passes through screw hole %s' % (w[0], p))
    if L.get(SENSE_LEAD, ('',))[0] != 'BAT': errs.append('the sense wire does not land on BAT')
    count[SENSE_LEAD] = count.get(SENSE_LEAD, 0) + 1
    for h, k in count.items():
        if k > MAX_ON_LEAD: errs.append('%d wires on %s' % (k, h))
    # every net in one piece, and no shorts, also through a switch
    parent = {}
    def find(h):
        parent.setdefault(h, h)
        while parent[h] != h: parent[h] = parent[parent[h]]; h = parent[h]
        return h
    for w in WIRES:
        a, b = ends(w); parent[find(a)] = find(b)
    for stage in ('', ' through a switch'):
        groups = {}
        for h, (net, part) in L.items():
            if h in spare and not stage: continue
            groups.setdefault(net.rstrip('*'), set()).add(find(h))
        for net in NETS:
            if net in ('RIN', 'RGND', 'BAT'): continue      # one lead each on the board; the rest is off it
            if len(groups.get(net, ())) > 1 and not stage: errs.append('%s is in %d pieces' % (net, len(groups[net])))
        byroot = {}
        for h, (net, part) in L.items():
            byroot.setdefault(find(h), set()).add(net.rstrip('*'))
        for r, nets in byroot.items():
            if len(nets) > 1: errs.append('short%s: %s' % (stage, sorted(nets)))
        for a, b in INTERNAL: parent[find(a)] = find(b)
    # front: nothing bare under the ring or the cap skirt; no wire across the skirt, over a joint, a resistor, or another wire
    front_joints = {h: L[h][1] for h in L} | {p: 'hole for ' + n for p, n in used.items()}
    for h, what in front_joints.items():
        r = r_of(h)
        if RING_IN - 0.5 <= r <= RING_OUT + 0.5 or SKIRT_IN - 0.5 <= r < RING_IN - 0.5:
            errs.append('%s at %s is under the ring or the cap skirt' % (what, h))
    fsegs = [(w[0], a, b) for w in WIRES for a, b in segs(w, 'F')] + [('sense', a, b) for a, b in zip(SENSE_FRONT, SENSE_FRONT[1:])]
    bodies = [(n, a, b) for (n, v, a, b, na, nb) in PARTS if n not in BACK_PARTS]
    for name, a, b in fsegs:
        for k in range(21):
            p = (a[0] + (b[0] - a[0]) * k / 20, a[1] + (b[1] - a[1]) * k / 20)
            if SKIRT_IN - 0.5 <= r_of(p) <= RING_IN:
                errs.append('%s crosses the cap skirt on the front' % name); break
        for h, what in front_joints.items():
            if h not in (a, b) and seg_dist(h, a, b) < 0.36:
                errs.append('%s runs over %s at %s' % (name, what, h))
        for n, ra, rb in bodies:
            ca, cb = sorted((ra, rb))
            mid = ((ra[0] + rb[0]) / 2, (ra[1] + rb[1]) / 2)
            half = (abs(rb[0] - ra[0]) + abs(rb[1] - ra[1])) / 2 * 0.0 + 6.3 / 2 / PITCH
            for k in range(1, 20):
                p = (a[0] + (b[0] - a[0]) * k / 20, a[1] + (b[1] - a[1]) * k / 20)
                along = (p[0] - mid[0]) if ra[1] == rb[1] else (p[1] - mid[1])
                across = (p[1] - mid[1]) if ra[1] == rb[1] else (p[0] - mid[0])
                if abs(along) < half and abs(across) < 0.5:
                    errs.append('%s runs over %s' % (name, n)); break
    for name, a, b in fsegs:
        for (kx, ky, kr) in keepouts():
            kc = (XIAO_COL + kx / PITCH, 7 - (ky - BOARD_DY) / PITCH)
            if seg_dist(kc, a, b) * PITCH < kr + 0.4:
                errs.append('%s runs under a case post or support at (%.1f, %.1f) mm' % (name, kx, ky)); break
    for i, (n1, a, b) in enumerate(fsegs):
        for (n2, c, d) in fsegs[i + 1:]:
            if n1 != n2 and crosses(a, b, c, d): errs.append('%s crosses %s on the front' % (n1, n2))
    # back: wires clear of the parts and header strips on the back
    for w in WIRES:
        for a, b in segs(w, 'B'):
            for k in range(1, 20):
                p = (a[0] + (b[0] - a[0]) * k / 20, a[1] + (b[1] - a[1]) * k / 20)
                for nm, (c0, c1, r0, r1) in list(BACK_BODIES.items()) + [('header', hd) for hd in HEADERS]:
                    if nm == 'XIAO': continue                     # under the XIAO is the header gap
                    if c0 <= p[0] <= c1 and r0 <= p[1] <= r1:
                        errs.append('%s runs into %s on the back' % (w[0], nm)); break
        for side, pts in w[2]:
            for e in (pts[0], pts[-1]):
                if side == 'B' and e in L and e in PINS and PINS[e][3] == 'B':
                    errs.append('%s is lapped on the back onto %s, which a part on the back covers' % (w[0], L[e][1]))
    for nm in ('Button 2', 'Q1', 'Q2'):
        c0, c1, r0, r1 = BACK_BODIES[nm]
        if (7 - r0) * PITCH + BOARD_DY > BATTERY_BOTTOM - 0.3: errs.append('%s reaches the battery' % nm)
    return sorted(set(errs)), count

def warnings():                            # joints a post or support comes close to (a joint is about 1 mm across)
    out = []
    for h, (net, what) in leads().items():
        hx_, hy_ = xy(h)
        for (kx, ky, kr) in keepouts():
            gap = math.hypot(hx_ - kx, hy_ - ky) - kr - 0.9
            if gap < 0.3: out.append('%s at %s: %.2f mm from the post or support at (%.1f, %.1f)' % (what, h, gap, kx, ky))
    return out

if __name__ == '__main__':
    e, count = check()
    nf = sum(1 for w in WIRES if all(s == 'F' for s, p in w[2]))
    print('\n'.join(e) if e else 'layout OK: %d wires (%d on the front, %d on the back), %d off-board leads, most on one lead %d (%s)' % (
        len(WIRES), nf, len(WIRES) - nf, len(OFFBOARD) + 1, max(count.values()), [h for h, k in count.items() if k == max(count.values())]))
    for w in warnings(): print('warning:', w)
