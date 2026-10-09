// The ring and the motor as hardware. ring.h works out the light; this tab puts it on the pixels, switches the
// ring's power and runs the motor. A small task does that about 60 times a second, so the light keeps moving while
// loop() waits on the network. The task and loop() take turns at the animation: everything here that changes it
// holds a RingTurn while it does.
//
// The ring's power is on only while something is lit. Its transistor switches the ground side, so while it is off
// the data pin must not be driven low (the pixels would find a ground through it): the pixel driver is created
// when the power goes on and deleted when it goes off, which releases the pin.
#ifndef PEBBLE_OUTPUT_H
#define PEBBLE_OUTPUT_H
#include <Adafruit_NeoPixel.h>
#include "esp_timer.h"
#include "config.h"
#include "power.h"
#include "pulse.h"
#include "input.h"
#include "ring.h"

SemaphoreHandle_t outLock = nullptr;
Adafruit_NeoPixel* pixels = nullptr;    // exists only while the ring has power
volatile bool outPlaying = false;       // a message is playing: the comet is not following button 1
volatile unsigned long buzzUntil = 0;   // the motor runs until this millis()
bool motorOn = false;
uint32_t outOwnRgb = COLOR_RGB[0];      // this device's color
bool outPressShown = false;             // the comet is lit for a press of button 1
bool outHoldShown = false;              // the meter is showing a hold of button 2, or the setup page
Pulse outPulse;                         // the message being played

double outputNow() { return esp_timer_get_time() / 1e6; }
bool buzzing() { return (long)(buzzUntil - millis()) > 0; }

struct RingTurn {   // hold one of these to change the animation: it waits for the task, and brings the ring up to now
  RingTurn() { xSemaphoreTake(outLock, portMAX_DELAY); ringAdvance(outputNow()); }
  ~RingTurn() { xSemaphoreGive(outLock); }
};

void motorRun(bool on) {   // gently: the pin is switched fast, so the motor gets only part of the power
  if (on == motorOn) return;
  motorOn = on;
  analogWrite(MOTOR, on ? MOTOR_STRENGTH : 0);
}

void ringPower(bool on) {
  if (on == (pixels != nullptr)) return;
  if (on) {
    pixels = new Adafruit_NeoPixel(RING_PIXELS, RING_DATA, NEO_GRBW + NEO_KHZ800);   // four-channel pixels
    pixels->begin();
    digitalWrite(RING_PWR, HIGH);
    delay(RING_POWER_MS);
  } else {
    pixels->clear();
    pixels->show();
    digitalWrite(RING_PWR, LOW);
    delete pixels;   // also releases the data pin
    pixels = nullptr;
  }
}

// The comet follows button 1 itself, straight from the pin, so it answers a press even while loop() is busy.
// The pin must read the same twice running, which skips switch bounce.
void outputFollow() {
  static bool last = false, steady = false;
  bool raw = pinsDown(0);
  if (raw == last) steady = raw;
  last = raw;
  bool down = steady || simHeld;
  if (outPlaying || down == outPressShown) return;
  if (down) ringSetColor(outOwnRgb);
  ringPress(down);
  outPressShown = down;
}

void outputTask(void*) {
  static uint8_t frame[RING_PIXELS][4];
  for (;;) {
    xSemaphoreTake(outLock, portMAX_DELAY);
    ringAdvance(outputNow());
    outputFollow();
    bool lit = ringActive();
    motorRun((ring.play && ring.down) || buzzing());   // a message buzzes with each of its presses
    ringPower(lit);
    if (lit) {
      ringRender(frame);
      for (int i = 0; i < RING_PIXELS; i++) pixels->setPixelColor(i, frame[i][0], frame[i][1], frame[i][2], frame[i][3]);
      pixels->show();
    }
    xSemaphoreGive(outLock);
    delay(FRAME_MS);
  }
}

void outputBegin() {
  static bool begun = false;
  if (begun) return;
  begun = true;
  pinMode(RING_PWR, OUTPUT);
  digitalWrite(RING_PWR, LOW);
  analogWriteFrequency(MOTOR, MOTOR_PWM_HZ);
  analogWrite(MOTOR, 0);
  int c = colorIndex(deviceColor);
  outOwnRgb = COLOR_RGB[c < 0 ? 0 : c];
  ring.clock = outputNow();
  outLock = xSemaphoreCreateMutex();
  xTaskCreate(outputTask, "output", 4096, nullptr, 2, nullptr);   // above loop(), so a busy loop() cannot stall the light
}

// Call before sleeping: dark and still. The turn is never given back, so the task does nothing more.
void outputStop() {
  xSemaphoreTake(outLock, portMAX_DELAY);
  motorRun(false);
  ringPower(false);
}

