// Ring animation engine. Pure math, no hardware: it turns "the button went down / up" into 12 RGBW pixel values,
// 200 times a second. The same code plays a recording live and plays a received message back, so both devices
// show the same thing. docs/ring-engine.js is a line-for-line JavaScript copy of this file for the browser pages;
// keep the two in step (the parity check in AGENTS.md compares their output).
//
// The comet: a head in the sender's color with a tail that melts into white, gliding round the ring between
// pixels. A press fades it in while the head leaps a little ahead and springs back, stretching the tail; it orbits
// while the button is held, picking up speed; a release makes it recoil, coast and fade out towards white. The
// brightness itself only fades smoothly: the playfulness is all in the motion. It carries on from where it stopped.
//
// The status lights are plainer on purpose, so they are never mistaken for a message: a sweep (one soft lap:
// message sent and received), a chase (two dots circling: working), and a meter (pixels lit from the top, clockwise: battery level,
// and button 2's holds). They are added on top of the comet, and all share the power budget.
#ifndef PEBBLE_RING_H
#define PEBBLE_RING_H
#include <math.h>
#include <stdint.h>

const int RING_PIXELS = 12;
const int RING_STEP_MS = 5;         // the simulation always advances in these steps, whatever the frame rate
const float RING_STEP_S = RING_STEP_MS / 1000.0f;

struct RingTuning {
  float speed = 0.9f;        // laps per second when a press starts
  float accel = 0.2f;        // laps per second gained for each second held
  float maxSpeed = 1.9f;     // laps per second, top speed
  float tail = 5.5f;         // tail length in pixels at full brightness
  float tailPower = 1.0f;    // how fast the tail dims along its length (1 = evenly; higher = thinner, dimmer tail)
  float attackHz = 2.6f;     // fade-in quickness (about 0.5 s)
  float releaseHz = 1.5f;    // fade-out quickness (about 0.5 s)
  float leap = 1.2f;         // pixels the head jumps ahead on a press before springing back
  float recoil = 1.0f;       // pixels the head kicks back on a release
  float bounceHz = 3.0f;     // how fast that spring wobbles...
  float bounceDamp = 0.35f;  // ...and how soon it settles (1 = no wobble, lower = more)
  float stretch = 1.0f;      // how much the tail stretches with a leap and squashes with a recoil
  float pastel = 0.2f;       // how much of the color's own white stays in the head (0 = pure hue, 1 = the full pastel)
  float whiteGain = 1.0f;    // how bright the white tail is next to the colored head
  float peak = 1.4f;         // brightness; above 1 the head is already at full, so more of the comet reaches full
  float budgetMa = 180.0f;   // the ring may draw at most this; a frame over it is dimmed as a whole
  float meter = 0.6f;        // brightness of the meter, which can light the whole ring, next to the comet
};
RingTuning ringTune;

struct RingState {
  bool down = false;             // the comet's "key" is held
  float level = 0, vel = 0;      // brightness, 0 to 1, and its rate of change
  float hop = 0, hopVel = 0;     // the head's springy offset from its path, in pixels, and its rate of change
  float angle = 0;               // laps travelled; the head is at the fractional part
  float held = 0, speed = 0;     // seconds this press has lasted; current laps per second
  float color[4] = {0, 0, 0, 0}; // the sender's color as red, green, blue, white (0 to 1)
  float failT = -1;              // seconds into the red "not received" pulse, or -1
  float blipT = -1;              // seconds into the "message waiting" blip, or -1
  float blipColor[4] = {0, 0, 0, 0};   // the color of that blip: the waiting message's sender, not this device
  int blipAt = 0;                // the pixel it breathes on
  float sweepT = -1;             // seconds into a sweep, or -1
  float sweepColor[4] = {0, 0, 0, 0};
  bool busy = false;             // the chase is wanted
  float busyLevel = 0, busyAngle = 0;   // its brightness, 0 to 1, and how far round it is, 0 to 1
  float busyColor[4] = {0, 0, 0, 0};
  float meterFill = 0;           // the meter: this many pixels are lit (a fraction lights the next one partly)
  float meterTarget = 0, meterLevel = 0;   // its brightness, 0 to 1: wanted, and now
  float meterColor[4] = {0, 0, 0, 0};
  int gaugeMs = -1;              // ms into a gauge (the meter fills, holds and fades by itself), or -1
  int gaugePixels = 0;
  const uint16_t* play = nullptr;   // a message being played back: press, gap, press... in ms
  int playCount = 0, playIndex = 0;
  int playLeftMs = 0;               // what is left of the current press or gap (whole ms, so every device agrees)
  double clock = 0;                 // seconds simulated so far
};
RingState ring;

