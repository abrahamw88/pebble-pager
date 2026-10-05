// Pebble prototype: breadboard test firmware.
// Pick one test with TEST below, or on the command line with
//   --build-property compiler.cpp.extra_flags=-DTEST=2
//   0. Bare-board check, no wiring and no secrets.h: chip info, Wi-Fi scan, internal temperature.
//   1. Hello over Serial: proves upload and Serial work.
//   2. Buttons, light ring and motor: every part wired so far works.
//   3. One ntfy message to the phone: joins the network saved by test 5 (run test 5 first), then
//      posts. Needs secrets.h (git-ignored) with TOPIC_URL, e.g. "https://ntfy.sh/pebble-xxxxxxx-phone".
//   4. Battery voltage on D1: divider works; Serial prints volts, pixel 0 blinks green.
//   5. Wi-Fi setup from a phone, no wiring and no secrets.h: hold the onboard BOOT button 5 s and
//      release, join "Pebble-Setup" on the phone, pick a network on the page that opens. Networks are
//      saved on the device (not in the repo). Hold 10 s and release to clear them. After a join it
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

#elif TEST == 5

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

const int BOOT_BTN = 9;                    // onboard BOOT button (GPIO9), LOW when pressed
const char* SETUP_SSID = "pebble-pager";
const char* SETUP_PASS = "pebble-pager";   // placeholder; the full build should use a per-device value
const unsigned long SETUP_HOLD_MS = 5000;
const unsigned long CLEAR_HOLD_MS = 10000;
const unsigned long SETUP_TIMEOUT_MS = 5UL * 60 * 1000;
const unsigned long JOIN_TIMEOUT_MS = 15000;
const int MAX_NETWORKS = 5;

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
          "<p>2.4 GHz networks only.</p></body>";
  server.send(200, "text/html", page);
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
  Serial.println("Setup mode: join Wi-Fi \"pebble-pager\" on the phone (password in the sketch).");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_SSID, SETUP_PASS);
  dns.start(53, "*", WiFi.softAPIP());   // captive portal: every name points here
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
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
