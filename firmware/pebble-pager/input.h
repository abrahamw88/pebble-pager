// Buttons. Button 1 (large): a tap plays the next message; a hold of half a second starts a recording and is its
// first press, and the message is sent 2 s after the last press. (If unread messages are waiting, the main tab
// plays them first and calls inputArm(): the recording then starts fresh, without that hold.) Button 2 (small): a tap shows the battery, a 5 s hold
// opens or closes the setup page, a 10 s hold turns the device off; holds act on release.
//
// Every press and release is caught by an interrupt and stamped with the time, so press lengths stay exact even
// while loop() is busy with a network request. All the logic works from those stamps.
#ifndef PEBBLE_INPUT_H
#define PEBBLE_INPUT_H
#include "config.h"
#include "pulse.h"

enum { EV_NONE, EV_PLAY, EV_RECORD_START, EV_RECORD_DONE, EV_RECORD_CANCEL, EV_BATTERY, EV_SETUP, EV_OFF };
Pulse recorded;   // the message, when inputTick() returns EV_RECORD_DONE

struct Edge { uint8_t button; bool down; unsigned long at; };   // button 0 or 1 went down or up at this time

// ---- Where edges come from: the pins (by interrupt), and the console's "press" command (for testing) ----
struct Source { uint8_t pin; uint8_t button; };
Source sources[] = {{BTN1, 0}, {BTN2, 1}, {BOOT_BTN, 1}};   // the onboard button's role can be changed with inputBootButton()
const int EDGE_QUEUE = 64;
volatile Edge edgeQueue[EDGE_QUEUE];
volatile int edgeHead = 0, edgeTail = 0;
Edge simQueue[2 * MAX_PRESSES + 8];   // simulated edges, each with the time it is due
int simCount = 0, simNext = 0;

void IRAM_ATTR edgeIsr(void* arg) {
  Source* s = (Source*)arg;
  int next = (edgeHead + 1) % EDGE_QUEUE;
  if (next == edgeTail) return;   // full: drop it; the level check in inputTick() catches up
  edgeQueue[edgeHead].button = s->button;
  edgeQueue[edgeHead].down = digitalRead(s->pin) == LOW;
  edgeQueue[edgeHead].at = millis();
  edgeHead = next;
}

struct Button { bool down; unsigned long at; };   // the settled state, and when it last changed
Button button[2];
enum { R_IDLE, R_PRESSED, R_ARMED, R_WAIT, R_DOWN };   // button 1: first press, not yet a hold / armed, waiting for release / between presses / in a press
int recState = R_IDLE, recPresses = 0, holdTold = EV_NONE;
unsigned long recStart = 0, recLastUp = 0, recQuietUntil = 0;

// A button already down at start-up is the press that woke the device: it began before the chip was running,
// so count it from WAKE_LEAD_MS ago. That way a hold from sleep is the first press of a message, like any other.
void inputBegin() {
  for (Source& s : sources) {
    pinMode(s.pin, INPUT_PULLUP);
    attachInterruptArg(s.pin, edgeIsr, &s, CHANGE);
    if (digitalRead(s.pin) == LOW) button[s.button] = {true, millis() - WAKE_LEAD_MS};
  }
  if (button[0].down) recState = R_PRESSED;
}

void inputBootButton(int button) { sources[2].button = button; }   // 0 = act as button 1, 1 = act as button 2

bool pinsDown(int button) {   // is any pin of this button held right now?
  for (const Source& s : sources)
    if (s.button == button && digitalRead(s.pin) == LOW) return true;
  return false;
}

// Simulate presses on a button: times in ms that alternate down, up, down... e.g. "700 300 600" = hold, gap, press.
void inputSimulate(int button, const String& times) {
  simCount = simNext = 0;
  unsigned long at = millis() + 100;
  bool down = true;
  for (int pos = 0; pos < (int)times.length() && simCount < (int)(sizeof(simQueue) / sizeof(simQueue[0])) - 1;) {
    while (pos < (int)times.length() && times[pos] == ' ') pos++;
    int end = times.indexOf(' ', pos);
    if (end < 0) end = times.length();
    if (end == pos) break;
    simQueue[simCount++] = {(uint8_t)button, down, at};
    at += strtoul(times.substring(pos, end).c_str(), nullptr, 10);
    down = !down;
    pos = end;
  }
  if (simCount && !down) simQueue[simCount++] = {(uint8_t)button, false, at};   // always end released
}

bool nextEdge(Edge& e) {
  noInterrupts();
  bool have = edgeTail != edgeHead;
  if (have) {
    e.button = edgeQueue[edgeTail].button;
    e.down = edgeQueue[edgeTail].down;
    e.at = edgeQueue[edgeTail].at;
    edgeTail = (edgeTail + 1) % EDGE_QUEUE;
  }
  interrupts();
  if (have) return true;
  if (simNext < simCount && (long)(millis() - simQueue[simNext].at) >= 0) { e = simQueue[simNext++]; return true; }
  return false;
}