// A pastel is a strong hue plus white, and the ring has a white LED. Split the color into the two: the hue at full
// strength on the colored LEDs, and a share of its white on the white LED. Keeping only part of the white makes
// the head read clearly as the color, next to the white tail.
void ringSplit(uint32_t rgb, float out[4]) {
  float r = ((rgb >> 16) & 255) / 255.0f, g = ((rgb >> 8) & 255) / 255.0f, b = (rgb & 255) / 255.0f;
  float w = fminf(r, fminf(g, b)), top = fmaxf(r, fmaxf(g, b)) - w;
  if (top < 0.001f) { out[0] = out[1] = out[2] = 0; out[3] = 1; return; }   // white or grey
  out[0] = (r - w) / top; out[1] = (g - w) / top; out[2] = (b - w) / top;
  out[3] = w * ringTune.pastel;
}

void ringSetColor(uint32_t rgb) { ringSplit(rgb, ring.color); }   // the color of the comet: whoever sent the message

void ringPress(bool down) {   // the button went down or up
  if (down == ring.down) return;
  ring.down = down;
  float kick = 6.2831853f * ringTune.bounceHz * 1.4f;   // the push that makes the spring peak at about one pixel
  if (down) { ring.held = 0; ring.speed = ringTune.speed; ring.hopVel += ringTune.leap * kick; }
  else ring.hopVel -= ringTune.recoil * kick;
}

void ringPlay(const uint16_t* ms, int count) {   // play a message: the times alternate press, gap, press...
  ring.play = ms; ring.playCount = count; ring.playIndex = 0;
  ring.playLeftMs = count > 0 ? ms[0] : 0;
  if (count > 0) ringPress(true);
}

void ringFail() { ring.failT = 0; }   // red pulse: the message was not received
void ringBlip(uint32_t rgb, int at = 0) { ringSplit(rgb, ring.blipColor); ring.blipAt = at; ring.blipT = 0; }   // one soft pixel breathes once
void ringSweep(uint32_t rgb) { ringSplit(rgb, ring.sweepColor); ring.sweepT = 0; }   // one soft lap of light
void ringBusy(uint32_t rgb) { ringSplit(rgb, ring.busyColor); ring.busy = true; }    // the chase, until ringBusyStop()
void ringBusyStop() { ring.busy = false; }

// The meter: `fill` pixels lit from the top, clockwise, at brightness `level`. It stays until changed or stopped.
void ringMeter(uint32_t rgb, float fill, float level) {
  ringSplit(rgb, ring.meterColor);
  ring.meterFill = fill; ring.meterTarget = level; ring.gaugeMs = -1;
}
void ringMeterStop() { ring.meterTarget = 0; ring.gaugeMs = -1; }
void ringGauge(uint32_t rgb, int pixels) {   // the meter fills to `pixels`, holds and fades by itself
  ringSplit(rgb, ring.meterColor);
  ring.gaugePixels = pixels; ring.gaugeMs = 0; ring.meterFill = 0; ring.meterTarget = 1;
}

bool ringActive() {   // false when everything is dark: the ring's power can be switched off
  return ring.down || ring.level > 0 || ring.failT >= 0 || ring.blipT >= 0 || ring.play ||
         ring.sweepT >= 0 || ring.busy || ring.busyLevel > 0 || ring.meterTarget > 0 || ring.meterLevel > 0;
}

float ringSmooth(float a, float b, float x) {   // 0 below a, 1 above b, an S-curve between
  float t = (x - a) / (b - a);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}

