// Hardware checks, built instead of the firmware with -DTEST=n. They use the same settings, Wi-Fi and ntfy
// code as the firmware.
//   1  Board check, no wiring: chip, MAC, internal temperature, Wi-Fi scan.
//   2  Buttons, light ring and motor: every part wired so far works.
//   4  Battery voltage on D1: Serial prints volts and percent, pixel 0 blinks green.
//   5  Button stress: like test 2, but never waits on a button or the serial port, a heartbeat pixel shows the
//      program is still running, and every 2 s it prints the reset reason, press counts and the raw pin levels.
// Tests 2, 4 and 5 drive the pixels directly, at a fixed low brightness, so a wiring fault is easy to see.
#ifndef PEBBLE_TESTS_H
#define PEBBLE_TESTS_H

#if TEST == 1

void setup() {
  serialBegin();
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

const int PIXELS = 12;
Adafruit_NeoPixel strip(PIXELS, RING_DATA, NEO_GRBW + NEO_KHZ800);   // four-channel pixels: red, green, blue, white

void light(int count, uint32_t color) {   // light the first `count` pixels in one color
  strip.clear();
  for (int i = 0; i < count; i++) strip.setPixelColor(i, color);
  strip.show();
}

void setup() {
  serialBegin();
  settingsBegin();
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(MOTOR, OUTPUT);
  pinMode(RING_PWR, OUTPUT);
  digitalWrite(RING_PWR, HIGH);         // switch the ring's power on
  delay(10);
  strip.begin();
  strip.setBrightness(30);              // out of 255: soft, and easy on the 3V3 supply
  for (int i = 1; i <= PIXELS; i++) {   // blue sweep: every pixel works
    light(i, strip.Color(0, 0, 255, 0));
    delay(100);
  }
  light(PIXELS, strip.Color(0, 0, 0, 255));   // white LEDs only
  delay(700);
  light(0, 0);
}

void loop() {
  if (digitalRead(BTN1) == LOW) {
    sayln("Button 1");
    light(PIXELS, strip.Color(255, 40, 90, 60));   // soft pink: color plus a little white
    digitalWrite(MOTOR, HIGH);
    while (digitalRead(BTN1) == LOW) delay(10);
    digitalWrite(MOTOR, LOW);
    light(0, 0);
  }
  if (digitalRead(BTN2) == LOW) {
    sayln("Button 2");
    light(9, strip.Color(0, 255, 0, 0));           // like a battery gauge: 9 of 12
    digitalWrite(MOTOR, HIGH);
    delay(100);
    digitalWrite(MOTOR, LOW);
    delay(900);
    light(0, 0);
  }
}

#elif TEST == 4

Adafruit_NeoPixel strip(12, RING_DATA, NEO_GRBW + NEO_KHZ800);

void setup() {
  serialBegin();
  settingsBegin();
  pinMode(RING_PWR, OUTPUT);
  digitalWrite(RING_PWR, HIGH);
  delay(10);
  strip.begin();
  strip.setBrightness(30);
}

void loop() {
  batteryRead();   // the firmware's own reading (battery.h)
  sayf("%s\n", batteryText().c_str());
  strip.setPixelColor(0, strip.Color(0, 255, 0, 0));   // green blink: still running
  strip.show();
  delay(100);
  strip.clear();
  strip.show();
  delay(1900);
}

#elif TEST == 5

#include <esp_system.h>

const int PIXELS = 12;
Adafruit_NeoPixel strip(PIXELS, RING_DATA, NEO_GRBW + NEO_KHZ800);

const char* resetName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "reset pin";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "PANIC (crash)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "BROWNOUT (supply dipped)";
    default: return "other";
  }
}

void setup() {
  serialBegin();
  settingsBegin();
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(RING_PWR, OUTPUT);
  digitalWrite(RING_PWR, HIGH);
  delay(10);
  strip.begin();
  strip.setBrightness(30);
  strip.clear();
  strip.show();
}

// Each pin must read the same twice in a row before a change counts. Nothing here ever waits.
struct Pin { int pin; bool last = false, down = false; unsigned long presses = 0, bounces = 0; };
Pin b1{BTN1}, b2{BTN2};
void readPin(Pin& p) {
  bool raw = digitalRead(p.pin) == LOW;
  if (raw == p.last) {
    if (raw != p.down) { p.down = raw; if (raw) p.presses++; }
  } else p.bounces++;
  p.last = raw;
}

void loop() {
  static unsigned long lastShow = 0, lastSay = 0, loops = 0, maxGap = 0, lastLoop = 0;
  unsigned long now = millis();
  if (lastLoop && now - lastLoop > maxGap) maxGap = now - lastLoop;
  lastLoop = now;
  loops++;
  readPin(b1);
  readPin(b2);
  if (now - lastShow >= 20) {   // redraw 50 times a second, whatever else happens
    lastShow = now;
    strip.clear();
    if (b1.down) for (int i = 0; i < PIXELS; i++) strip.setPixelColor(i, strip.Color(255, 40, 90, 60));
    if (b2.down) for (int i = 0; i < 9; i++) strip.setPixelColor(i, strip.Color(0, 255, 0, 0));
    if ((now / 500) % 2) strip.setPixelColor(11, strip.Color(0, 0, 255, 0));   // heartbeat: blinks while the loop runs
    strip.show();
  }
  if (now - lastSay >= 2000) {
    lastSay = now;
    if (Serial.availableForWrite() >= 120)   // never block on a serial port nobody is reading
      sayf("t=%lus reset=%s | b1 %s presses=%lu bounces=%lu | b2 %s presses=%lu | loops=%lu maxgap=%lums | pins %d%d\n",
           now / 1000, resetName(), b1.down ? "DOWN" : "up", b1.presses, b1.bounces, b2.down ? "DOWN" : "up", b2.presses, loops, maxGap,
           digitalRead(BTN1), digitalRead(BTN2));
    loops = 0;
    maxGap = 0;
  }
}

#else
#error "Unknown TEST: use 1, 2, 4 or 5"
#endif

#endif
