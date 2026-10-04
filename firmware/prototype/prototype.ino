// Pebble prototype: breadboard test firmware.
// Runs the spec's test sketches one at a time. Sketch 1 is active; swap in 2-4 from
// README.md (Build instructions section) as the breadboard grows.
//   1. Hello over Serial        (active below)
//   2. Buttons, ring and motor  (needs Adafruit NeoPixel)
//   3. One ntfy message         (needs secrets.h with Wi-Fi and topic)
//   4. Battery voltage on D1

// Pin plan, same as the full build.
const int BATT = D1;
const int BTN1 = D2;
const int BTN2 = D3;
const int MOTOR = D4;
const int RING_PWR = D5;
const int RING_DATA = D10;

void setup() {
  Serial.begin(115200);
}

void loop() {
  Serial.println("Hello from Pebble");
  delay(1000);
}