void ringStep() {   // advance the simulation by RING_STEP_S
  const float dt = RING_STEP_S, twoPi = 6.2831853f;
  ring.clock += dt;

  if (ring.play) {   // playback presses the key for us
    ring.playLeftMs -= RING_STEP_MS;
    while (ring.play && ring.playLeftMs <= 0) {
      ring.playIndex++;
      if (ring.playIndex >= ring.playCount) { ring.play = nullptr; ringPress(false); break; }
      ring.playLeftMs += ring.play[ring.playIndex];
      ringPress(ring.playIndex % 2 == 0);
    }
  }

  // Brightness eases towards on or off, with no overshoot.
  float w = twoPi * (ring.down ? ringTune.attackHz : ringTune.releaseHz);
  float target = ring.down ? 1.0f : 0.0f;
  ring.vel += (w * w * (target - ring.level) - 2 * w * ring.vel) * dt;
  ring.level += ring.vel * dt;
  if (ring.level > 1) ring.level = 1;
  if (ring.level < 0 || (!ring.down && ring.level < 0.003f && ring.vel <= 0)) { ring.level = 0; ring.vel = 0; ring.hop = 0; ring.hopVel = 0; }

  // The head's offset is a spring that presses and releases kick: it wobbles around the path and settles.
  float wb = twoPi * ringTune.bounceHz;
  ring.hopVel += (-wb * wb * ring.hop - 2 * ringTune.bounceDamp * wb * ring.hopVel) * dt;
  ring.hop += ring.hopVel * dt;

  // It orbits while held, a little faster the longer the hold, and coasts to a stop as it fades.
  if (ring.down) {
    ring.held += dt;
    ring.speed = fminf(ringTune.maxSpeed, ringTune.speed + ringTune.accel * ring.held);
    ring.angle += ring.speed * dt;
  } else {
    ring.angle += ring.speed * fminf(ring.level, 1.0f) * dt;
  }
  if (ring.angle >= 1024) ring.angle -= 1024;

  if (ring.failT >= 0) { ring.failT += dt; if (ring.failT >= 1.35f) ring.failT = -1; }
  if (ring.blipT >= 0) { ring.blipT += dt; if (ring.blipT >= 0.9f) ring.blipT = -1; }
  if (ring.sweepT >= 0) { ring.sweepT += dt; if (ring.sweepT >= 0.8f) ring.sweepT = -1; }

  // The chase fades in and out, and circles while it shows.
  ring.busyLevel += ((ring.busy ? 1.0f : 0.0f) - ring.busyLevel) * 6.0f * dt;
  if (!ring.busy && ring.busyLevel < 0.003f) ring.busyLevel = 0;
  if (ring.busyLevel > 0) { ring.busyAngle += 0.8f * dt; if (ring.busyAngle >= 1) ring.busyAngle -= 1; }

  // The meter's brightness eases to what is wanted. A gauge drives the meter: fill, hold, fade.
  if (ring.gaugeMs >= 0) {
    ring.gaugeMs += RING_STEP_MS;
    ring.meterFill = ring.gaugePixels * ringSmooth(0.0f, 500.0f, ring.gaugeMs);
    if (ring.gaugeMs >= 2500) { ring.gaugeMs = -1; ring.meterTarget = 0; }
  }
  ring.meterLevel += (ring.meterTarget - ring.meterLevel) * 8.0f * dt;
  if (ring.meterTarget == 0 && ring.meterLevel < 0.003f) ring.meterLevel = 0;
}

void ringAdvance(double nowS) {   // bring the simulation up to `nowS` seconds
  if (nowS - ring.clock > 1.0) ring.clock = nowS - 1.0;   // after a long pause, don't replay the gap
  while (ring.clock + RING_STEP_S <= nowS) ringStep();
}

