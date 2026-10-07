// Hardware checks, built instead of the firmware with -DTEST=n. They use the same settings, Wi-Fi and ntfy
// code as the firmware.
//   1  Board check, no wiring: chip, MAC, internal temperature, Wi-Fi scan.
//   2  Buttons, light ring and motor: every part wired so far works.
//   4  Battery voltage on D1: Serial prints volts, pixel 0 blinks green.
#ifndef PEBBLE_TESTS_H
#define PEBBLE_TESTS_H

#if TEST == 1

void setup() {
  Serial.begin(115200);
  settingsBegin();
  for (unsigned long t0 = millis(); !Serial && millis() - t0 < 3000;) delay(10);   // wait for the serial monitor
  sayln("\n== board check ==");
  sayf("Chip %s r%d x%d %d MHz\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getCpuFreqMHz());
  sayf("Flash %u KB, heap %u KB\n", ESP.getFlashChipSize() / 1024, ESP.getFreeHeap() / 1024);
  sayf("Temp %.1f C\n", temperatureRead());
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  sayf("MAC %s\n", WiFi.macAddress().c_str());   // valid only after the radio starts
  sayln("Scanning...");
  int n = WiFi.scanNetworks();
  if (n <= 0) sayln("No networks: check antenna");
  for (int i = 0; i < n; i++)
    sayf("  %-28s ch %2d  %4d dBm  %s\n", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
         WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
  sayln("done");
}

void loop() { consoleTick(); }

#elif TEST == 2

#include <Adafruit_NeoPixel.h>

const int PIXELS = 12;
Adafruit_NeoPixel ring(PIXELS, RING_DATA, NEO_GRBW + NEO_KHZ800);   // four-channel pixels: red, green, blue, white

void light(int count, uint32_t color) {   // light the first `count` pixels in one color
  ring.clear();
  for (int i = 0; i < count; i++) ring.setPixelColor(i, color);
  ring.show();
}

void setup() {
  Serial.begin(115200);
  settingsBegin();
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
    sayln("Button 1");
    light(PIXELS, ring.Color(255, 40, 90, 60));   // soft pink: color plus a little white
    digitalWrite(MOTOR, HIGH);
    while (digitalRead(BTN1) == LOW) delay(10);
    digitalWrite(MOTOR, LOW);
    light(0, 0);
  }
  if (digitalRead(BTN2) == LOW) {
    sayln("Button 2");
    light(9, ring.Color(0, 255, 0, 0));           // like a battery gauge: 9 of 12
    digitalWrite(MOTOR, HIGH);
    delay(100);
    digitalWrite(MOTOR, LOW);
    delay(900);
    light(0, 0);
  }
}

#elif TEST == 4

#include <Adafruit_NeoPixel.h>

Adafruit_NeoPixel ring(12, RING_DATA, NEO_GRBW + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  settingsBegin();
  pinMode(RING_PWR, OUTPUT);
  digitalWrite(RING_PWR, HIGH);
  delay(10);
  ring.begin();
  ring.setBrightness(30);
}

void loop() {
  // Average 16 readings, as Seeed's battery guide does. The two equal resistors halve the voltage, so double it.
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(BATT);
  sayf("Batt %.2f V\n", 2 * mv / 16 / 1000.0);
  ring.setPixelColor(0, ring.Color(0, 255, 0, 0));   // green blink: still running
  ring.show();
  delay(100);
  ring.clear();
  ring.show();
  delay(1900);
}

#else
#error "Unknown TEST: use 1, 2 or 4"
#endif

#endif
