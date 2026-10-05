// Pebble prototype: breadboard test firmware.
// Pick one test with TEST below, or on the command line with
//   --build-property compiler.cpp.extra_flags=-DTEST=2
//   0. Bare-board check, no wiring and no secrets.h: chip info, Wi-Fi scan, internal temperature.
//   1. Hello over Serial: proves upload and Serial work.
//   2. Buttons, light ring and motor: every part wired so far works.
//   3. One ntfy message to the phone: joins the network saved by test 5 (run test 5 first), then
//      posts. Needs secrets.h (git-ignored) with TOPIC_URL, e.g. "https://ntfy.sh/pebble-xxxxxxx-phone".
//   4. Battery voltage on D1: divider works; Serial prints volts, pixel 0 blinks green.
//   6. Wake-and-check cycle (power plan B), no wiring: deep sleep, wake every 30 s, join the network
//      saved by test 5, poll the topic in secrets.h for new messages, print timings, sleep again.
//      Run on USB: the port disappears while asleep and returns on each wake. Hold the onboard BOOT
//      button during a wake to stay awake (for flashing). Timings exclude the ~0.25 s ROM boot.
//   7. Remote firmware update: joins a saved network (from test 5), reads manifest.txt from the release
//      location, and if its version is newer than FW_VERSION downloads the .bin, checks size and SHA-256,
//      installs it and reboots, then posts to the phone topic (secrets.h) that it updated. A new image that crashes before validating rolls back to the old one.
//      Tap the onboard BOOT button, post \"update\" to the device topic (<base>-a), or wait for the daily check. Build helpers: -DFW_VERSION=N (default 1), -DFW_CRASH
//      (image that crashes at startup, to test rollback). -DOTA_LOCAL_IP=a.b.c.d tests against a local
//      web server (port 8000) instead of GitHub. See README.md (Remote updates).
//   5. Wi-Fi setup from a phone, no wiring and no secrets.h: hold the onboard BOOT button 5 s and
//      release, join "Pebble-Setup" on the phone, pick a network on the page that opens. Networks are
//      saved on the device (not in the repo), up to 10, with a delete button per network on the page. Hold 10 s and release to clear them. After a join it
//      checks ntfy.sh over HTTPS.
// Wiring steps and expected results are in docs/build-steps.md.

#ifndef TEST
#define TEST 0
#endif

// Pin plan, same as the full build.
const int BATT = D1;
const int BTN1 = D2;
const int BTN2 = D3;
const int MOTOR = D4;
const int RING_PWR = D5;
const int RING_DATA = D10;

#if TEST == 0

#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);   // wait for the USB serial monitor
  Serial.println("\n== Pebble bare-board check ==");
  Serial.printf("Chip: %s rev %d, %d core(s), %d MHz\n", ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.printf("Flash: %u KB, free heap: %u KB\n", ESP.getFlashChipSize() / 1024, ESP.getFreeHeap() / 1024);
  Serial.printf("Internal temperature: %.1f C\n", temperatureRead());

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());   // valid only after the radio starts
  Serial.println("Scanning Wi-Fi (2.4 GHz only)...");
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("No networks found: check the antenna is clicked on.");
  } else {
    Serial.printf("%d networks found:\n", n);
    for (int i = 0; i < n; i++)
      Serial.printf("  %-28s ch %2d  %4d dBm  %s\n", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
                    WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
  }
  Serial.println("== done ==");
}

void loop() {}

