// Pebble prototype: breadboard test firmware.
// Pick one test with TEST below, or on the command line with
//   --build-property compiler.cpp.extra_flags=-DTEST=2
//   1. Hello over Serial: proves upload and Serial work.
//   2. Buttons, light ring and motor: every part wired so far works.
//   3. One ntfy message to the phone: Wi-Fi and ntfy work. Needs secrets.h (git-ignored) with
//      WIFI_NAME, WIFI_PASS and TOPIC_URL, e.g. "https://ntfy.sh/pebble-xxxxxxx-phone".
//   4. Battery voltage on D1: divider works; Serial prints volts, pixel 0 blinks green.
// Wiring steps and expected results are in docs/build-steps.md.

#ifndef TEST
#define TEST 1
#endif

// Pin plan, same as the full build.
const int BATT = D1;
const int BTN1 = D2;
const int BTN2 = D3;
const int MOTOR = D4;
const int RING_PWR = D5;
const int RING_DATA = D10;

#if TEST == 1

void setup() {
  Serial.begin(115200);
}

void loop() {
  Serial.println("Hello from Pebble");
  delay(1000);
}

#elif TEST == 2

#include <Adafruit_NeoPixel.h>

const int PIXELS = 12;
// NEO_GRBW: four-channel pixels (red, green, blue, white)
Adafruit_NeoPixel ring(PIXELS, RING_DATA, NEO_GRBW + NEO_KHZ800);

// Light the first `count` pixels in one color
void light(int count, uint32_t color) {
  ring.clear();
  for (int i = 0; i < count; i++) ring.setPixelColor(i, color);
  ring.show();
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(MOTOR, OUTPUT);
  pinMode(RING_PWR, OUTPUT);
  digitalWrite(RING_PWR, HIGH);         // switch the ring's power on
  delay(10);
  ring.begin();
  ring.setBrightness(30);               // out of 255: soft, and easy on the 3V3 supply
  for (int i = 1; i <= PIXELS; i++) {   // blue sweep: every pixel works
    light(i, ring.Color(0, 0, 255, 0));
    delay(100);
  }
  light(PIXELS, ring.Color(0, 0, 0, 255));   // white LEDs only
  delay(700);
  light(0, 0);
}

void loop() {
  if (digitalRead(BTN1) == LOW) {
    Serial.println("Button 1");
    light(PIXELS, ring.Color(255, 40, 90, 60));   // soft pink: color plus a little white
    digitalWrite(MOTOR, HIGH);
    while (digitalRead(BTN1) == LOW) delay(10);
    digitalWrite(MOTOR, LOW);
    light(0, 0);
  }
  if (digitalRead(BTN2) == LOW) {
    Serial.println("Button 2");
    light(9, ring.Color(0, 255, 0, 0));           // like a battery gauge: 9 of 12
    digitalWrite(MOTOR, HIGH);
    delay(100);
    digitalWrite(MOTOR, LOW);
    delay(900);
    light(0, 0);
  }
}

#elif TEST == 3

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "secrets.h"

void setup() {
  Serial.begin(115200);
  delay(1000);
  WiFi.begin(WIFI_NAME, WIFI_PASS);
  Serial.print("Joining Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Wi-Fi connected");

  WiFiClientSecure client;
  client.setInsecure();   // test only: skips the certificate check
  HTTPClient http;
  http.begin(client, TOPIC_URL);
  http.addHeader("Title", "Pebble test");
  http.addHeader("Priority", "high");
  int code = http.POST("Hello from the breadboard");
  Serial.printf("ntfy replied %d\n", code);
  http.end();
}

void loop() {}

#elif TEST == 4

#include <Adafruit_NeoPixel.h>

Adafruit_NeoPixel ring(12, RING_DATA, NEO_GRBW + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  pinMode(RING_PWR, OUTPUT);
  digitalWrite(RING_PWR, HIGH);    // switch the ring's power on
  delay(10);
  ring.begin();
  ring.setBrightness(30);
}

void loop() {
  // Average 16 readings, as Seeed's battery guide does, to smooth out noise.
  // The two equal resistors halve the voltage, so double the result.
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(BATT);
  float volts = 2 * mv / 16 / 1000.0;
  Serial.printf("Battery: %.2f V\n", volts);

  ring.setPixelColor(0, ring.Color(0, 255, 0, 0));   // green blink: still running
  ring.show();
  delay(100);
  ring.clear();
  ring.show();
  delay(1900);
}

#endif
