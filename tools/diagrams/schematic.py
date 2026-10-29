"""Draws docs/img/schematic.svg. Run: python3 schematic.py [out.svg]"""
import sys, os
IMG = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'docs', 'img')
W, H = 980, 700
INK, MUTE, RED, OFF = '#1F2124', '#5F6267', '#D64545', '#7A4FC0'
o = []; A = o.append
def text(x, y, s, size=11, weight=None, fill=INK, anchor=None, halo=False):
    h = " stroke='#FFFFFF' stroke-width='3.5' stroke-linejoin='round' paint-order='stroke'" if halo else ''
    A("<text x='%.1f' y='%.1f' font-size='%g'%s%s fill='%s'%s>%s</text>" % (x, y, size, " font-weight='%s'" % weight if weight else '', " text-anchor='%s'" % anchor if anchor else '', fill, h, s))
def wire(pts, col=INK, w=1.8):
    A("<path d='M%s' fill='none' stroke='%s' stroke-width='%g' stroke-linejoin='round'/>" % (' L'.join('%.1f %.1f' % p for p in pts), col, w))
def dot(x, y, col=INK):
    A("<circle cx='%.1f' cy='%.1f' r='3.6' fill='%s'/>" % (x, y, col))
def res(x, y, vertical, ref, val, col=INK):
    # body 14 x 40 centred on (x, y)
    if vertical:
        A("<rect x='%.1f' y='%.1f' width='14' height='40' rx='3' fill='#FFFFFF' stroke='%s' stroke-width='1.8'/>" % (x - 7, y - 20, col))
        text(x + 12, y - 2, ref, 11, 700); text(x + 12, y + 12, val, 10, fill=MUTE)
    else:
        A("<rect x='%.1f' y='%.1f' width='40' height='14' rx='3' fill='#FFFFFF' stroke='%s' stroke-width='1.8'/>" % (x - 20, y - 7, col))
        text(x, y - 12, ref + ' ' + val, 10.5, 600, anchor='middle')
def switch(x, y, ref):
    # vertical push button between (x, y-20) and (x, y+20)
    wire([(x, y - 20), (x, y - 10)]); wire([(x, y + 10), (x, y + 20)])
    A("<circle cx='%.1f' cy='%.1f' r='2.6' fill='#FFFFFF' stroke='%s' stroke-width='1.6'/><circle cx='%.1f' cy='%.1f' r='2.6' fill='#FFFFFF' stroke='%s' stroke-width='1.6'/>" % (x, y - 10, INK, x, y + 10, INK))
    wire([(x - 12, y - 14), (x - 12, y + 14)]); wire([(x - 12, y), (x - 20, y)]); wire([(x - 20, y - 6), (x - 20, y + 6)])
    text(x + 10, y + 4, ref, 11, 700)
def npn(x, y, ref, val):
    # body circle at (x, y); base enters from the left at (x-22, y); collector up to (x, y-30); emitter down to (x, y+30)
    A("<circle cx='%.1f' cy='%.1f' r='18' fill='#FFFFFF' stroke='%s' stroke-width='1.6'/>" % (x, y, INK))
    wire([(x - 22, y), (x - 7, y)]); wire([(x - 7, y - 11), (x - 7, y + 11)], w=2.6)
    wire([(x - 7, y - 5), (x, y - 13), (x, y - 30)]); wire([(x - 7, y + 5), (x, y + 13), (x, y + 30)])
    A("<path d='M%.1f %.1f L%.1f %.1f L%.1f %.1f Z' fill='%s'/>" % (x, y + 13, x - 7, y + 12, x - 3, y + 7, INK))
    text(x + 24, y - 2, ref, 11, 700); text(x + 24, y + 12, val, 10, fill=MUTE)
    text(x + 6, y - 22, 'C', 9, fill=MUTE); text(x + 6, y + 30, 'E', 9, fill=MUTE); text(x - 24, y - 6, 'B', 9, fill=MUTE)
def diode(x, y, ref):
    # vertical, cathode (stripe) at the top
    wire([(x, y - 22), (x, y - 8)]); wire([(x, y + 8), (x, y + 22)])
    A("<path d='M%.1f %.1f L%.1f %.1f L%.1f %.1f Z' fill='#FFFFFF' stroke='%s' stroke-width='1.6'/>" % (x - 9, y + 8, x + 9, y + 8, x, y - 7, INK))
    wire([(x - 9, y - 8), (x + 9, y - 8)], w=2.6)
    text(x + 13, y - 2, ref, 11, 700); text(x + 13, y + 12, '1N4148', 10, fill=MUTE)

