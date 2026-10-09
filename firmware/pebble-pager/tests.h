// Hardware checks, built instead of the firmware with -DTEST=n. They use the same settings, Wi-Fi and ntfy
// code as the firmware.
//   1  Board check, no wiring: chip, MAC, internal temperature, Wi-Fi scan.
//   2  Buttons, light ring and motor: every part wired so far works.
//   4  Battery voltage on D1: Serial prints volts and percent, pixel 0 blinks green.
// Tests 2 and 4 drive the pixels directly, at a fixed low brightness, so a wiring fault is easy to see.
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

const int PIXELS = 12;
Adafruit_NeoPixel strip(PIXELS, RING_DATA, NEO_GRBW + NEO_KHZ800);   // four-channel pixels: red, green, blue, white

void light(int count, uint32_t color) {   // light the first `count` pixels in one color
  strip.clear();
  for (int i = 0; i < count; i++) strip.setPixelColor(i, color);
  strip.show();
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
  Serial.begin(115200);
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

#else
#error "Unknown TEST: use 1, 2 or 4"
#endif

#endif
