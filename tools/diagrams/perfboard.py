"""Draws docs/img/perfboard-front.svg and perfboard-back.svg, the two views of the board, from layout.py.
Run: python3 perfboard.py [out-dir]"""
import sys, math
import os
HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, '..', '..', 'docs', 'img')
sys.path.insert(0, HERE)
from layout import *
S = 34.0 / PITCH
OX, OY = 470.0, 630.0
SG, OFFX = 1, 0
X = lambda x: OFFX + OX + SG * x * S
Y = lambda y: OY - y * S
hx = lambda h: X(xy(h)[0]); hy = lambda h: Y(xy(h)[1])
W, H = 1040, 1520                          # each file; the back view is drawn at x 1040 to 2080 and shown from there
INK, MUTE, LINE, OFF = '#1F2124', '#5F6267', '#8A8D93', '#7A4FC0'
NETC = {'GND': '#1F2124', '3V3': '#D64545', 'D1': '#C99700', 'D2': '#E08A2E', 'D3': '#2F9E6E', 'D4': '#8E5BD0', 'D5': '#3B7DD8',
        'D10': '#1A9E96', 'BASE1': '#74A9EC', 'BASE2': '#B99BE6', 'MOT-': '#A0673A', 'BAT': OFF}
SENSE_C = '#D64545'                         # the battery sense wire carries the battery's voltage: red
NETNAME = [('GND', 'ground'), ('3V3', '3V3'), ('D1', 'D1, battery sense'), ('D2', 'D2, button 1'), ('D3', 'D3, button 2'), ('D4', 'D4, motor'),
           ('D5', 'D5, ring power'), ('D10', 'D10, ring data'), ('BASE1', 'Q1 base'), ('BASE2', 'Q2 base'), ('MOT-', 'motor −')]
o, TXT = [], []
A = o.append
halo = "stroke='#FFFFFF' stroke-width='3.5' stroke-linejoin='round' paint-order='stroke'"
def text(x, y, s, size=10, weight=None, fill=INK, anchor=None, h=False, extra=''):
    TXT.append("<text x='%.1f' y='%.1f' font-size='%g'%s%s%s fill='%s'%s>%s</text>" % (x, y, size, " font-weight='%s'" % weight if weight else '', " text-anchor='%s'" % anchor if anchor else '', ' ' + halo if h else '', fill, extra, s))
def P(p):                                   # hole units -> px
    return X((p[0] - XIAO_COL) * PITCH), Y((7 - p[1]) * PITCH + BOARD_DY)
def path(pts):
    return 'M' + ' L'.join('%.1f %.1f' % P(p) for p in pts)

def take():                                  # what has been drawn since the last call: (shapes, text)
    out = (o[:], TXT[:]); del o[:]; del TXT[:]
    return out
text(1124, 32, 'The same board, back view: flipped left to right, as you see it turning the board over', 16, 600)
text(1124, 54, 'Column 14 is on the left and column 1 on the right; rows are unchanged. The side facing you is solid and the far side dashed or dotted, as in the front view.', 11.5, fill=MUTE)
text(1124, 72, 'The key below covers both views. Here the back parts and wires A to D are solid, and the front parts and wires are the dashed ones.', 11.5, fill=MUTE)
BACK_TITLE = take()
text(24, 32, 'Perfboard layout, front view (the ring side)', 16, 600)
text(24, 54, 'A stock 40 × 60 mm board with 14 × 20 holes, its bottom border cut off, drawn to scale. Every line is one insulated wire, lap-soldered onto the', 11.5, fill=MUTE)
text(24, 72, 'leads at its ends; bare it where the run is short. Purple numbers are the parts off the board: the same number on the part and on the lead it joins.', 11.5, fill=MUTE)

x0, x1, y0, y1 = BOARD
SG, OFFX = 1, 0       # 1 / -1: the front view / the mirrored back view; OFFX: where the view's board center sits
def anc(a):           # flip a text anchor when the view is mirrored
    return a if SG > 0 else {'start': 'end', 'end': 'start'}.get(a, a)
def badge(k, x, y, to=None, layer=TXT):
    if to:
        layer.append("<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='%s' stroke-width='1.6'/>" % (x, y, to[0], to[1], OFF))
    layer.append("<circle cx='%.1f' cy='%.1f' r='7.5' fill='%s' stroke='#FFFFFF' stroke-width='1.5'/><text x='%.1f' y='%.1f' font-size='9.5' font-weight='700' fill='#FFFFFF' text-anchor='middle'>%s</text>" % (x, y, OFF, x, y + 3.4, k))