A("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 %d %d' width='%d' height='%d' role='img' aria-label='Schematic of the Pebble Pager: the XIAO, both buttons, the ring and motor switches, the battery sensor, the LED ring, the motor, the capacitor and the battery' font-family=\"-apple-system, 'Segoe UI', Helvetica, Arial, sans-serif\">" % (W, H, W, H))
A("<rect width='%d' height='%d' rx='12' fill='#FFFFFF'/>" % (W, H))
text(24, 32, 'Schematic: how everything connects', 16, 600)
text(24, 52, 'An electrical reference, not a layout. Part names match the board diagram (R1 to R7, Q1, Q2 and the diode). Purple parts are off the board.', 11.5, fill=MUTE)
text(24, 68, 'A dot joins lines; lines that cross without a dot do not connect.', 11.5, fill=MUTE)
TOP, BOT = 92, 640
# rails
wire([(205, TOP), (950, TOP)], RED, 2.4); text(950, TOP - 8, '3V3', 11, 700, RED, 'end')
wire([(205, BOT), (950, BOT)], INK, 2.4); text(950, BOT + 18, 'GND', 11, 700, INK, 'end')
# XIAO
A("<rect x='40' y='120' width='140' height='470' rx='10' fill='#EEF3FB' stroke='#3B7DD8' stroke-width='1.8'/>")
text(110, 145, 'XIAO', 14, 700, '#3B7DD8', 'middle'); text(110, 162, 'ESP32-C3', 11, 600, '#3B7DD8', 'middle')
pins = [('3V3', 190), ('D1', 240), ('D2', 300), ('D3', 350), ('D4', 410), ('D5', 470), ('D10', 530), ('GND', 556)]
for nm, y in pins:
    text(172, y + 4, nm, 10.5, 600, INK, 'end')
    wire([(180, y), (205, y)])
wire([(205, 190), (205, TOP)], RED, 2); dot(205, TOP, RED)
wire([(205, 556), (205, BOT)]); dot(205, BOT)
text(110, 205, 'unused: D0, D6–D9, 5V', 9, fill=MUTE, anchor='middle')
# antenna and battery pads
wire([(110, 120), (110, 96)], OFF); A("<path d='M98 80 L110 96 L122 80' fill='none' stroke='%s' stroke-width='1.8'/>" % OFF)
text(128, 84, 'antenna (U.FL)', 10, 600, OFF)
for x, nm in ((80, 'BAT+'), (140, 'BAT−')):
    A("<rect x='%d' y='586' width='16' height='8' fill='#FFFFFF' stroke='%s' stroke-width='1.6'/>" % (x - 8, OFF))
    text(x, 580, nm, 9.5, 700, OFF, 'middle')
wire([(80, 594), (80, 640), (100, 640)], OFF); wire([(140, 594), (140, 640), (120, 640)], OFF)
wire([(100, 628), (100, 652)], OFF, 2.6); wire([(108, 634), (108, 646)], OFF, 2.6); wire([(114, 628), (114, 652)], OFF, 2.6); wire([(120, 634), (120, 646)], OFF, 2.6)
wire([(100, 640), (100, 640)])
text(110, 672, 'LiPo 3.7 V', 10.5, 600, OFF, 'middle'); text(110, 686, '(BAT− is the XIAO\'s ground)', 9, fill=MUTE, anchor='middle')
# battery sensor: BAT+ -> R6 -> D1 -> R7 -> GND
x = 255
text(x, 112, 'from BAT+', 10, 700, OFF, 'middle')
wire([(x, 118), (x, 150)]); res(x, 170, True, 'R6', '220 kΩ'); wire([(x, 190), (x, 240)]); dot(x, 240)
wire([(205, 240), (x, 240)]); wire([(x, 240), (x, 568)]); res(x, 590, True, 'R7', '220 kΩ'); wire([(x, 610), (x, BOT)]); dot(x, BOT)
text(x + 12, 128, 'battery sensor', 9.5, 600, MUTE)
# buttons
for (y, xb, ref, lab) in ((300, 338, 'Button 1', 'view and send'), (350, 435, 'Button 2', 'battery, setup, off')):
    wire([(205, y), (xb, y), (xb, 585)])
    switch(xb, 605, ref)
    wire([(xb, 625), (xb, BOT)]); dot(xb, BOT)
    text(xb + 10, 590, lab, 9.5, fill=MUTE) if False else None