#elif TEST == 1

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
#include <Preferences.h>
#include "secrets.h"

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  Preferences prefs;
  prefs.begin("pebble", true);   // networks saved by test 5
  String ssid = prefs.getString("s0", "");
  String pass = prefs.getString("p0", "");
  prefs.end();
  if (ssid.length() == 0) {
    Serial.println("No saved network: run test 5 first.");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
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

#elif TEST == 6

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "secrets.h"
#include "esp_timer.h"

const int BOOT_BTN = 9;                              // onboard BOOT button, LOW when pressed
const unsigned long CHECK_INTERVAL_MS = 30000;       // wake-to-wake period
const unsigned long JOIN_TIMEOUT_MS = 10000;         // full join
const unsigned long FAST_JOIN_TIMEOUT_MS = 4000;     // join with remembered channel, address and IP
const int COLD_REFRESH_CYCLES = 120;                 // full join about hourly so the DHCP lease stays valid
const int CPU_MHZ = 160;                             // 80 saved current but lengthened each wake more than it saved
const unsigned long MAX_AWAKE_MS = 15000;            // failsafe: force sleep so one hung request cannot drain the battery
const unsigned long BOOT_OVERHEAD_MS = 250;          // ROM and bootloader time before setup() runs (estimate)
const float RADIO_ON_MA = 90.0;                      // from the power budget in README.md
const float SLEEP_MA = 0.053;                        // 43 uA deep sleep + about 10 uA battery divider
const float USABLE_MAH = 1600.0;                     // 80% of 2,000 mAh

// RTC memory survives deep sleep but not a power cycle or a flash.
RTC_DATA_ATTR int cycle = 0;
RTC_DATA_ATTR char lastId[24] = "";                  // newest ntfy message id seen
RTC_DATA_ATTR bool haveAp = false;                   // router channel and address remembered
RTC_DATA_ATTR uint8_t apBssid[6];
RTC_DATA_ATTR int apChannel = 0;
RTC_DATA_ATTR bool haveIp = false;                   // IP settings remembered, so DHCP can be skipped
RTC_DATA_ATTR uint32_t ipAddr, ipGw, ipMask, ipDns;
RTC_DATA_ATTR int aborts = 0;                        // cycles ended by the failsafe
RTC_DATA_ATTR int totalMsgs = 0;
RTC_DATA_ATTR char lastText[48] = "";
RTC_DATA_ATTR int fastFails = 0;
const int HIST = 16;                                 // recent cycles kept, printed on every wake
RTC_DATA_ATTR uint16_t hJoin[HIST], hPoll[HIST], hAwake[HIST];
RTC_DATA_ATTR char hHow[HIST];                       // c cold, f fast, i fast+ip, x fast failed
RTC_DATA_ATTR unsigned long sumAwakeMs = 0;
RTC_DATA_ATTR unsigned long minAwakeMs = 0xFFFFFFFF;
RTC_DATA_ATTR unsigned long maxAwakeMs = 0;

String extract(const String& line, const char* key) {   // value of "key":"value" in one JSON line
  String k = String("\"") + key + "\":\"";
  int i = line.indexOf(k);
  if (i < 0) return "";
  i += k.length();
  int j = line.indexOf('"', i);
  return j < 0 ? "" : line.substring(i, j);
}

static void failsafe(void*) {   // runs if a cycle goes past MAX_AWAKE_MS
  aborts++;
  esp_sleep_enable_timer_wakeup(CHECK_INTERVAL_MS * 1000ULL);
  esp_deep_sleep_start();
}

bool waitForJoin(unsigned long timeoutMs) {
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(20);
  return WiFi.status() == WL_CONNECTED;
}

void setup() {
  esp_timer_handle_t guard;
  esp_timer_create_args_t guardArgs = {};
  guardArgs.callback = failsafe;
  guardArgs.name = "failsafe";
  esp_timer_create(&guardArgs, &guard);
  esp_timer_start_once(guard, MAX_AWAKE_MS * 1000ULL);
  setCpuFrequencyMhz(CPU_MHZ);
  Serial.begin(115200);
  pinMode(BOOT_BTN, INPUT_PULLUP);
  bool stayAwake = digitalRead(BOOT_BTN) == LOW;
  cycle++;

  Preferences prefs;
  prefs.begin("pebble", true);   // networks saved by test 5
  String ssid = prefs.getString("s0", ""), pass = prefs.getString("p0", "");
  prefs.end();
  if (ssid.length() == 0) {
    while (!Serial) delay(10);
    Serial.println("No saved network: run test 5 first.");
    return;
  }

  // 1. Join, reusing the remembered channel, address and IP when there are some.
  if (cycle % COLD_REFRESH_CYCLES == 0) { haveAp = false; haveIp = false; }
  WiFi.mode(WIFI_STA);
  const char* how = haveAp ? (haveIp ? "fast+ip" : "fast") : "cold";
  unsigned long t0 = millis();
  if (haveIp) WiFi.config(IPAddress(ipAddr), IPAddress(ipGw), IPAddress(ipMask), IPAddress(ipDns));
  if (haveAp) WiFi.begin(ssid.c_str(), pass.c_str(), apChannel, apBssid);
  else WiFi.begin(ssid.c_str(), pass.c_str());
  bool ok = waitForJoin(haveAp ? FAST_JOIN_TIMEOUT_MS : JOIN_TIMEOUT_MS);
  if (!ok && haveAp) {   // router or lease changed: forget it all and do a full join
    how = "fast-failed";
    fastFails++;
    haveAp = false;
    haveIp = false;
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
    haveAp = true;
    ipAddr = (uint32_t)WiFi.localIP();
    ipGw = (uint32_t)WiFi.gatewayIP();
    ipMask = (uint32_t)WiFi.subnetMask();
    ipDns = (uint32_t)WiFi.dnsIP();
    haveIp = true;
  }

  // 2. Poll the topic for anything newer than the last message seen.
  unsigned long t1 = millis();
  int code = 0, fresh = 0, backlog = 0;
  String texts;
  if (ok) {
    WiFiClientSecure client;
    client.setInsecure();   // test only: skips the certificate check
    client.setHandshakeTimeout(5);
    HTTPClient http;
    http.setConnectTimeout(4000);
    String url = String(TOPIC_URL) + "/json?poll=1&since=" + (lastId[0] ? lastId : "all");
    http.begin(client, url);
    http.setTimeout(8000);
    http.useHTTP10(true);   // server closes the connection after the body: no waiting on chunked reads
    code = http.GET();
    if (code == 200) {
      String body = http.getString();
      int pos = 0;
      while (pos < (int)body.length()) {
        int nl = body.indexOf('\n', pos);
        if (nl < 0) nl = body.length();
        String line = body.substring(pos, nl);
        pos = nl + 1;
        if (line.indexOf("\"event\":\"message\"") < 0) continue;
        String id = extract(line, "id");
        if (id.length() > 0 && id.length() < sizeof(lastId)) id.toCharArray(lastId, sizeof(lastId));
        if (cycle == 1) backlog++;   // first run only catches up; nothing is "new" yet
        else {
          fresh++;
          totalMsgs++;
          String m = extract(line, "message");
          m.toCharArray(lastText, sizeof(lastText));
          texts += "\n  message: " + m;
        }
      }
    }
    http.end();
  }
  unsigned long pollMs = millis() - t1;

  // 3. Totals. Radio-on time is everything up to here.
  unsigned long awakeMs = millis() + BOOT_OVERHEAD_MS;
  sumAwakeMs += awakeMs;
  if (awakeMs < minAwakeMs) minAwakeMs = awakeMs;
  if (awakeMs > maxAwakeMs) maxAwakeMs = awakeMs;
  int h = cycle % HIST;
  hJoin[h] = joinMs > 65535 ? 65535 : joinMs;
  hPoll[h] = pollMs > 65535 ? 65535 : pollMs;
  hAwake[h] = awakeMs > 65535 ? 65535 : awakeMs;
  hHow[h] = how[0] == 'c' ? 'c' : (how[4] == 'f' ? 'x' : (haveIp && how[4] == '+' ? 'i' : 'f'));
  float avgAwake = (float)sumAwakeMs / cycle;
  float avgMa = (RADIO_ON_MA * avgAwake + SLEEP_MA * (CHECK_INTERVAL_MS - avgAwake)) / CHECK_INTERVAL_MS;

  // Printing waits for the USB serial port to come back and is not counted above.
  unsigned long w = millis();
  while (!Serial && millis() - w < 2500) delay(10);
  Serial.printf("\n[cycle %d] join %s %s %lu ms | poll HTTP %d %lu ms | awake %lu ms | new %d%s\n", cycle, how,
                ok ? "ok" : "FAILED", joinMs, code, pollMs, awakeMs, fresh,
                cycle == 1 ? " (caught up on backlog)" : "");
  if (texts.length()) Serial.println(texts);
  Serial.printf("  messages received so far: %d (last: \"%s\"), fast-join failures: %d, failsafe aborts: %d\n", totalMsgs, lastText, fastFails, aborts);
  Serial.println("  recent cycles (cycle: join / poll / awake ms, mode):");
  for (int c = max(1, cycle - HIST + 1); c <= cycle; c++)
    Serial.printf("    %d: %u / %u / %u %c\n", c, hJoin[c % HIST], hPoll[c % HIST], hAwake[c % HIST], hHow[c % HIST]);
  Serial.printf("  awake min/avg/max %lu / %.0f / %lu ms -> est. avg %.2f mA, battery about %.1f days\n", minAwakeMs,
                avgAwake, maxAwakeMs, avgMa, USABLE_MAH / avgMa / 24.0);

  if (stayAwake) {
    Serial.println("BOOT held: staying awake. Reset to resume the cycle.");
    return;
  }
  unsigned long sleepMs = awakeMs < CHECK_INTERVAL_MS ? CHECK_INTERVAL_MS - awakeMs : 1000;
  Serial.printf("  sleeping %lu ms\n", sleepMs);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(sleepMs * 1000ULL);
  esp_deep_sleep_start();
}

void loop() {}

#elif TEST == 7

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include "esp_ota_ops.h"
#include "mbedtls/sha256.h"
#include "secrets.h"

#ifndef FW_VERSION
#define FW_VERSION 1
#endif
#ifdef OTA_LOCAL_IP   // -DOTA_LOCAL_IP=192.168.x.y : test against a local web server on port 8000
#ifndef STR_
#define STR_(x) #x
#define STR(x) STR_(x)
#endif
#define OTA_BASE "http://" STR(OTA_LOCAL_IP) ":8000/"
#endif
#ifdef OTA_HOST   // -DOTA_HOST=name : point at another https host (certificate tests)
#define STR_(x) #x
#define STR(x) STR_(x)
#define OTA_BASE "https://" STR(OTA_HOST) "/"
#endif
#ifndef OTA_BASE   // where manifest.txt and the .bin live; secrets.h can override for local testing
#define OTA_BASE "https://github.com/abrahamw88/pebble-pager/releases/latest/download/"
#endif

const char* MANIFEST_FILE = "manifest.txt";   // lines: version=N, size=BYTES, sha256=HEX
const char* BIN_FILE = "pebble-prototype.bin";
const int BOOT_BTN = 9;
const unsigned long JOIN_TIMEOUT_MS = 8000;
#ifndef DAILY_CHECK_MS   // fallback check when no command arrives; override for testing
#define DAILY_CHECK_MS (24UL * 60 * 60 * 1000)
#endif
const unsigned long COMMAND_POLL_MS = 30000;   // how often the device topic is read for commands

// Keep the new image on probation until it proves itself (Wi-Fi up and manifest readable).
extern "C" bool verifyRollbackLater() { return true; }

// Real certificate checking against the built-in root certificates. That needs the clock, so sync it first.
void secureSetup(WiFiClientSecure& c) { c.useBuiltinCACertBundle(); }

bool syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  unsigned long t0 = millis();
  while (time(nullptr) < 1700000000 && millis() - t0 < 8000) delay(100);
  bool ok = time(nullptr) >= 1700000000;
  Serial.printf("Time sync %s (%lu ms)\n", ok ? "ok" : "FAILED", millis() - t0);
  return ok;
}