// Work out the 12 pixels: out[i] = red, green, blue, white, 0 to 255, ready to send. Pixel 0 is the top; the
// comet travels towards higher numbers. Returns the current the frame draws, in mA.
float ringRender(uint8_t out[RING_PIXELS][4]) {
  float px[RING_PIXELS][4];
  float level = ring.level;
  float head = (ring.angle - floorf(ring.angle)) * RING_PIXELS + ring.hop;   // where the head is, in pixels
  while (head < 0) head += RING_PIXELS;
  while (head >= RING_PIXELS) head -= RING_PIXELS;
  float tail = ringTune.tail * (0.35f + 0.65f * level) + ringTune.stretch * ring.hop;   // grows as it brightens; stretches with a leap
  if (tail < 1) tail = 1;
  float fading = ring.down ? 0 : 1 - fminf(level, 1.0f);             // 0 while held, towards 1 as it fades out
  for (int i = 0; i < RING_PIXELS; i++) {
    float behind = head - i;                                         // how far this pixel is behind the head
    if (behind < 0) behind += RING_PIXELS;
    float ahead = RING_PIXELS - behind;
    float bright = 0, white = 0;
    if (ahead < 1) bright = 1 - ahead;                               // the pixel the head is gliding onto
    else if (behind <= tail) {
      float along = behind / tail;                                   // 0 at the head, 1 at the tail's end
      bright = powf(1 - along, ringTune.tailPower);
      white = ringSmooth(0.12f, 0.7f, along);                        // color at the head, white down the tail
    }
    white += (1 - white) * fading * 0.85f;                           // the whole comet pales as it fades
    bright *= level * ringTune.peak;
    for (int c = 0; c < 3; c++) px[i][c] = ring.color[c] * (1 - white) * bright;
    px[i][3] = (ring.color[3] * (1 - white) + ringTune.whiteGain * white) * bright;
  }

  if (ring.failT >= 0) {   // two soft red pulses over the whole ring
    float t = ring.failT, pulse = 0;
    if (t < 0.6f) pulse = sinf(3.1415927f * t / 0.6f);
    else if (t >= 0.75f) pulse = sinf(3.1415927f * (t - 0.75f) / 0.6f);
    pulse = pulse * pulse * ringTune.peak;
    for (int i = 0; i < RING_PIXELS; i++) px[i][0] += 0.9f * pulse;
  }
  if (ring.blipT >= 0) {   // one pixel breathes once, with a faint glow either side
    float breath = sinf(3.1415927f * ring.blipT / 0.9f);
    breath = breath * breath * ringTune.peak * 0.8f;
    const int at[3] = {(ring.blipAt + RING_PIXELS - 1) % RING_PIXELS, ring.blipAt, (ring.blipAt + 1) % RING_PIXELS};
    const float share[3] = {0.25f, 1.0f, 0.25f};
    for (int k = 0; k < 3; k++)
      for (int c = 0; c < 4; c++) px[at[k]][c] += ring.blipColor[c] * breath * share[k];
  }
  if (ring.sweepT >= 0) {   // a soft band leaves the top, goes once round and fades as it comes back
    float t = ring.sweepT / 0.8f, at = t * RING_PIXELS, glow = sinf(3.1415927f * t);
    for (int i = 0; i < RING_PIXELS; i++) {
      float d = fabsf(i - at);
      if (d > RING_PIXELS / 2.0f) d = RING_PIXELS - d;
      if (d >= 2.5f) continue;
      for (int c = 0; c < 4; c++) px[i][c] += ring.sweepColor[c] * (1 - d / 2.5f) * glow;
    }
  }
  if (ring.busyLevel > 0) {   // two soft dots, opposite each other, circling
    for (int i = 0; i < RING_PIXELS; i++) {
      float d = fmodf(fabsf(i - ring.busyAngle * RING_PIXELS), RING_PIXELS / 2.0f);   // distance to the nearer dot
      if (d > RING_PIXELS / 4.0f) d = RING_PIXELS / 2.0f - d;
      if (d >= 1.5f) continue;
      for (int c = 0; c < 4; c++) px[i][c] += ring.busyColor[c] * (1 - d / 1.5f) * ring.busyLevel * 0.8f;
    }
  }
  if (ring.meterLevel > 0) {   // pixels lit from the top, clockwise; the last one partly, so the fill glides
    for (int i = 0; i < RING_PIXELS; i++) {
      float lit = ring.meterFill - i;
      lit = lit < 0 ? 0 : lit > 1 ? 1 : lit;
      for (int c = 0; c < 4; c++) px[i][c] += ring.meterColor[c] * lit * ring.meterLevel * ringTune.meter;
    }
  }

  // To LED values: the eye is not linear, so apply gamma; then keep the whole frame inside the power budget.
  float sum = 0;
  for (int i = 0; i < RING_PIXELS; i++)
    for (int c = 0; c < 4; c++) {
      float v = px[i][c] < 0 ? 0 : px[i][c] > 1 ? 1 : px[i][c];
      px[i][c] = powf(v, 2.2f);
      sum += px[i][c];
    }
  float idle = RING_PIXELS * 1.0f, draw = sum * 18.0f;   // about 1 mA per pixel just for being on, 18 mA per LED at full
  float scale = draw > ringTune.budgetMa - idle ? (ringTune.budgetMa - idle) / draw : 1.0f;
  for (int i = 0; i < RING_PIXELS; i++)
    for (int c = 0; c < 4; c++) out[i][c] = (uint8_t)(px[i][c] * scale * 255.0f + 0.5f);
  return idle + draw * scale;
}

#endif