// ---- The logic ----
bool inputBusy() { return recState != R_IDLE || button[1].down; }   // someone is using the buttons: keep loop() free

unsigned long pulseClamp(unsigned long ms) { return ms < PULSE_MIN_MS ? PULSE_MIN_MS : ms > PULSE_MAX_MS ? PULSE_MAX_MS : ms; }

// End the recording. `cutOff` = it hit a limit while the user may still be pressing: ignore the button briefly,
// so the presses that spill over are not taken as taps.
int recordFinish(unsigned long at, bool cutOff) {
  recState = R_IDLE;
  if (cutOff) recQuietUntil = (at + SEND_PAUSE_MS) | 1;
  if (recPresses == 0) return EV_RECORD_CANCEL;
  int color = colorIndex(deviceColor);
  recorded.color = color < 0 ? 0 : color;
  recorded.count = 2 * recPresses - 1;
  return EV_RECORD_DONE;
}

int recordPressEnd(unsigned long at, unsigned long held) {   // a press of `held` ms ends at `at`
  recorded.ms[2 * recPresses] = pulseClamp(held);
  recPresses++;
  recLastUp = at;
  recState = R_WAIT;
  return recPresses == MAX_PRESSES ? recordFinish(at, true) : EV_NONE;
}

int onEdge(const Edge& e) {
  Button& b = button[e.button];
  if (e.down == b.down || e.at - b.at < DEBOUNCE_MS) return EV_NONE;   // no change, or switch bounce
  unsigned long held = e.at - b.at;   // for a release: how long it was down
  b = {e.down, e.at};

  if (e.button == 1) {   // button 2 acts on release, by how long it was held
    holdTold = EV_NONE;
    if (e.down) return EV_NONE;
    return held >= OFF_HOLD_MS ? EV_OFF : held >= SETUP_HOLD_MS ? EV_SETUP : EV_BATTERY;
  }

  if (recQuietUntil && (long)(e.at - recQuietUntil) < 0) return EV_NONE;
  recQuietUntil = 0;
  if (e.down) {
    if (recState == R_IDLE) recState = R_PRESSED;
    else if (recState == R_WAIT) {
      if (recPresses == 0) recStart = e.at;
      else recorded.ms[2 * recPresses - 1] = pulseClamp(e.at - recLastUp);   // the gap since the last press
      recState = R_DOWN;
    }
    return EV_NONE;
  }
  switch (recState) {
    case R_PRESSED:   // released before loop() noticed the hold: decide by how long it was down
      if (held < RECORD_HOLD_MS) { recState = R_IDLE; return EV_PLAY; }
      recPresses = 0; recStart = e.at - held;
      recordPressEnd(e.at, held);   // the hold is the first press
      return EV_RECORD_START;
    case R_ARMED: recLastUp = e.at; recState = R_WAIT; return EV_NONE;
    case R_DOWN: return recordPressEnd(e.at, held);
  }
  return EV_NONE;
}

// Start the recording over with no presses yet: the hold that began it is not part of the message.
void inputArm() {
  recPresses = 0;
  recLastUp = millis();
  recState = button[0].down ? R_ARMED : R_WAIT;
}

// Call every loop() until it returns EV_NONE. Returns one thing the user did.
int inputTick() {
  Edge e;
  if (nextEdge(e)) return onEdge(e);
  unsigned long now = millis();

  // A press shorter than the bounce filter, or a dropped interrupt, can leave a button's state wrong: follow the pin.
  if (simNext >= simCount)
    for (int i = 0; i < 2; i++)
      if (pinsDown(i) != button[i].down && now - button[i].at >= DEBOUNCE_MS) return onEdge({(uint8_t)i, !button[i].down, now});

  if (recState == R_PRESSED && now - button[0].at >= RECORD_HOLD_MS) {   // a hold: recording, and this press is its first
    recPresses = 0; recStart = button[0].at; recState = R_DOWN;
    return EV_RECORD_START;
  }
  if (recState == R_WAIT && now - recLastUp >= SEND_PAUSE_MS) return recordFinish(now, false);
  bool started = recPresses > 0 || recState == R_DOWN;
  if ((recState == R_WAIT || recState == R_DOWN) && started && now - recStart >= MAX_RECORD_MS) {   // out of time
    unsigned long limit = recStart + MAX_RECORD_MS;
    if (recState == R_DOWN && limit - button[0].at >= PULSE_MIN_MS) recordPressEnd(limit, limit - button[0].at);   // keep the press, cut short
    return recordFinish(now, true);
  }

  if (button[1].down) {   // say what releasing now would do
    unsigned long held = now - button[1].at;
    int would = held >= OFF_HOLD_MS ? EV_OFF : held >= SETUP_HOLD_MS ? EV_SETUP : EV_NONE;
    if (would != holdTold) { holdTold = would; sayln(would == EV_OFF ? "Release to turn off" : "Release for setup"); }
  }
  return EV_NONE;
}

#endif