// A device that was switched off woke on button 2. It turns on only if the button is held for ON_HOLD_MS (the ring
// fills green meanwhile, the same meter as the other holds); otherwise it goes straight back to sleep. A restart or power cut is not
// "off" (power.h), so it is also the way out when the button is not wired. Returns after the button is released.
void outputOnGate() {
  if (!powerIsOff) return;
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  outputBegin();
  unsigned long from = millis() - WAKE_LEAD_MS, held = 0;
  while (digitalRead(BTN2) == LOW && (held = millis() - from) < ON_HOLD_MS) {
    powerTouch();
    { RingTurn turn; if (held >= HOLD_SHOW_MS) ringMeter(RGB_ON, RING_PIXELS * (float)held / ON_HOLD_MS, 1); }
    delay(10);
  }
  if (held < ON_HOLD_MS) {   // let go too soon
    { RingTurn turn; ringMeterStop(); }
    delay(300);              // the fade
    outputStop();
    powerSleep(0);
  }
  powerIsOff = false;
  sayln("On");
  { RingTurn turn; ringMeterStop(); }
  while (digitalRead(BTN2) == LOW) { powerTouch(); delay(10); }   // the hold is spent: it is not the start of a setup hold
  delay(DEBOUNCE_MS * 2);
}

bool outputActive() {   // something is lit, playing or buzzing
  RingTurn turn;
  return ringActive() || outPlaying || buzzing();
}

void buzz(unsigned long ms) { buzzUntil = millis() + ms; }

// Call every loop(). `working` = joining Wi-Fi after switching on: the cyan chase. Button 2's hold is shown here too: the
// ring fills purple up to 5 s (release for setup), then counts down in red to 10 s (dark: release to turn off).
void outputTick(bool working) {
  RingTurn turn;
  int c = colorIndex(deviceColor);
  outOwnRgb = COLOR_RGB[c < 0 ? 0 : c];
  if (working) ringBusy(RGB_WORKING);
  else ringBusyStop();

  unsigned long held = button[1].down ? millis() - button[1].at : 0;
  unsigned long redFrom = SETUP_HOLD_MS + SETUP_SHOWN_MS;
  bool gauge = ring.gaugeMs >= 0;   // the battery level is showing: leave it be
  if (held >= OFF_HOLD_MS) ringMeterStop();
  else if (held >= redFrom) ringMeter(RGB_OFF, RING_PIXELS * (float)(OFF_HOLD_MS - held) / (OFF_HOLD_MS - redFrom), 1);
  else if (held >= SETUP_HOLD_MS) ringMeter(RGB_SETUP, RING_PIXELS, 1);
  else if (held >= HOLD_SHOW_MS) ringMeter(RGB_SETUP, RING_PIXELS * (float)held / SETUP_HOLD_MS, 1);
  else if (portalOn && !gauge) ringMeter(RGB_SETUP, RING_PIXELS, SETUP_GLOW);
  else if (outHoldShown && !gauge) ringMeterStop();
  outHoldShown = held >= HOLD_SHOW_MS || portalOn;
}

// ---- What the device shows. Each returns at once, except showPlay(). ----
void showReceived() { { RingTurn turn; ringSweep(RGB_RECEIVED); } buzz(BUZZ_SHORT_MS); }   // green, with a short buzz
void showFailed() { { RingTurn turn; ringFail(); } buzz(BUZZ_SHORT_MS); }               // red, with a short buzz
void showWaiting(uint32_t rgb) { RingTurn turn; ringBlip(rgb, 0); }                 // a message is waiting: its sender's color, at the top
void showLow() { RingTurn turn; ringBlip(RGB_LOW, RING_PIXELS / 2); }               // battery low: amber, at the bottom
void showBattery(int percent) {   // 1 to 12 pixels
  int lit = (percent * RING_PIXELS + 99) / 100;
  RingTurn turn;
  ringGauge(percent < BATT_LOW_PERCENT ? RGB_LOW : RGB_BATTERY, lit < 1 ? 1 : lit > RING_PIXELS ? RING_PIXELS : lit);
}
void showSetup(bool on) {   // the setup page is opening (it takes a few seconds to appear) or closing
  RingTurn turn;
  if (on) ringMeter(RGB_SETUP, RING_PIXELS, SETUP_GLOW);
  else ringMeterStop();
}

bool cometIdle() { RingTurn turn; return !ring.play && !ring.down && ring.level == 0; }
void cometWait() { while (!cometIdle()) { powerTouch(); delay(10); } }

// Play a message: the comet in its sender's color, the motor buzzing with each press. Returns when it has finished.
// Nothing pressed while it plays counts for anything.
void showPlay(const Pulse& p) {
  {
    RingTurn turn;
    outPlaying = true;
    outPressShown = false;
    ringPress(false);
  }
  cometWait();   // let a press that was showing fade first
  {
    RingTurn turn;
    outPulse = p;
    ringSetColor(COLOR_RGB[p.color]);
    ringPlay(outPulse.ms, outPulse.count);
  }
  cometWait();
  delay(PLAY_GAP_MS);
  outPlaying = false;
  inputSkip();
}

#endif
