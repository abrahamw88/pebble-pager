// Hardware checks, built instead of the firmware with -DTEST=n. They use the same settings, Wi-Fi and ntfy
// code as the firmware.
//   1  Board check, no wiring: chip, MAC, internal temperature, Wi-Fi scan.
//   2  Buttons, light ring and motor: every part wired so far works.
//   4  Battery voltage on D1: Serial prints volts, pixel 0 blinks green.
//   6  Wake-and-check timing (power plan B), no wiring: deep sleep, wake every 30 s, join the newest saved
//      network with the remembered channel, address and IP, read the inbox, print timings, sleep again.
//      On USB the port disappears while asleep; hold the onboard button during a wake to stay awake.
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

#elif TEST == 6

#include "esp_timer.h"

const unsigned long CHECK_INTERVAL_MS = 30000;       // wake-to-wake period
const unsigned long JOIN_TIMEOUT_MS = 10000;         // full join
const unsigned long FAST_JOIN_TIMEOUT_MS = 4000;     // join with remembered channel, address and IP
const int COLD_REFRESH_CYCLES = 120;                 // full join about hourly so the DHCP lease stays valid
const unsigned long MAX_AWAKE_MS = 15000;            // failsafe: force sleep so one hung request cannot drain the battery
const unsigned long BOOT_OVERHEAD_MS = 250;          // ROM and bootloader time before setup() runs (estimate)
const float RADIO_ON_MA = 90.0;                      // from the power budget in README.md
const float SLEEP_MA = 0.053;                        // 43 uA deep sleep + about 10 uA battery divider
const float USABLE_MAH = 1600.0;                     // 80% of 2,000 mAh
const int HIST = 16;                                 // recent cycles kept, printed on every wake

// RTC memory survives deep sleep but not a power cycle or a flash.
RTC_DATA_ATTR int cycle = 0, aborts = 0, fastFails = 0, totalMsgs = 0;
RTC_DATA_ATTR bool haveAp = false;                   // router channel, address and IP settings remembered
RTC_DATA_ATTR uint8_t apBssid[6];
RTC_DATA_ATTR int apChannel = 0;
RTC_DATA_ATTR uint32_t ipAddr, ipGw, ipMask, ipDns;
RTC_DATA_ATTR uint16_t hJoin[HIST], hPoll[HIST], hAwake[HIST];
RTC_DATA_ATTR char hHow[HIST];                       // c cold join, f fast join, x fast join failed
RTC_DATA_ATTR unsigned long sumAwakeMs = 0, minAwakeMs = 0xFFFFFFFF, maxAwakeMs = 0;

static void failsafe(void*) {   // runs if a cycle goes past MAX_AWAKE_MS
  aborts++;
  esp_sleep_enable_timer_wakeup(CHECK_INTERVAL_MS * 1000ULL);
  esp_deep_sleep_start();
}

bool waitForJoin(unsigned long timeoutMs) {
  for (unsigned long t0 = millis(); WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs;) delay(20);
  return WiFi.status() == WL_CONNECTED;
}

void countMessage(const NtfyMessage&) { totalMsgs++; }