def view(M, offx):
    # M False: the front view. M True: the back view, the board turned over (flipped left to right).
    # The side facing you is drawn solid; the far side is drawn dashed or dotted, so both views read the same way.
    global SG, OFFX
    SG, OFFX = (-1 if M else 1), offx
    FAR_DASH = "stroke-dasharray='6 4'"
    def far(side):                                  # is this side of the board away from the viewer?
        return (side == 'F') if M else (side == 'B')
    # the battery, then the board
    bl = min(X(-17), X(17))
    A("<rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' rx='6' fill='#6B6E73' fill-opacity='%s' stroke='%s' stroke-width='%s'%s/>" % (
        bl, Y(BATTERY_BOTTOM + 53), 34 * S, 53 * S, '0.10' if M else '0.05', '#8A8D93' if M else '#B9BBC0', '1.6' if M else '1', '' if M else ''))
    outline = [(x0, y1), (x1, y1), (x1, y0 + 1.5), (x1 - 1.5, y0), (x0 + 3.0, y0), (x0, y0 + 3.0)]   # the corners cut 3 mm at column 1 and 1.5 mm at column 14
    A("<path d='M%s Z' fill='#F7F3E8' stroke='%s' stroke-width='1.5' stroke-linejoin='round'/>" % (' L'.join('%.1f %.1f' % (X(a), Y(b)) for a, b in outline), LINE))
    A("<rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' fill='#6B6E73' fill-opacity='%s'/>" % (bl, Y(y1), 34 * S, (y1 - BATTERY_BOTTOM) * S, '0.12' if M else '0.05'))
    A("<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='#9A9CA1' stroke-width='1'/>" % (X(-17), Y(BATTERY_BOTTOM), X(17), Y(BATTERY_BOTTOM)))
    text(X(-16.8), Y(BATTERY_BOTTOM) - 4, 'battery, on the back, above this line', 8.5, None, MUTE, anc('start'), True)
    text(X(0), Y(37.0 + BOARD_DY), 'LiPo battery, on the back of the board (53 × 34 × 10 mm)' if M else 'LiPo battery, behind the board (53 × 34 × 10 mm)', 10, 600, MUTE, 'middle', True)
    for c in range(1, COLS + 1):
        text(hx((c, 1)), Y(y1) - 8, str(c), 9, fill=MUTE, anchor='middle')
    for r in range(1, ROWS + 1):
        text(X(x0) - SG * 10, hy((1, r)) + 3, str(r), 9, fill=MUTE, anchor=anc('end'))
    # the ring and the cap skirt, on the front: areas, dashed edges when the board is turned over
    def annulus(r0, r1, fill, op, edge):
        A("<path d='M%.1f %.1f a%.1f %.1f 0 1 0 %.1f 0 a%.1f %.1f 0 1 0 %.1f 0 Z M%.1f %.1f a%.1f %.1f 0 1 1 %.1f 0 a%.1f %.1f 0 1 1 %.1f 0 Z' fill='%s' fill-opacity='%g' fill-rule='evenodd' stroke='%s' stroke-width='1'%s/>" % (
            X(0) - r1 * S, Y(0), r1 * S, r1 * S, 2 * r1 * S, r1 * S, r1 * S, -2 * r1 * S, X(0) - r0 * S, Y(0), r0 * S, r0 * S, 2 * r0 * S, r0 * S, r0 * S, -2 * r0 * S, fill, op, edge, " stroke-dasharray='2 3'" if M else ''))
    annulus(RING_IN, RING_OUT, '#E86C9A', 0.07, '#F0A5C0')
    annulus(SKIRT_IN, 11.38, '#3A3B3F', 0.10, '#B9BBC0')

    # ---- parts: each is drawn solid on the side facing you, dashed on the far side
    def box(c0, c1, r0, r1, col, near, rx=5, fo=0.06):
        (xa, ya), (xb, yb) = P((c0, r0)), P((c1, r1))
        A("<rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' rx='%g' fill='%s' fill-opacity='%g' stroke='%s' stroke-width='%s'%s/>" % (
            min(xa, xb), ya, abs(xb - xa), yb - ya, rx, col, fo * (2.2 if near else 1) if near else fo, col, '1.8' if near else '1.4', '' if near else ' ' + FAR_DASH))
    def back_parts(near):
        A("<rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' rx='5' fill='#3B7DD8' fill-opacity='%s' stroke='#3B7DD8' stroke-width='%s'%s/>" % (
            X(0) - 8.9 * S, Y(-14.9 + BOARD_DY), 17.8 * S, 20.96 * S, '0.12' if near else '0.05', '1.8' if near else '1.4', '' if near else ' ' + FAR_DASH))
        A("<rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' rx='3' fill='#FFFFFF' fill-opacity='%s' stroke='#3B7DD8' stroke-width='%s'%s/>" % (
            X(0) - 4.47 * S, Y(-29.06 + BOARD_DY), 8.94 * S, (37.61 - 29.06) * S, '0.9' if near else '0.5', '1.8' if near else '1.4', '' if near else ' ' + FAR_DASH))
        if near:
            b2 = BACK_BODIES['Button 2']
            (xa, ya), (xb, yb) = P((b2[0], b2[2])), P((b2[1], b2[3]))
            A("<rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' rx='4' fill='#3A3B3F' fill-opacity='0.88' stroke='%s' stroke-width='1.2'/><circle cx='%.1f' cy='%.1f' r='%.1f' fill='#6B6E73' stroke='#FFFFFF' stroke-width='1.2'/>" % (
                min(xa, xb), ya, abs(xb - xa), yb - ya, INK, (xa + xb) / 2, (ya + yb) / 2, 0.24 * abs(xb - xa)))
            for h in ((1, 18), (4, 18), (1, 20), (4, 20)):
                cx_ = (xa + xb) / 2 + (-1 if h[0] == 1 else 1) * 0.5 * abs(xb - xa) * 0.9
                A("<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='#9A9CA1' stroke-width='3'/>" % (hx(h), hy(h), cx_, (ya + yb) / 2 + (-1 if h[1] == 18 else 1) * 0.5 * (yb - ya) * 0.8))
        else:
            box(*BACK_BODIES['Button 2'], '#3B7DD8', False, 4)
        for q in ('Q1', 'Q2'):
            if near:
                box(*BACK_BODIES[q], '#3A3B3F', True, 6, 0.40)
            else:
                box(*BACK_BODIES[q], '#3A3B3F', False, 6, 0.10)
        dx_, dy_ = P((12, 15.73)); dx2, dy2 = P((12, 17.27))
        if near:
            A("<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='#9A9CA1' stroke-width='2'/>" % (hx((12, 15)), hy((12, 15)), hx((12, 18)), hy((12, 18))))
            A("<rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' rx='6' fill='#F2B8A0' fill-opacity='0.85' stroke='%s' stroke-width='1.2'/>" % (dx_ - 12.7, dy_, 25.4, dy2 - dy_, INK))
            A("<rect x='%.1f' y='%.1f' width='25.4' height='5' fill='%s'/>" % (dx_ - 12.7, dy2 - 9, INK))
        else:
            A("<rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' rx='6' fill='none' stroke='#3A3B3F' stroke-width='1.4' %s/>" % (dx_ - 12.7, dy_, 25.4, dy2 - dy_, FAR_DASH))
    def front_parts(near):
        for (n, v, a, b, na, nb) in PARTS:
            if n in BACK_PARTS:
                continue
            ax, ay, bx, by = hx(a), hy(a), hx(b), hy(b)
            mx_, my_ = (ax + bx) / 2, (ay + by) / 2
            vert = a[0] == b[0]
            L_, T_ = 6.3 * S, 2.4 * S
            if near:
                A("<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='#9A9CA1' stroke-width='2'/>" % (ax, ay, bx, by))
            A("<g transform='translate(%.1f %.1f) rotate(%d)'><rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' rx='%.1f' fill='#E9D9B6' fill-opacity='%s' stroke='%s' stroke-width='%s'%s/></g>" % (
                mx_, my_, 90 if vert else 0, -L_ / 2, -T_ / 2, L_, T_, T_ / 3, '1' if near else '0.35', INK, '1.2' if near else '1.3', '' if near else " stroke-dasharray='5 3'"))
            lab = n + ' ' + v.replace(' ', '')
            ex = (" stroke='#E9D9B6' stroke-width='3' paint-order='stroke'" if near else " stroke='#F7F3E8' stroke-width='3' paint-order='stroke'") + (" transform='rotate(-90 %.1f %.1f)'" % (mx_, my_) if vert else '')
            text(mx_, my_ + 3.5, lab, 9, 600 if near else 500, fill=INK if near else MUTE, anchor='middle', extra=ex)
        bx_, by_ = X(-1.27), Y(BOARD_DY)
        if near:
            A("<rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' rx='5' fill='#3A3B3F' fill-opacity='0.88' stroke='%s' stroke-width='1.2'/><circle cx='%.1f' cy='%.1f' r='23.4' fill='#6B6E73' stroke='#FFFFFF' stroke-width='1.5'/>" % (bx_ - 40.15, by_ - 40.15, 80.3, 80.3, INK, bx_, by_))
            for h in ((6, 6), (9, 6), (6, 8), (9, 8)):
                A("<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='#9A9CA1' stroke-width='3'/>" % (hx(h), hy(h), bx_ + SG * (-38 if h[0] == 6 else 38), by_ + (-28 if h[1] == 6 else 28)))
        else:
            A("<rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' rx='5' fill='#3A3B3F' fill-opacity='0.06' stroke='%s' stroke-width='1.3' stroke-dasharray='6 4'/><circle cx='%.1f' cy='%.1f' r='23.4' fill='none' stroke='%s' stroke-width='1.3' stroke-dasharray='4 3'/>" % (bx_ - 40.15, by_ - 40.15, 80.3, 80.3, INK, bx_, by_, INK))
    # off the board: ring pads, motor, capacitor, antenna, the XIAO's battery pads
    pads = {'DIN': (90, 3, (2.4, 1.9)), 'PWR': (0, 2, (1.9, 2.2)), 'GND': (180, 1, (-1.9, 2.2)), 'DOUT': (270, None, None)}
    def off_board():
        for nm, (ang, k, off) in pads.items():
            a = math.radians(ang); px_, py_ = X(15.0 * math.cos(a)), Y(15.0 * math.sin(a))
            A("<rect x='%.1f' y='%.1f' width='12' height='12' rx='2' fill='#FFFFFF' stroke='%s' stroke-width='1.6'/>" % (px_ - 6, py_ - 6, OFF))
            if k:
                badge(k, px_ + SG * off[0] * S, py_ - off[1] * S, (px_, py_))
                text(px_ + SG * (off[0] * S + (11 if off[0] > 0 else -11)), py_ - off[1] * S + 3.5, nm, 8.5, 700, OFF, anc('start' if off[0] > 0 else 'end'), True)
            else:
                text(px_ + SG * 10, py_ + 3.5, 'DOUT, unused', 8.5, 700, OFF, anc('start'), True)
        text(X(0), Y(12.3), 'LED ring pads, on its back: about every 90°, order to check', 9, 600, OFF, 'middle', True)
        mcx, mcy = X(8.0), Y(30.5 + BOARD_DY)
        A("<circle cx='%.1f' cy='%.1f' r='%.1f' fill='%s' fill-opacity='0.06' stroke='%s' stroke-width='1.6' stroke-dasharray='6 3'/>" % (mcx, mcy, 5 * S, OFF, OFF))
        text(mcx, mcy + 3, 'motor', 10, 600, OFF, 'middle', True)
        for xx, k, lab in ((6.4, 4, 'red'), (9.6, 5, 'blue')):
            yb = 30.5 + BOARD_DY - math.sqrt(25 - (xx - 8) ** 2)
            A("<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='%s' stroke-width='2'/>" % (X(xx), Y(yb), X(xx), Y(23.6 + BOARD_DY), OFF))
            badge(k, X(xx), Y(23.6 + BOARD_DY) + 7.5)
            text(X(xx) + SG * (-11 if k == 4 else 11), Y(23.6 + BOARD_DY) + 11, lab, 8.5, 700, OFF, anc('end' if k == 4 else 'start'), True)
        ccx0, ccx1, ccy = X(-17.6), X(-6.1), Y(28.2 + BOARD_DY)
        A("<rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' rx='10' fill='%s' fill-opacity='0.06' stroke='%s' stroke-width='1.6' stroke-dasharray='6 3'/>" % (min(ccx0, ccx1), ccy - 4 * S, abs(ccx1 - ccx0), 8 * S, OFF, OFF))
        A("<rect x='%.1f' y='%.1f' width='8' height='%.1f' fill='%s' fill-opacity='0.5'/>" % (ccx0 + (6 if SG > 0 else -14), ccy - 4 * S, 8 * S, OFF))
        text((ccx0 + ccx1) / 2, ccy + 3, '1000 µF capacitor', 10, 600, OFF, 'middle', True)
        for xx, k, lab in ((-15.6, 6, '−'), (-8.0, 2, '+')):
            A("<line x1='%.1f' y1='%.1f' x2='%.1f' y2='%.1f' stroke='%s' stroke-width='2'/>" % (X(xx), Y(24.2 + BOARD_DY), X(xx), Y(23.6 + BOARD_DY), OFF))
            badge(k, X(xx), Y(23.6 + BOARD_DY) + 7.5)
            text(X(xx) + SG * 11, Y(23.6 + BOARD_DY) + 11, lab, 10, 700, OFF, anc('start'), True)
        ufl = P((7.0, 13.45))
        A("<circle cx='%.1f' cy='%.1f' r='5' fill='#FFFFFF' stroke='%s' stroke-width='1.6'/><circle cx='%.1f' cy='%.1f' r='1.6' fill='%s'/>" % (ufl[0], ufl[1], OFF, ufl[0], ufl[1], OFF))
        text(ufl[0] - SG * 9, ufl[1] + 3.5, 'antenna (U.FL)', 8.5, 700, OFF, anc('end'), True)
    batp = {'+': P((8.5, 18)), '−': P((7.5, 18))}

    def wires(side_wanted, dotted):
        for w in WIRES:
            for side, pts in w[2]:
                if side != side_wanted:
                    continue
                d = path(pts)
                if dotted:
                    A("<path d='%s' fill='none' stroke='%s' stroke-width='3.2' stroke-linecap='round' stroke-linejoin='round' stroke-dasharray='0.1 6'/>" % (d, NETC[w[1]]))
                else:
                    A("<path d='%s' fill='none' stroke='#FFFFFF' stroke-width='7' stroke-linecap='round' stroke-linejoin='round'/><path d='%s' fill='none' stroke='%s' stroke-width='3.6' stroke-linecap='round' stroke-linejoin='round'/>" % (d, d, NETC[w[1]]))
        # the battery's sense wire, in red: from the BAT+ pad on the back, round the board's bottom edge, onto R6 on the front
        d = path(SENSE_BACK if side_wanted == 'B' else SENSE_FRONT)
        if dotted:
            A("<path d='%s' fill='none' stroke='%s' stroke-width='3.2' stroke-linecap='round' stroke-linejoin='round' stroke-dasharray='0.1 6'/>" % (d, SENSE_C))
        else:
            A("<path d='%s' fill='none' stroke='#FFFFFF' stroke-width='7' stroke-linecap='round' stroke-linejoin='round'/><path d='%s' fill='none' stroke='%s' stroke-width='3.6' stroke-linecap='round' stroke-linejoin='round'/>" % (d, d, SENSE_C))
    near_side, far_side = ('B', 'F') if M else ('F', 'B')
    # layers, bottom to top: the far side's parts, the holes, the far side's wires, this side's parts, this side's wires
    (front_parts if M else back_parts)(False)
    off_board()
    for c in range(1, COLS + 1):
        for r in range(1, ROWS + 1):
            if (c, r) not in SCREW_HOLES:
                A("<circle cx='%.1f' cy='%.1f' r='3' fill='#FFFFFF' stroke='#C4C6CB' stroke-width='1'/>" % (hx((c, r)), hy((c, r))))
    wires(far_side, True)
    (back_parts if M else front_parts)(True)
    wires(near_side, False)
    # where the case's screw posts and supports press on the front (dashed from the back), and the screw holes (heads, from the back)
    KEEP = "fill='#E39B2D' fill-opacity='0.22' stroke='#D08A1E' stroke-width='1.2'" + (" stroke-dasharray='3 3'" if M else '')
    for (kx, ky, kr) in keepouts():
        A("<circle cx='%.1f' cy='%.1f' r='%.1f' %s/>" % (X(kx), Y(ky), kr * S, KEEP))
    for h in SCREW_HOLES:
        A("<circle cx='%.1f' cy='%.1f' r='%.1f' fill='%s' stroke='%s' stroke-width='1.8'/><path d='M%.1f %.1f h%.1f M%.1f %.1f v%.1f' stroke='%s' stroke-width='1'/>" % (
            hx(h), hy(h), SCREW_HOLE_R * S, '#C9CBD0' if M else '#FFFFFF', INK, hx(h) - 6, hy(h), 12, hx(h), hy(h) - 6, 12, INK))
    text(hx((14, 1)) - SG * 22, hy((14, 1)) + 4, 'M2.3, drill 3/32 in (2.5 mm)', 8.5, 700, INK, anc('end'), True)
    # battery pads, on the XIAO's underside (hidden under the XIAO from the back)
    for sgn, (px_, py_) in batp.items():
        A("<rect x='%.1f' y='%.1f' width='13' height='10' rx='2' fill='#FFFFFF' stroke='%s' stroke-width='1.4'%s/><text x='%.1f' y='%.1f' font-size='9' font-weight='700' fill='%s' text-anchor='middle'>%s</text>" % (
            px_ - 6.5, py_ - 5, OFF, " stroke-dasharray='3 2'" if M else '', px_, py_ + 3.3, OFF, sgn))
    # pins, legs and leads. A part on the far side shows gold squares: its joints, soldered on this side.
    # A part on this side shows grey circles: its leads, going through to be soldered on the far side.
    used = {e for w in WIRES for e in ends(w)} | {h for (k, wt, net, h, at) in OFFBOARD} | {SENSE_LEAD}
    def joint(x_, y_, label=None, on=True):
        A("<rect x='%.1f' y='%.1f' width='12' height='12' rx='2' fill='%s' stroke='%s' stroke-width='1'/>" % (x_ - 6, y_ - 6, '#D9B45A' if on else '#EFE3BF', INK if on else '#B8AE8E'))
        if label:
            A("<text x='%.1f' y='%.1f' font-size='7.5' font-weight='700' fill='%s' text-anchor='middle'>%s</text>" % (x_, y_ + 2.7, INK, label))
    def lead(x_, y_, on=True):
        A("<circle cx='%.1f' cy='%.1f' r='4.4' fill='%s' stroke='%s' stroke-width='1'/>" % (x_, y_, '#D0D2D6' if on else '#ECECEE', INK if on else '#B9BBC0'))
    for h, p in PINS.items():
        x_, y_ = hx(h), hy(h)
        isfar = far(p[3])
        if p[2] == 'XIAO':
            on = h in used
            if isfar:
                A("<rect x='%.1f' y='%.1f' width='24' height='13' rx='3' fill='%s' stroke='%s' stroke-width='1'/>" % (x_ - 12, y_ - 6.5, '#D9B45A' if on else '#EFE3BF', INK if on else '#B8AE8E'))
                A("<text x='%.1f' y='%.1f' font-size='7.5' font-weight='700' fill='%s' text-anchor='middle'>%s</text>" % (x_, y_ + 2.7, INK if on else '#8C8471', p[1]))
            else:
                lead(x_, y_, on)
                out = -1 if h[0] == 5 else 1          # the pin names sit outside the pins, in the gap beside the header
                text(x_ + SG * out * 8, y_ + 2.7, p[1], 7.5, 700 if on else 500, INK if on else '#8C8471', anc('start' if out > 0 else 'end'), True)
        elif p[2] in ('Q1', 'Q2'):
            if isfar:
                joint(x_, y_, p[1])
            else:
                lead(x_, y_)
                inward = 1 if p[2] == 'Q1' else -1
                text(x_ + SG * inward * 8, y_ + 2.7, p[1], 7.5, 700, INK, anc('start' if inward > 0 else 'end'), True)
        else:
            (joint if isfar else lead)(x_, y_)
    for (n, v, a, b, na, nb) in PARTS:
        for h in (a, b):
            isfar = far('B' if n in BACK_PARTS else 'F')
            (joint if isfar else lead)(hx(h), hy(h))
    # holes a back wire passes through
    for w in WIRES:
        for p in passes(w):
            A("<circle cx='%.1f' cy='%.1f' r='5.6' fill='#FFFFFF' stroke='%s' stroke-width='2.2'/>" % (hx(p), hy(p), NETC[w[1]]))
    # off-board numbers on the board
    for (k, what, net, h, at) in OFFBOARD:
        badge(k, *P(at), to=(hx(h), hy(h)))
    # names of the wires on the back
    for nm, at, an in (('A', (5.85, 11.6), 'start'), ('B', (3.75, 10.6), 'end'), ('C', (11.176, 11.6), 'start'), ('D', (2.55, 17.55), 'start')):
        w = [w for w in WIRES if w[0] == nm][0]
        text(*P(at), 'Wire %s, back' % nm, 8.5, 700, NETC[w[1]] if w[1] != 'GND' else '#3A3B3F', anc(an), True)
    bx_, by_ = X(-1.27), Y(BOARD_DY)
    # callouts
    def callout(side, y, title, sub, tx, ty):
        if M: side = 'R' if side == 'L' else 'L'          # the sides swap when the board is flipped
        xx = min(X(x0), X(x1)) - 40 if side == 'L' else max(X(x0), X(x1)) + 34
        A("<path d='M%.1f %.1f L%.1f %.1f' fill='none' stroke='#B9BBC0' stroke-width='1'/><circle cx='%.1f' cy='%.1f' r='2.2' fill='#9A9CA1'/>" % (tx, ty, xx, y - 4, tx, ty))
        xt = xx - 6 if side == 'L' else xx + 6
        an = 'end' if side == 'L' else 'start'
        text(xt, y, title, 11, 600, anchor=an); text(xt, y + 14, sub, 10, fill=MUTE, anchor=an)
    callout('L', hy((1, 4)), 'Button 1', 'front, in the ring', bx_ - SG * 30, by_ - 20)
    callout('L', hy((1, 11)), 'Q1 PN2222A, ring switch', 'back, standing, flat face to the edge', *P((0.6, 14.6)))
    callout('L', hy((1, 19)) + 18, 'Button 2', 'back', *P((2.0, 18.4)))
    callout('R', hy((14, 12)), 'Q2 PN2222A, motor switch', 'back, standing, flat face to the XIAO', *P((14.5, 14.6)))
    callout('R', hy((14, 17)) + 6, 'Diode 1N4148', 'back, stripe toward 3V3', *P((12.3, 16.8)))
    callout('R', hy((14, 20)) + 14, 'Bottom corners cut', '3 mm at column 1, 1.5 mm at column 14' if M else '3 mm left, 1.5 mm right', X(x1 - 0.75), Y(y0 + 0.75))
    text(X(0), Y(-33.2 + BOARD_DY), 'USB-C', 8.5, 600, '#3B7DD8', 'middle', True)
    text(X(0), Y(-37.61 + BOARD_DY) + 16, 'XIAO ESP32-C3, on the back: its chip and USB-C face you' if M else 'XIAO ESP32-C3, on the back, chip and USB-C facing away from the board', 9.5, 600, '#3B7DD8', 'middle', True)