text(348, 622, 'view, send', 9, fill=MUTE); text(445, 622, 'battery, setup, off', 9, fill=MUTE)
# motor: 3V3 -> motor -> Q2 collector; diode across the motor; D4 -> R3 -> Q2 base; R4 base -> GND
mx = 600
wire([(mx, TOP), (mx, 128)]); dot(mx, TOP, RED)
A("<circle cx='%d' cy='150' r='20' fill='#FFFFFF' stroke='%s' stroke-width='1.8'/>" % (mx, OFF)); text(mx, 155, 'M', 14, 700, OFF, 'middle')
text(mx - 26, 140, 'motor', 10, 600, OFF, 'end'); text(mx - 26, 154, 'red +, blue −', 9, fill=MUTE, anchor='end')
wire([(mx, 170), (mx, 205)]); dot(mx, 205)
wire([(mx + 45, TOP), (mx + 45, 128)]); dot(mx + 45, TOP, RED)
diode(mx + 45, 150, 'diode'); wire([(mx + 45, 172), (mx + 45, 205), (mx, 205)])
wire([(mx, 205), (mx, 222)])
npn(mx, 252, 'Q2', 'PN2222A')
wire([(mx, 282), (mx, BOT)]); dot(mx, BOT)
wire([(205, 410), (470, 410), (470, 252), (500, 252)]); res(520, 252, False, 'R3', '1 kΩ'); wire([(540, 252), (578, 252)]); dot(552, 252)
wire([(552, 252), (552, 300)]); res(552, 320, True, 'R4', '10 kΩ'); wire([(552, 340), (552, BOT)]); dot(552, BOT)
# ring: 3V3 -> ring PWR; ring GND -> Q1 collector; D5 -> R1 -> Q1 base; R2 base -> GND; D10 -> R5 -> ring DIN
rx = 800
A("<rect x='%d' y='140' width='120' height='90' rx='45' fill='#FDF0F5' stroke='%s' stroke-width='1.8'/>" % (rx - 40, OFF))
text(rx + 20, 182, 'LED ring', 12, 700, OFF, 'middle'); text(rx + 20, 198, '12 RGBW', 9.5, fill=MUTE, anchor='middle')
wire([(rx + 40, TOP), (rx + 40, 140)]); dot(rx + 40, TOP, RED); text(rx + 46, 134, 'PWR', 9, 700, OFF)
wire([(rx + 20, 230), (rx + 20, 288)]); text(rx + 26, 246, 'GND', 9, 700, OFF)
text(rx + 82, 190, 'DOUT', 9, 700, MUTE); text(rx + 82, 203, 'unused', 9, fill=MUTE)
npn(rx + 20, 318, 'Q1', 'PN2222A')
wire([(rx + 20, 348), (rx + 20, BOT)]); dot(rx + 20, BOT)
wire([(205, 470), (712, 470), (712, 318), (722, 318)]); res(742, 318, False, 'R1', '470 Ω'); wire([(762, 318), (798, 318)]); dot(772, 318)
wire([(772, 318), (772, 380)]); res(772, 400, True, 'R2', '10 kΩ'); wire([(772, 420), (772, BOT)]); dot(772, BOT)
wire([(205, 530), (690, 530), (690, 185), (698, 185)]); res(718, 185, False, 'R5', '330 Ω'); wire([(738, 185), (760, 185)]); text(752, 202, 'DIN', 9, 700, OFF, 'middle')
text(rx + 44, 346, 'ring switch', 9.5, fill=MUTE); text(mx + 24, 282, 'motor switch', 9.5, fill=MUTE)
# capacitor across the rails
cx = 920
wire([(cx, TOP), (cx, 330)]); dot(cx, TOP, RED)
wire([(cx - 16, 330), (cx + 16, 330)], OFF, 2.6)
A("<path d='M%d 346 Q%d 338 %d 346' fill='none' stroke='%s' stroke-width='2.6'/>" % (cx - 16, cx, cx + 16, OFF))
wire([(cx, 342), (cx, BOT)]); dot(cx, BOT)
text(cx + 22, 326, '+', 12, 700, OFF, 'middle'); text(cx - 22, 368, '1000 µF', 10, 600, OFF, 'end'); text(cx - 22, 382, '6.3 V or more', 9, fill=MUTE, anchor='end')
A('</svg>')
open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(IMG, 'schematic.svg'), 'w').write('\n'.join(o) + '\n')