void setup() {
  esp_timer_handle_t guard;
  esp_timer_create_args_t guardArgs = {};
  guardArgs.callback = failsafe;
  guardArgs.name = "failsafe";
  esp_timer_create(&guardArgs, &guard);
  esp_timer_start_once(guard, MAX_AWAKE_MS * 1000ULL);
  Serial.begin(115200);
  settingsBegin();
  pinMode(BOOT_BTN, INPUT_PULLUP);
  bool stayAwake = digitalRead(BOOT_BTN) == LOW;
  cycle++;
  if (savedCount() == 0 || !ntfyReady()) {
    while (!Serial) delay(10);
    sayln("Needs a saved Wi-Fi network and a topic base: flash the firmware and use the setup page");
    return;
  }

  // 1. Join the newest saved network, reusing the remembered channel, address and IP when there are some.
  String ssid = savedSsid(0), pass = savedPass(0);
  if (cycle % COLD_REFRESH_CYCLES == 0) haveAp = false;
  char how = haveAp ? 'f' : 'c';
  WiFi.mode(WIFI_STA);
  unsigned long t0 = millis();
  if (haveAp) {
    WiFi.config(IPAddress(ipAddr), IPAddress(ipGw), IPAddress(ipMask), IPAddress(ipDns));
    WiFi.begin(ssid.c_str(), pass.c_str(), apChannel, apBssid);
  } else {
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
  bool ok = waitForJoin(haveAp ? FAST_JOIN_TIMEOUT_MS : JOIN_TIMEOUT_MS);
  if (!ok && haveAp) {   // router or lease changed: forget it all and do a full join
    how = 'x';
    fastFails++;
    haveAp = false;
    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.config(IPAddress(), IPAddress(), IPAddress());   // back to DHCP
    WiFi.begin(ssid.c_str(), pass.c_str());
    ok = waitForJoin(JOIN_TIMEOUT_MS);
  }
  unsigned long joinMs = millis() - t0;
  if (ok && !haveAp) {
    memcpy(apBssid, WiFi.BSSID(), 6);
    apChannel = WiFi.channel();
    ipAddr = (uint32_t)WiFi.localIP();
    ipGw = (uint32_t)WiFi.gatewayIP();
    ipMask = (uint32_t)WiFi.subnetMask();
    ipDns = (uint32_t)WiFi.dnsIP();
    haveAp = true;
  }

  // 2. Read the inbox, exactly as the firmware does.
  unsigned long t1 = millis();
  int fresh = ok ? ntfyPoll(countMessage) : -1;
  unsigned long pollMs = millis() - t1;

  // 3. Totals. Radio-on time is everything up to here.
  unsigned long awakeMs = millis() + BOOT_OVERHEAD_MS;
  sumAwakeMs += awakeMs;
  minAwakeMs = min(minAwakeMs, awakeMs);
  maxAwakeMs = max(maxAwakeMs, awakeMs);
  int h = cycle % HIST;
  hJoin[h] = min(joinMs, 65535UL);
  hPoll[h] = min(pollMs, 65535UL);
  hAwake[h] = min(awakeMs, 65535UL);
  hHow[h] = how;
  float avgAwake = (float)sumAwakeMs / cycle;
  float avgMa = (RADIO_ON_MA * avgAwake + SLEEP_MA * (CHECK_INTERVAL_MS - avgAwake)) / CHECK_INTERVAL_MS;

  // Printing waits for the USB serial port to come back and is not counted above.
  for (unsigned long w = millis(); !Serial && millis() - w < 2500;) delay(10);
  sayf("\n#%d join %c %s %lums poll %d %lums awake %lums\n", cycle, how, ok ? "ok" : "fail", joinMs, fresh, pollMs, awakeMs);
  sayf("  msgs %d fastfail %d abort %d\n", totalMsgs, fastFails, aborts);
  sayln("  history join/poll/awake ms:");
  for (int c = max(1, cycle - HIST + 1); c <= cycle; c++)
    sayf("    %d: %u/%u/%u %c\n", c, hJoin[c % HIST], hPoll[c % HIST], hAwake[c % HIST], hHow[c % HIST]);
  sayf("  awake min/avg/max %lu/%.0f/%lu ms, ~%.2f mA, ~%.1f d\n", minAwakeMs, avgAwake, maxAwakeMs, avgMa, USABLE_MAH / avgMa / 24.0);

  if (stayAwake) { sayln("Button held: awake"); return; }
  unsigned long sleepMs = awakeMs < CHECK_INTERVAL_MS ? CHECK_INTERVAL_MS - awakeMs : 1000;
  sayf("  sleep %lums\n", sleepMs);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(sleepMs * 1000ULL);
  esp_deep_sleep_start();
}

void loop() { consoleTick(); }

#else
#error "Unknown TEST: use 1, 2, 4 or 6"
#endif

#endif
