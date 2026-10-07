// The pulse message format: one line of text, the same everywhere (device to device, phone to device).
//   pink 200 200 500
// A color name, then times in milliseconds that alternate press, gap, press... and end on a press.
#ifndef PEBBLE_PULSE_H
#define PEBBLE_PULSE_H
#include "config.h"

const int PULSE_MAX_TIMES = 2 * MAX_PRESSES - 1;   // presses plus the gaps between them
const unsigned long PULSE_MIN_MS = 20;             // shorter than this is switch bounce, not a press
const unsigned long PULSE_MAX_MS = 5000;           // one press or gap

struct Pulse {
  int color;                       // index into COLOR_NAMES
  int count;                       // how many times follow
  uint16_t ms[PULSE_MAX_TIMES];    // press, gap, press, ...
};

// True if the text starts with a color name: it is meant as a pulse message, valid or not.
bool pulseLooksLike(const String& text) {
  int sp = text.indexOf(' ');
  return colorIndex(sp < 0 ? text : text.substring(0, sp)) >= 0;
}

// Read a message into `p`. Returns nullptr if it is valid, otherwise a short reason.
const char* pulseParse(const String& text, Pulse& p) {
  int pos = text.indexOf(' ');
  p.color = colorIndex(pos < 0 ? text : text.substring(0, pos));
  p.count = 0;
  if (p.color < 0) return "unknown color";
  unsigned long total = 0;
  while (pos >= 0 && pos < (int)text.length()) {
    while (pos < (int)text.length() && text[pos] == ' ') pos++;   // any run of spaces
    if (pos >= (int)text.length()) break;
    int end = text.indexOf(' ', pos);
    if (end < 0) end = text.length();
    unsigned long value = 0;
    for (int i = pos; i < end; i++) {
      if (!isdigit((unsigned char)text[i])) return "times must be numbers";
      value = value * 10 + (text[i] - '0');
      if (value > PULSE_MAX_MS) return "a time is out of range";   // also stops a long number overflowing
    }
    if (value < PULSE_MIN_MS) return "a time is out of range";
    if (p.count == PULSE_MAX_TIMES) return "too many presses";
    p.ms[p.count++] = value;
    total += value;
    pos = end;
  }
  if (p.count == 0) return "no times";
  if (p.count % 2 == 0) return "must end with a press";
  if (total > MAX_RECORD_MS) return "too long";
  return nullptr;
}

// The message as text, in its one standard form: lower-case color, single spaces.
String pulseText(const Pulse& p) {
  String out = COLOR_NAMES[p.color];
  out.toLowerCase();
  for (int i = 0; i < p.count; i++) out += " " + String(p.ms[i]);
  return out;
}

#endif