int lastHttpCode = 0;   // last HTTP status seen; 0 or negative means the server was never reached

bool isHttps(const String& url) { return url.startsWith("https"); }

// GET `url`, following redirects (GitHub release downloads redirect to another host).
bool openUrl(HTTPClient& http, WiFiClient& plain, WiFiClientSecure& secure, const String& url) {
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  http.useHTTP10(true);
  bool begun = isHttps(url) ? http.begin(secure, url) : http.begin(plain, url);
  if (!begun) return false;
  int code = http.GET();
  lastHttpCode = code;
  Serial.printf("GET %s -> %d\n", url.c_str(), code);
  return code == 200;
}

int savedCount() {
  Preferences prefs;
  prefs.begin("pebble", true);   // networks saved by test 5
  int n = prefs.getInt("n", 0);
  prefs.end();
  return n;
}

bool joinSaved(int index) {
  Preferences prefs;
  prefs.begin("pebble", true);
  String ssid = prefs.getString(("s" + String(index)).c_str(), "");
  String pass = prefs.getString(("p" + String(index)).c_str(), "");
  prefs.end();
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  Serial.printf("Joining \"%s\"...\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < JOIN_TIMEOUT_MS) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

bool postNtfy(const char* title, const String& message) {
  WiFiClientSecure client;
  secureSetup(client);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  if (!http.begin(client, TOPIC_URL)) return false;
  http.addHeader("Title", title);
  int code = http.POST(message);
  http.end();
  Serial.printf("ntfy post \"%s\" -> %d\n", message.c_str(), code);
  return code == 200;
}

// After an update the old image records "from" and "to"; the next boot reports how it went.
void reportUpdateResult() {
  Preferences prefs;
  prefs.begin("pebble", false);
  int to = prefs.getInt("otaTo", 0), from = prefs.getInt("otaFrom", 0);
  if (to == 0) { prefs.end(); return; }
  bool ok = FW_VERSION == to;
  String msg = ok ? "Firmware updated from version " + String(from) + " to " + String(to) + "."
                  : "Update to version " + String(to) + " failed and rolled back. Still on version " + String(FW_VERSION) + ".";
  if (postNtfy(ok ? "Pebble updated" : "Pebble update failed", msg)) {
    prefs.remove("otaTo");
    prefs.remove("otaFrom");
  }   // if the post fails, it is tried again on the next boot
  prefs.end();
}

String jsonText(const String& line, const char* key) {   // value of "key":"value" in one JSON line
  String k = String("\"") + key + "\":\"";
  int i = line.indexOf(k);
  if (i < 0) return "";
  i += k.length();
  int j = line.indexOf('"', i);
  return j < 0 ? "" : line.substring(i, j);
}

String field(const String& text, const char* key) {   // value of key=value on its own line
  String k = String(key) + "=";
  int i = text.indexOf(k);
  if (i < 0) return "";
  i += k.length();
  int j = text.indexOf('\n', i);
  String v = j < 0 ? text.substring(i) : text.substring(i, j);
  v.trim();
  return v;
}

// Why an install did not happen. Bad images are never retried; network trouble is.
const int OTA_OK = 0, OTA_NET_ERR = 1, OTA_BAD_IMAGE = 2;   // plain ints: Arduino generates prototypes before types
const char* otaReason = "";

int installUpdate(size_t size, const String& wantSha) {
  WiFiClient plain;
  WiFiClientSecure secure;
  secureSetup(secure);
  HTTPClient http;
  if (!openUrl(http, plain, secure, String(OTA_BASE) + BIN_FILE)) { http.end(); otaReason = "download failed"; return OTA_NET_ERR; }
  if (http.getSize() != (int)size) {
    Serial.printf("Size mismatch: server says %d, manifest says %u\n", http.getSize(), (unsigned)size);
    http.end();
    otaReason = "file size does not match the manifest";
    return OTA_BAD_IMAGE;
  }
  if (!Update.begin(size)) {
    Serial.printf("Update.begin failed: %s\n", Update.errorString());
    http.end();
    otaReason = "image does not fit";
    return OTA_BAD_IMAGE;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  size_t done = 0;
  int lastPct = -1;
  unsigned long lastData = millis();
  while (done < size && millis() - lastData < 20000) {
    int avail = stream->available();
    if (avail <= 0) { delay(5); continue; }
    int n = stream->readBytes(buf, min((size_t)avail, sizeof(buf)));
    if (n <= 0) continue;
    lastData = millis();
    if (Update.write(buf, n) != (size_t)n) {
      Serial.printf("Write failed: %s\n", Update.errorString());
      Update.abort();
      http.end();
      otaReason = "flash write failed";
      return OTA_NET_ERR;
    }
    mbedtls_sha256_update(&sha, buf, n);
    done += n;
    int pct = done * 100 / size;
    if (pct / 10 != lastPct / 10) { Serial.printf("  %d%%\n", pct); lastPct = pct; }
  }
  http.end();
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  if (done != size) {
    Serial.println("Download incomplete.");
    Update.abort();
    otaReason = "download incomplete";
    return OTA_NET_ERR;
  }
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", digest[i]);
  if (!wantSha.equalsIgnoreCase(hex)) {
    Serial.printf("SHA-256 mismatch. Got %s\n", hex);
    Update.abort();   // nothing is installed
    otaReason = "SHA-256 does not match the manifest";
    return OTA_BAD_IMAGE;
  }
  if (!Update.end(true)) {
    Serial.printf("Update.end failed: %s\n", Update.errorString());
    otaReason = "image check failed";
    return OTA_BAD_IMAGE;
  }
  return OTA_OK;
}

// Returns true if an update was installed (the caller then reboots).
const char* checkNote = "";   // set by checkForUpdate: "current", "skipped" or "" when something else happened
int checkedVersion = 0;       // the release version the last check saw

bool checkForUpdate(bool* reached) {
  checkNote = "";
  *reached = false;
  WiFiClient plain;
  WiFiClientSecure secure;
  secureSetup(secure);
  HTTPClient http;
  if (!openUrl(http, plain, secure, String(OTA_BASE) + MANIFEST_FILE)) {
    http.end();
    *reached = lastHttpCode > 0;   // any answer (even 404) proves Wi-Fi and TLS work; only silence means try another network
    return false;
  }
  *reached = true;
  String manifest = http.getString();
  http.end();
  int remote = field(manifest, "version").toInt();
  checkedVersion = remote;
  size_t size = field(manifest, "size").toInt();
  String sha = field(manifest, "sha256");
  Serial.printf("Running version %d, release has version %d\n", FW_VERSION, remote);
  Preferences badPrefs;
  badPrefs.begin("pebble", true);
  int bad = badPrefs.getInt("otaBad", 0);
  badPrefs.end();
  if (remote == bad) { Serial.printf("Version %d failed to start earlier; skipping it.\n", bad); checkNote = "skipped"; return false; }
  if (remote <= FW_VERSION) { Serial.println("Up to date."); checkNote = "current"; return false; }
  if (size == 0 || sha.length() != 64) { Serial.println("Manifest is incomplete; not updating."); return false; }
  Serial.printf("Installing version %d (%u bytes)...\n", remote, (unsigned)size);
  int r = installUpdate(size, sha);
  if (r == OTA_NET_ERR) { Serial.printf("Update to %d did not finish (%s); will retry.\n", remote, otaReason); return false; }
  Preferences prefs;
  prefs.begin("pebble", false);
  if (r == OTA_BAD_IMAGE) {
    prefs.putInt("otaBad", remote);   // never retried
    prefs.end();
    postNtfy("Pebble update rejected", "Update to version " + String(remote) + " rejected: " + otaReason + ". Still on version " + String(FW_VERSION) + ".");
    return false;
  }
  prefs.putInt("otaFrom", FW_VERSION);
  prefs.putInt("otaTo", remote);
  prefs.end();
  return true;
}

// The command topic is this device's own inbox: the phone topic name with -phone swapped for -a.
String commandUrl() {
  String u = TOPIC_URL;
  if (u.endsWith("-phone")) u = u.substring(0, u.length() - 6);
  return u + "-a";
}

// Read new messages from the command topic. Returns true if one of them said "update".
// The first run only catches up (so old commands are never replayed); the last id seen is kept across reboots.
bool pollCommands() {
  Preferences prefs;
  prefs.begin("pebble", false);
  String last = prefs.getString("cmdId", "");
  WiFiClientSecure client;
  secureSetup(client);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  http.useHTTP10(true);
  String url = commandUrl() + "/json?poll=1&since=" + (last.length() ? last : String("all"));
  bool wantUpdate = false;
  if (http.begin(client, url) && http.GET() == 200) {
    String body = http.getString();
    int pos = 0;
    while (pos < (int)body.length()) {
      int nl = body.indexOf('\n', pos);
      if (nl < 0) nl = body.length();
      String line = body.substring(pos, nl);
      pos = nl + 1;
      if (line.indexOf("\"event\":\"message\"") < 0) continue;
      String id = jsonText(line, "id"), msg = jsonText(line, "message");
      if (id.length()) { prefs.putString("cmdId", id); }
      if (last.length() == 0) continue;   // catching up on first run
      msg.trim();
      msg.toLowerCase();
      Serial.printf("Command: \"%s\"\n", msg.c_str());
      if (msg == "update") wantUpdate = true;   // anything else is ignored
    }
  }
  http.end();
  if (last.length() == 0 && prefs.getString("cmdId", "").length() == 0)
    prefs.putString("cmdId", String((unsigned long)time(nullptr)));   // empty topic: remember "now", so the first real command is not swallowed
  prefs.end();
  return wantUpdate;
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
#ifdef FW_CRASH
  Serial.println("FW_CRASH build: crashing before validating, to test rollback.");
  delay(500);
  abort();
#endif
  pinMode(BOOT_BTN, INPUT_PULLUP);
  {   // An update was started but this is not the version it was meant to install: the new one failed and rolled back.
    Preferences prefs;
    prefs.begin("pebble", false);
    int to = prefs.getInt("otaTo", 0);
    if (to != 0 && FW_VERSION != to) prefs.putInt("otaBad", to);   // remembered so it is not retried in a loop
    prefs.end();
  }
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  const char* st = "unknown";
  if (esp_ota_get_state_partition(running, &state) == ESP_OK)
    st = state == ESP_OTA_IMG_PENDING_VERIFY ? "pending verify (on probation)" : (state == ESP_OTA_IMG_VALID ? "valid" : "other");
  Serial.printf("\n== Pebble OTA test: version %d, running slot %s, image %s ==\n", FW_VERSION, running->label, st);

  // Try each saved network in turn until one can reach the release location.
  bool reached = false;
  for (int i = 0; i < savedCount() && !reached; i++) {
    if (!joinSaved(i)) continue;
    Serial.printf("Connected, IP %s\n", WiFi.localIP().toString().c_str());
    if (isHttps(OTA_BASE) && !syncTime()) { Serial.println("No time, so certificates cannot be checked; not updating."); continue; }
    if (checkForUpdate(&reached)) {
      Serial.println("Update installed. Rebooting into it...");
      delay(500);
      ESP.restart();
    }
    if (!reached) Serial.println("Release location not reachable on this network; trying the next one.");
  }
  if (!reached) { Serial.println("No saved network could reach the release location: run test 5, or check OTA_BASE."); return; }
  // Wi-Fi up and the release location readable: the running image has proven itself.
  esp_ota_mark_app_valid_cancel_rollback();
  Serial.println("Marked this image valid. Tap BOOT to check again.");
  reportUpdateResult();
}

// Check for an update; tell the phone when the check was asked for, and reboot if one was installed.
void runCheck(bool asked) {
  bool reached;
  if (asked) postNtfy("Pebble update", "Update command received. Checking for a newer version...");
  if (checkForUpdate(&reached)) {
    Serial.println("Update installed. Rebooting into it...");
    delay(500);
    ESP.restart();
  }
  if (!asked) return;
  if (!reached) postNtfy("Pebble update", "Could not reach the update server. Still on version " + String(FW_VERSION) + ".");
  else if (!strcmp(checkNote, "current")) postNtfy("Pebble update", "Already on the latest version (" + String(FW_VERSION) + ").");
  else if (!strcmp(checkNote, "skipped")) postNtfy("Pebble update", "Version " + String(checkedVersion) + " was rejected or failed earlier, so it is skipped. Still on version " + String(FW_VERSION) + ".");
}   // installed, rejected and failed outcomes are reported by the update code itself

void loop() {
  static bool wasDown = false;
  static unsigned long lastPoll = 0, lastCheck = 0;
  bool down = digitalRead(BOOT_BTN) == LOW;
  bool tapped = wasDown && !down;
  wasDown = down;

  if (WiFi.status() == WL_CONNECTED) {
    if (tapped) { Serial.println("Checking for an update (BOOT tap)..."); runCheck(false); }
    if (millis() - lastPoll >= COMMAND_POLL_MS) {
      lastPoll = millis();
      if (pollCommands()) runCheck(true);
    }
    if (millis() - lastCheck >= DAILY_CHECK_MS) {
      lastCheck = millis();
      Serial.println("Daily update check...");
      runCheck(false);
    }
  } else if (millis() - lastPoll >= COMMAND_POLL_MS) {   // lost Wi-Fi: rejoin a saved network
    lastPoll = millis();
    for (int i = 0; i < savedCount() && WiFi.status() != WL_CONNECTED; i++) joinSaved(i);
  }
  delay(20);
}

#elif TEST == 5

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

const int BOOT_BTN = 9;                    // onboard BOOT button (GPIO9), LOW when pressed
const char* SETUP_SSID = "pebblepager";
const char* SETUP_PASS = "pebblepager";   // placeholder; the full build should use a per-device value
const unsigned long SETUP_HOLD_MS = 5000;
const unsigned long CLEAR_HOLD_MS = 10000;
const unsigned long SETUP_TIMEOUT_MS = 5UL * 60 * 1000;
const unsigned long JOIN_TIMEOUT_MS = 15000;
const int MAX_NETWORKS = 10;

Preferences prefs;
WebServer server(80);
DNSServer dns;
bool portalOn = false;
unsigned long portalStart = 0;
bool joinedViaPortal = false;

String esc(const String& s) {
  String o;
  for (char c : s) {
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

int savedCount() { return prefs.getInt("n", 0); }

// Newest network goes first; a repeated name is replaced; the list is capped.
void saveNetwork(const String& ssid, const String& pass) {
  String ss[MAX_NETWORKS], pp[MAX_NETWORKS];
  int n = 0;
  ss[n] = ssid; pp[n] = pass; n++;
  int old = savedCount();
  for (int i = 0; i < old && n < MAX_NETWORKS; i++) {
    String s = prefs.getString(("s" + String(i)).c_str(), "");
    if (s == ssid) continue;
    ss[n] = s; pp[n] = prefs.getString(("p" + String(i)).c_str(), ""); n++;
  }
  for (int i = 0; i < n; i++) {
    prefs.putString(("s" + String(i)).c_str(), ss[i]);
    prefs.putString(("p" + String(i)).c_str(), pp[i]);
  }
  prefs.putInt("n", n);
}

// Remove saved network `index`, shifting later ones up.
void deleteNetwork(int index) {
  int n = savedCount();
  if (index < 0 || index >= n) return;
  for (int i = index; i < n - 1; i++) {
    prefs.putString(("s" + String(i)).c_str(), prefs.getString(("s" + String(i + 1)).c_str(), ""));
    prefs.putString(("p" + String(i)).c_str(), prefs.getString(("p" + String(i + 1)).c_str(), ""));
  }
  prefs.remove(("s" + String(n - 1)).c_str());
  prefs.remove(("p" + String(n - 1)).c_str());
  prefs.putInt("n", n - 1);
}

bool waitForJoin() {
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < JOIN_TIMEOUT_MS) delay(250);
  return WiFi.status() == WL_CONNECTED;
}

// Join whichever saved network is in range with the strongest signal.
bool joinSaved() {
  int n = savedCount();
  if (n == 0) return false;
  int found = WiFi.scanNetworks();
  int best = -1, bestRssi = -1000;
  for (int i = 0; i < n; i++) {
    String s = prefs.getString(("s" + String(i)).c_str(), "");
    for (int j = 0; j < found; j++)
      if (WiFi.SSID(j) == s && WiFi.RSSI(j) > bestRssi) { best = i; bestRssi = WiFi.RSSI(j); }
  }
  if (best < 0) { Serial.println("No saved network in range."); return false; }
  String s = prefs.getString(("s" + String(best)).c_str(), "");
  Serial.printf("Joining saved network \"%s\" (%d dBm)...\n", s.c_str(), bestRssi);
  WiFi.begin(s.c_str(), prefs.getString(("p" + String(best)).c_str(), "").c_str());
  return waitForJoin();
}

void healthCheck() {
  WiFiClientSecure client;
  client.setInsecure();   // test only: skips the certificate check
  HTTPClient http;
  http.begin(client, "https://ntfy.sh/v1/health");
  int code = http.GET();
  Serial.printf("ntfy.sh health: HTTP %d %s\n", code, code > 0 ? http.getString().c_str() : "");
  http.end();
}

void handleRoot() {
  int found = WiFi.scanNetworks();
  String page = "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
                "<title>Pebble Wi-Fi</title><body style='font-family:sans-serif;max-width:28em;margin:1em auto;padding:0 1em'>"
                "<h2>Pebble Wi-Fi setup</h2><form method=post action=/save>"
                "<p><label>Network<br><select name=ssid style='font-size:1.1em;width:100%'>";
  for (int i = 0; i < found; i++)
    page += "<option>" + esc(WiFi.SSID(i)) + "</option>";
  page += "</select></label></p><p><label>Password<br><input name=pass type=password "
          "style='font-size:1.1em;width:100%'></label></p>"
          "<p><button style='font-size:1.1em;padding:.6em 1.2em'>Save and join</button></p></form>"
          "<p>2.4 GHz networks only.</p>";
  int n = savedCount();
  if (n > 0) {
    page += "<h3>Saved networks (" + String(n) + " of " + String(MAX_NETWORKS) + ")</h3>";
    if (n >= MAX_NETWORKS) page += "<p>The list is full: saving another removes the oldest. Delete one below to choose which.</p>";
    for (int i = 0; i < n; i++)
      page += "<form method=post action=/delete style='display:flex;justify-content:space-between;align-items:center;margin:.4em 0'>"
              "<span>" + esc(prefs.getString(("s" + String(i)).c_str(), "")) + "</span>"
              "<input type=hidden name=i value=" + String(i) + ">"
              "<button style='font-size:1em;padding:.4em .9em'>Delete</button></form>";
  }
  page += "</body>";
  server.send(200, "text/html", page);
}

void handleDelete() {
  deleteNetwork(server.arg("i").toInt());
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handleSave() {
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  if (ssid.length() == 0) { server.send(400, "text/plain", "Pick a network."); return; }
  saveNetwork(ssid, pass);
  WiFi.begin(ssid.c_str(), pass.c_str());
  bool ok = waitForJoin();
  String page = "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
                "<body style='font-family:sans-serif;max-width:28em;margin:1em auto;padding:0 1em'>";
  page += ok ? "<h2>Connected to " + esc(ssid) + "</h2><p>Saved. You can leave this page.</p>"
             : "<h2>Could not join " + esc(ssid) + "</h2><p>Saved anyway. Check the password and try again.</p>";
  page += "</body>";
  server.send(200, "text/html", page);
  Serial.printf("Portal: %s \"%s\"\n", ok ? "joined" : "failed to join", ssid.c_str());
  joinedViaPortal = ok;
}

void startPortal() {
  Serial.println("Setup mode: join Wi-Fi \"pebblepager\" on the phone (password in the sketch).");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_SSID, SETUP_PASS);
  dns.start(53, "*", WiFi.softAPIP());   // captive portal: every name points here
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/delete", HTTP_POST, handleDelete);
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  });
  server.begin();
  portalOn = true;
  joinedViaPortal = false;
  portalStart = millis();
}

void stopPortal() {
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalOn = false;
  Serial.println("Setup mode ended.");
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  Serial.println("\n== Pebble Wi-Fi setup test ==");
  pinMode(BOOT_BTN, INPUT_PULLUP);
  prefs.begin("pebble", false);
  WiFi.mode(WIFI_STA);
  Serial.printf("%d saved network(s).\n", savedCount());
  if (joinSaved()) {
    Serial.printf("Connected, IP %s\n", WiFi.localIP().toString().c_str());
    healthCheck();
  }
  Serial.println("Hold BOOT 5 s and release for setup, 10 s to clear saved networks.");
}

void loop() {
  static unsigned long pressedAt = 0;
  static bool told5 = false, told10 = false;
  if (digitalRead(BOOT_BTN) == LOW) {
    if (!pressedAt) pressedAt = millis();
    unsigned long held = millis() - pressedAt;
    if (held >= SETUP_HOLD_MS && !told5) { told5 = true; Serial.println("5 s: release for setup."); }
    if (held >= CLEAR_HOLD_MS && !told10) { told10 = true; Serial.println("10 s: release to clear saved networks."); }
  } else if (pressedAt) {
    unsigned long held = millis() - pressedAt;
    pressedAt = 0; told5 = told10 = false;
    if (held >= CLEAR_HOLD_MS) {
      prefs.clear();
      Serial.println("Saved networks cleared.");
    } else if (held >= SETUP_HOLD_MS && !portalOn) {
      startPortal();
    }
  }
  if (portalOn) {
    dns.processNextRequest();
    server.handleClient();
    if (joinedViaPortal) {
      delay(3000);   // let the phone show its confirmation page
      server.handleClient();
      Serial.printf("Connected, IP %s\n", WiFi.localIP().toString().c_str());
      healthCheck();
      stopPortal();
    } else if (millis() - portalStart > SETUP_TIMEOUT_MS) {
      stopPortal();
    }
  }
  delay(10);
}

#endif