FRONT_TITLE = take()
view(False, 90)
FRONT = take()
view(True, 1100)
BACK = take()
SG, OFFX = 1, 0
# key
ky = Y(y0) + 84
text(24, ky, 'Parts off the board', 12, 600)
offs = [(k, what, h) for (k, what, net, h, at) in OFFBOARD]
desc = {1: 'Q1\'s collector, 1, 14 (the ring\'s switched ground)', 2: 'the XIAO\'s 3V3 pin, 11, 18', 3: 'R5\'s end at 7, 17',
        4: 'the diode\'s striped end, 12, 18', 5: 'the diode\'s plain end, 12, 15', 6: 'Q1\'s emitter, 1, 16'}
for i, (k, what, h) in enumerate(offs):
    y = ky + 22 + (i // 2) * 20; x = 24 + (i % 2) * 470
    badge(k, x + 8, y - 4)
    text(x + 22, y, '%s → %s' % (what, desc[k]), 11)
y = ky + 22 + 3 * 20
text(24 + 30, y, 'The capacitor\'s + lead (2) solders onto the ring\'s PWR pad, with the ring\'s own PWR wire.', 11)
text(24 + 30, y + 20, 'The battery\'s leads solder to the XIAO\'s BAT+ and BAT− pads (purple + and −, underneath); a sense wire runs from BAT+ down under the XIAO, round the board\'s bottom edge, onto R6.', 11)
ly = y + 50
text(24, ly, 'Lines and marks', 12, 600)
items = [
    ("<line x1='0' y1='0' x2='26' y2='0' stroke='#E08A2E' stroke-width='3.6' stroke-linecap='round'/>", 'Wire on the side facing you, lapped onto the leads at its ends'),
    ("<line x1='0' y1='0' x2='26' y2='0' stroke='#E08A2E' stroke-width='3.2' stroke-linecap='round' stroke-dasharray='0.1 6'/>", 'Wire on the far side (A to D are the ones on the back)'),
    ("<circle cx='13' cy='0' r='5.6' fill='#FFFFFF' stroke='#E08A2E' stroke-width='2.2'/>", 'Hole a wire passes up through to a XIAO pin (nothing soldered in it)'),
    ("<rect x='1' y='-6.5' width='24' height='13' rx='3' fill='#D9B45A' stroke='#1F2124'/><text x='13' y='2.7' font-size='7.5' font-weight='700' text-anchor='middle'>D5</text>", 'Pin or leg of a part on the far side, soldered on this side'),
    ("<circle cx='13' cy='0' r='4.4' fill='#D0D2D6' stroke='#1F2124'/>", 'Lead of a part on this side, going through to the far side'),
    ("<rect x='0' y='-7' width='26' height='14' rx='4' fill='none' stroke='#3A3B3F' stroke-width='1.4' stroke-dasharray='6 4'/>", 'Outline of a part on the far side'),
    ("<rect x='0' y='-7' width='26' height='14' fill='#E86C9A' fill-opacity='0.12' stroke='#F0A5C0'/>", 'The LED ring: only insulated wires under it'),
    ("<rect x='0' y='-7' width='26' height='14' fill='#3A3B3F' fill-opacity='0.14' stroke='#B9BBC0'/>", 'Button 1\'s cap skirt: nothing on the front crosses it'),
    ("<circle cx='13' cy='0' r='8' fill='#E39B2D' fill-opacity='0.22' stroke='#D08A1E' stroke-width='1.2'/>", 'A case post or support presses here: keep wires off it'),
    ("<circle cx='13' cy='0' r='8' fill='#FFFFFF' stroke='#1F2124' stroke-width='1.8'/><path d='M7 0 h12 M13 -6 v12' stroke='#1F2124'/>", 'Screw hole: drill 3/32 in (or 2.5 mm); the screw goes in from the back'),
    ("<line x1='0' y1='0' x2='20' y2='0' stroke='#B9BBC0'/><circle cx='20' cy='0' r='2.2' fill='#9A9CA1'/>", 'Pointer from a label to its part'),
]
for i, (svg, lab) in enumerate(items):
    y = ly + 22 + (i // 2) * 22; x = 24 + (i % 2) * 470
    A("<g transform='translate(%d %.1f)'>%s</g>" % (x, y - 4, svg))
    text(x + 36, y, lab, 11)
ny = ly + 22 + 6 * 22 + 6
text(24, ny, 'Colour is the signal:', 11, 600)
x = 150
for net, nm in NETNAME:
    A("<line x1='%d' y1='%.1f' x2='%d' y2='%.1f' stroke='%s' stroke-width='3.6' stroke-linecap='round'/>" % (x, ny - 4, x + 14, ny - 4, NETC[net]))
    TXT.append("<text x='%d' y='%.1f' font-size='10.5' fill='%s'>%s</text>" % (x + 19, ny, INK, nm))
    x += 34 + int(len(nm) * 6.0)
    if x > 860 and net != NETNAME[-1][0]:
        x = 150; ny += 18
KEY = take()
def write(name, x0, label, title, view_):
    f = ["<svg xmlns='http://www.w3.org/2000/svg' viewBox='%d 0 %d %d' width='%d' height='%d' role='img' aria-label='Perfboard layout on a 14 by 20 hole board: %s' font-family=\"-apple-system, 'Segoe UI', Helvetica, Arial, sans-serif\">" % (x0, W, H, W, H, label),
         "<rect%s width='%d' height='%d' rx='12' fill='#FFFFFF'/>" % (" x='%d'" % x0 if x0 else '', W, H)]
    f += view_[0] + title[1] + view_[1]
    f += (["<g transform='translate(%d 0)'>" % x0] if x0 else []) + KEY[0] + KEY[1] + (['</g>'] if x0 else []) + ['</svg>']
    open(os.path.join(sys.argv[1] if len(sys.argv) > 1 else IMG, name), 'w').write('\n'.join(f) + '\n')
write('perfboard-front.svg', 0, 'the front view', FRONT_TITLE, FRONT)
write('perfboard-back.svg', 1040, 'the back view, mirrored', BACK_TITLE, BACK)
