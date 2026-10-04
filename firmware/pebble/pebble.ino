// Pebble: full pager firmware. Skeleton only; behavior is specified in docs/build-spec.md.
//
// To build, in order:
//   1. Buttons: tap vs hold, recording, 2 s send pause
//   2. Ring: RGBW (NEO_GRBW) animations for every state, power switched on D5
//   3. Motor: buzz patterns
//   4. Wi-Fi: saved networks, setup mode ("Pebble-Setup"), 2.4 GHz only
//   5. ntfy: per-device topics, poll=1 check (plan B), "delivered" and "seen" receipts
//   6. Sleep: deep sleep with 30 s wake, away mode (5 min scan), off mode
//   7. Battery: divider on D1, low-battery blink under 20%
//   8. Offline queue and remote firmware updates

// Pins (never use D0, D6, D7, D8, D9).
const int BATT = D1;
const int BTN1 = D2;       // large: view and send
const int BTN2 = D3;       // small: battery, setup, off
const int MOTOR = D4;
const int RING_PWR = D5;
const int RING_DATA = D10;

// Starting values to tune.
const unsigned long SEND_PAUSE_MS = 2000;
const int MAX_PRESSES = 12;
const unsigned long MAX_RECORD_MS = 15000;
const unsigned long CHECK_INTERVAL_S = 30;
const unsigned long AWAY_SCAN_INTERVAL_S = 300;
const int RING_PIXELS = 12;
const int RING_BRIGHTNESS = 30;   // of 255, keeps the 3V3 pin within budget

void setup() {
  Serial.begin(115200);
}

void loop() {
}
