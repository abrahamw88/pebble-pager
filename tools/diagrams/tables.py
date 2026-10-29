"""Prints the part and wire tables for step 10 of docs/build-steps.md, from layout.py. Run: python3 tables.py"""
import sys
import os
HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, '..', '..', 'docs', 'img')
sys.path.insert(0, HERE)
from layout import *
LEG = {'E': 'emitter', 'B': 'base', 'C': 'collector'}
def hs(h): return '%d, %d' % h
def lead_(h):
    if h in PINS:
        net, leg, part, side = PINS[h]
        if part == 'XIAO': return 'XIAO %s pin (%s)' % (leg, hs(h))
        if part in ('Q1', 'Q2'): return '%s %s (%s)' % (part, LEG[leg], hs(h))
        return '%s (%s)' % (part.lower() if part.startswith('Button') else part, hs(h))
    for (n, v, a, b, na, nb) in PARTS:
        if h in (a, b):
            if n == 'D1': return 'diode, %s end (%s)' % ('plain' if h == a else 'striped', hs(h))
            return '%s (%s)' % (n, hs(h))
def lead(h):
    s = lead_(h); return s[0].upper() + s[1:]
NET = {'GND': 'Ground', '3V3': '3V3', 'D1': 'D1', 'D2': 'D2', 'D3': 'D3', 'D4': 'D4', 'D5': 'D5', 'D10': 'D10', 'BASE1': 'Q1 base', 'BASE2': 'Q2 base', 'MOT-': 'Motor −'}
out = []
out.append('| Part | Holes (column, row) |\n|---|---|')
out.append('| XIAO ESP32-C3 (back) | Left pins in column 5, rows 14 to 20: D6, D5, D4, D3, D2, D1, D0. Right pins in column 11, rows 14 to 20: D7, D8, D9, D10, 3V3, GND, 5V. USB end at the bottom edge |')
out.append('| Button 1 (front) | 6, 6 and 9, 6; 6, 8 and 9, 8. Ground is lapped onto 6, 6 and D2 onto 9, 8, diagonally opposite |')
out.append('| Button 2 (back) | 1, 18 and 4, 18; 1, 20 and 4, 20. D3 is lapped onto 4, 18 and ground onto 1, 20, diagonally opposite |')
out.append('| Q1, ring switch (back, standing, flat face toward the left edge) | Collector 1, 14; base 1, 15; emitter 1, 16 |')
out.append('| Q2, motor switch (back, standing, flat face toward the XIAO) | Collector 14, 14; base 14, 15; emitter 14, 16 |')
for (n, v, a, b, na, nb) in PARTS:
    if n == 'D1':
        out.append('| 1N4148 diode (back, lying flat) | Plain end 12, 15; striped end 12, 18, toward 3V3 |')
    else:
        out.append('| %s, %s, %s (front) | %s to %s |' % (n, v, PART_USE[n], hs(a), hs(b)))
out.append('')
out.append('| Wire | From | To |\n|---|---|---|')
for w in WIRES:
    a, b = ends(w)
    if w[0] in ('A', 'B', 'C', 'D'):
        ps = passes(w)
        via = ', up through %s to the XIAO pin' % hs(ps[0]) if ps else ''
        out.append('| %s, wire %s (back%s) | %s | %s |' % (NET[w[1]], w[0], via, lead(a), lead(b)))
    else:
        out.append('| %s | %s | %s |' % (NET[w[1]], lead(a), lead(b)))
out.append('')
out.append('| Off the board | Lapped onto |\n|---|---|')
for (k, what, net, h, at) in OFFBOARD:
    note = '; the capacitor\'s + lead also solders onto this ring pad' if k == 2 else ''
    out.append('| %d. %s | %s%s |' % (k, what, lead(h), note))
out.append('| BAT+ sense wire, from the XIAO\'s BAT+ pad, under the XIAO and round the bottom edge | R6 (9, 19) |')
out.append('| Battery + and − | The XIAO\'s BAT+ and BAT− pads, underneath |')
print('\n'.join(out))
