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
//      Tap the onboard BOOT button, post \"update\" to the device topic (<base>-a), or wait for the daily check. It also runs the Wi-Fi setup page, which opens only when BOOT is held 5 s and released, and keeps the device on the best saved network (see the Wi-Fi connection manager). Build helpers: -DFW_VERSION=N (default 1), -DFW_CRASH
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

// Libraries are included up here so the Arduino-generated function prototypes (which now come after the
// shared helpers below) can see every type they use.
#if TEST == 0 || TEST == 3 || TEST == 5 || TEST == 6 || TEST == 7
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#endif
#if TEST == 5 || TEST == 7
#include <WebServer.h>
#include <DNSServer.h>
#endif
#if TEST == 6 || TEST == 7
#include "esp_timer.h"
#endif
#if TEST == 7
#include <Update.h>
#include "esp_ota_ops.h"
#include "mbedtls/sha256.h"
#endif
#if TEST == 2 || TEST == 4
#include <Adafruit_NeoPixel.h>
#endif

// Device name and primary color: saved in settings, set on the Wi-Fi setup page (test 5), and used in
// every ntfy and serial message. Unset values fall back to the defaults below.
#include <Preferences.h>
#include <stdarg.h>

const char* DEFAULT_DEVICE_NAME = "Eliana";
const char* DEFAULT_DEVICE_COLOR = "Pink";
const int NAME_MAX_LEN = 20;
// Dropdown order on the setup page. The RGB values are for the ring in the full build.
const int COLOR_COUNT = 8;
const char* COLOR_NAMES[COLOR_COUNT] = {"Pink", "Blue", "Green", "Purple", "Orange", "Teal", "Yellow", "Red"};
const uint32_t COLOR_RGB[COLOR_COUNT] = {0xF29BB5, 0x7FB2F0, 0x6CC795, 0xA98BE0, 0xF08A4B, 0x5CC9C0, 0xF2D45C, 0xE8736B};

String deviceName = DEFAULT_DEVICE_NAME;
String deviceColor = DEFAULT_DEVICE_COLOR;

uint32_t colorRgb(const String& name) {
  for (int i = 0; i < COLOR_COUNT; i++)
    if (name == COLOR_NAMES[i]) return COLOR_RGB[i];
  return COLOR_RGB[0];
}

// Letters, digits, space, hyphen, underscore and dot only, trimmed and cut to NAME_MAX_LEN.
String cleanName(const String& raw) {
  String out;
  for (unsigned i = 0; i < raw.length() && (int)out.length() < NAME_MAX_LEN; i++) {
    char c = raw[i];
    if (isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_' || c == '.') out += c;
  }
  out.trim();
  return out.length() ? out : String(DEFAULT_DEVICE_NAME);
}

void loadDevice() {
  Preferences prefs;
  prefs.begin("pebble", true);
  deviceName = cleanName(prefs.getString("name", DEFAULT_DEVICE_NAME));
  String c = prefs.getString("color", DEFAULT_DEVICE_COLOR);
  prefs.end();
  deviceColor = DEFAULT_DEVICE_COLOR;
  for (int i = 0; i < COLOR_COUNT; i++)
    if (c == COLOR_NAMES[i]) deviceColor = c;
}

void saveDevice(const String& name, const String& color) {
  Preferences prefs;
  prefs.begin("pebble", false);
  prefs.putString("name", cleanName(name));
  prefs.putString("color", color);
  prefs.end();
  loadDevice();
}

// Serial output with the device name in front: "Eliana: ...". Indented lines and dots stay plain.
void sayf(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  const char* m = buf;
  while (*m == '\n') { Serial.print('\n'); m++; }
  if (*m != ' ' && *m != '.' && *m != 0) Serial.printf("%s: ", deviceName.c_str());
  Serial.print(m);
}
void sayln(const char* msg) { sayf("%s\n", msg); }

#if TEST == 5 || TEST == 7
// ---- Wi-Fi setup portal, shared by the setup test (5) and the update test (7) ----
const char* SETUP_SSID = "pebblepager";
const char* SETUP_PASS = "pebblepager";   // placeholder; the full build should use a per-device value
const unsigned long SETUP_HOLD_MS = 5000;
const unsigned long CLEAR_HOLD_MS = 10000;
const unsigned long SETUP_TIMEOUT_MS = 5UL * 60 * 1000;
const unsigned long PORTAL_JOIN_TIMEOUT_MS = 15000;
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

// Forget every saved network but keep the device name, color and update flags.
void clearNetworks() {
  int n = savedCount();
  for (int i = 0; i < n; i++) {
    prefs.remove(("s" + String(i)).c_str());
    prefs.remove(("p" + String(i)).c_str());
  }
  prefs.putInt("n", 0);
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
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < PORTAL_JOIN_TIMEOUT_MS) delay(250);
  return WiFi.status() == WL_CONNECTED;
}

// Join whichever saved network is in range with the strongest signal.
// Networks seen by the last scan, as <option> tags. The page uses this copy, because a scan while the hotspot
// is up makes the radio hop channels for a few seconds and can drop the phone's connection.
String scanHtml;

void refreshScan() {
  int found = WiFi.scanNetworks();
  scanHtml = "";
  for (int i = 0; i < found; i++) {
    String name = WiFi.SSID(i);
    if (name.length() == 0 || scanHtml.indexOf("<option>" + esc(name) + "</option>") >= 0) continue;   // hidden or repeated
    scanHtml += "<option>" + esc(name) + "</option>";
  }
  sayf("Scan: %d networks\n", found);
}

void handleRescan() {
  refreshScan();
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handleRoot() {
  sayf("Portal: page requested\n");
  String page = "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
                "<title>Pebble Wi-Fi</title><body style='font-family:sans-serif;max-width:28em;margin:1em auto;padding:0 1em'>"
                "<h2>Pebble setup</h2>"
                "<form method=post action=/device><h3>Device</h3>"
                "<p><label>Name<br><input name=name value=\"" + esc(deviceName) + "\" maxlength=" + String(NAME_MAX_LEN) +
                " style='font-size:1.1em;width:100%'></label></p>"
                "<p><label>Color<br><select name=color style='font-size:1.1em;width:100%'>";
  for (int i = 0; i < COLOR_COUNT; i++)
    page += String("<option") + (deviceColor == COLOR_NAMES[i] ? " selected" : "") + ">" + COLOR_NAMES[i] + "</option>";
  page += "</select></label></p>"
          "<p><button style='font-size:1.1em;padding:.6em 1.2em'>Save name and color</button></p></form>"
          "<form method=post action=/save><h3>Wi-Fi</h3>"
          "<p><label>Network<br><select name=ssid style='font-size:1.1em;width:100%'>";
  page += scanHtml;
  page += "</select></label></p><p><label>Password<br><input name=pass type=password "
          "style='font-size:1.1em;width:100%'></label></p>"
          "<p><button style='font-size:1.1em;padding:.6em 1.2em'>Save and join</button></p></form>"
          "<p>2.4 GHz networks only. <a href=/rescan>Rescan</a> (the page pauses for a few seconds).</p>";
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
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", page);
}

void handleDevice() {
  String color = server.arg("color");
  saveDevice(server.arg("name"), color);
  sayf("Device saved: %s, %s\n", deviceName.c_str(), deviceColor.c_str());
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
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
  sayf("Portal %s \"%s\"\n", ok ? "joined" : "failed", ssid.c_str());
  joinedViaPortal = ok;
}

void startPortal() {
  sayf("Setup: join \"%s\"\n", SETUP_SSID);
  WiFi.mode(WIFI_AP_STA);
  refreshScan();   // before the hotspot starts, so the scan cannot disturb a connected phone
  WiFi.softAP(SETUP_SSID, SETUP_PASS);
  WiFi.AP.enableDhcpCaptivePortal();   // tells phones where the setup page is, so it opens by itself
  dns.start(53, "*", WiFi.softAPIP());   // captive portal: every name points here
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/device", HTTP_POST, handleDevice);
  server.on("/rescan", handleRescan);
  server.onNotFound([]() {
    sayf("Portal: redirect %s\n", server.uri().c_str());
    server.sendHeader("Cache-Control", "no-store");
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
  sayln("Setup ended");
}


// Call from loop() while the setup portal may be open. Returns true once, when a network was joined through the page
// (the portal is closed by then); also closes the portal after SETUP_TIMEOUT_MS.
bool portalTick() {
  if (!portalOn) return false;
  dns.processNextRequest();
  server.handleClient();
  if (joinedViaPortal) {
    delay(3000);   // let the phone show its confirmation page
    server.handleClient();
    stopPortal();
    return true;
  }
  if (millis() - portalStart > SETUP_TIMEOUT_MS) stopPortal();
  return false;
}

#endif

#if TEST == 7
// ---- Wi-Fi connection manager ----
// Keeps the device on the best saved network without ever blocking loop():
//   scan in the background -> join the strongest saved access point by its exact address -> stay online.
//   A dropout gets a short grace period, then a rescan. Nothing in range: retry with a growing wait, up to 5 min.
//   Callers report each internet request with wifiIoResult(); repeated failures (a login page, a dead router)
//   mark the network bad for a while and move to the next one. A weak signal triggers a rescan and a move
//   only to a clearly stronger access point.
// Needs: prefs (open on "pebble"), savedCount(), portalOn, sayf()/sayln(), MAX_NETWORKS.
const unsigned long WIFI_SCAN_TIMEOUT_MS = 10000;
const unsigned long WIFI_SETTLE_MS = 300;                    // radio settles after a disconnect before scanning
const int WIFI_SCAN_FAIL_LIMIT = 5;                          // failed scans retried quickly before backing off
const unsigned long WIFI_JOIN_TIMEOUT_MS = 6000;             // per access point
const unsigned long WIFI_GRACE_MS = 5000;                    // let a brief dropout heal itself before rescanning
const unsigned long WIFI_RETRY_MIN_MS = 10000;               // first rescan after finding nothing
const unsigned long WIFI_RETRY_MAX_MS = 5UL * 60 * 1000;     // away from every saved network: scan every 5 min
const int WIFI_IO_FAIL_LIMIT = 3;                            // failed internet requests in a row before moving on
const unsigned long WIFI_BAD_COOLDOWN_MS = 10UL * 60 * 1000; // how long a network without internet is avoided
const unsigned long WIFI_RSSI_CHECK_MS = 30000;
const int WIFI_WEAK_DBM = -78;                               // below this for WIFI_WEAK_COUNT checks: look for better
const int WIFI_WEAK_COUNT = 3;
const int WIFI_ROAM_GAIN_DB = 10;                            // move only if the other access point is this much stronger

enum { W_IDLE, W_SCAN, W_JOIN, W_ONLINE };
int wState = W_IDLE, wCur = -1, wCandN = 0, wCandI = 0, wIoFails = 0, wWeak = 0, wScanFails = 0;
bool wRoam = false, wNewConn = false;
unsigned long wAt = 0, wNextScan = 0, wRetry = WIFI_RETRY_MIN_MS, wLostAt = 0, wRssiAt = 0;
unsigned long wBadUntil[MAX_NETWORKS];                       // millis() until which saved network i is avoided; 0 = not avoided
int wCand[MAX_NETWORKS], wCandRssi[MAX_NETWORKS], wCandCh[MAX_NETWORKS];   // saved networks in range, strongest first
uint8_t wCandBssid[MAX_NETWORKS][6];
#ifdef WIFI_TEST   // test hooks, driven by ntfy commands in the update test
unsigned long wTestHideUntil = 0;   // pretend no network is in range until then (0 = off)
bool wTestRoam = false;             // pretend the signal is weak and any other access point is better
#endif

bool wifiDue(unsigned long t) { return (long)(millis() - t) >= 0; }   // safe across millis() rollover
bool wifiBad(int i) {   // still inside its no-internet cooldown?
  if (wBadUntil[i] && wifiDue(wBadUntil[i])) wBadUntil[i] = 0;
  return wBadUntil[i] != 0;
}
void wifiListChanged() { memset(wBadUntil, 0, sizeof(wBadUntil)); wCur = -1; }   // saved networks were added or removed
String savedSsid(int i) { return prefs.getString(("s" + String(i)).c_str(), ""); }
String savedPass(int i) { return prefs.getString(("p" + String(i)).c_str(), ""); }

void wifiBegin() {
  WiFi.persistent(false);   // the saved list lives in our own settings, not the Wi-Fi driver's
  WiFi.mode(WIFI_STA);
  wNextScan = millis();
}
bool wifiOnline() { return wState == W_ONLINE && WiFi.status() == WL_CONNECTED; }
bool wifiJustConnected() { bool b = wNewConn; wNewConn = false; return b; }   // true once per new connection
void wifiIoResult(bool ok) { wIoFails = ok ? 0 : wIoFails + 1; }              // call after every internet request
void wifiKick() { if (wState == W_IDLE) { wRetry = WIFI_RETRY_MIN_MS; wNextScan = millis(); } }   // look now (button press)
void wifiPause() {   // before the setup portal scans: stop any search in progress
  if (wState != W_SCAN && wState != W_JOIN) return;
  if (wState == W_JOIN) WiFi.disconnect();
  WiFi.scanDelete();
  wState = W_IDLE;
}
void wifiAdopt() {   // the setup portal joined a network itself (it is saved as network 0)
  wifiListChanged();
  if (WiFi.status() != WL_CONNECTED) return;
  wCur = 0; wState = W_ONLINE; wNewConn = true; wIoFails = wWeak = 0; wLostAt = 0;
  wRetry = WIFI_RETRY_MIN_MS; wRssiAt = millis();
}

void wifiRetryLater() {
  wState = W_IDLE;
  wNextScan = millis() + wRetry;
  wRetry = min(wRetry * 2, WIFI_RETRY_MAX_MS);
}
void wifiRescanNow() { WiFi.disconnect(); wState = W_IDLE; wNextScan = millis() + WIFI_SETTLE_MS; }
void wifiStartScan(bool roam) {
  wRoam = roam;
  WiFi.scanDelete();
  WiFi.scanNetworks(true);   // true = in the background
  wState = W_SCAN;
  wAt = millis();
}
void wifiStartJoin() {
  int i = wCand[wCandI];
  sayf("Wi-Fi join %d of %d (%d dBm)\n", i + 1, savedCount(), wCandRssi[wCandI]);
  WiFi.begin(savedSsid(i).c_str(), savedPass(i).c_str(), wCandCh[wCandI], wCandBssid[wCandI]);
  wState = W_JOIN;
  wAt = millis();
}

// Fill the candidate list from a finished scan: each saved network's strongest access point, strongest first.
void wifiPickCandidates(int found, bool allowBad) {
  wCandN = 0;
  int n = savedCount();
  for (int i = 0; i < n && i < MAX_NETWORKS; i++) {
    if (!allowBad && wifiBad(i)) continue;
    String ssid = savedSsid(i);
    int best = -1;
    for (int j = 0; j < found; j++)
      if (WiFi.SSID(j) == ssid && (best < 0 || WiFi.RSSI(j) > WiFi.RSSI(best))) best = j;
    if (best < 0) continue;
    int k = wCandN++;
    for (; k > 0 && wCandRssi[k - 1] < WiFi.RSSI(best); k--) {   // insertion sort, strongest first
      wCand[k] = wCand[k - 1]; wCandRssi[k] = wCandRssi[k - 1]; wCandCh[k] = wCandCh[k - 1];
      memcpy(wCandBssid[k], wCandBssid[k - 1], 6);
    }
    wCand[k] = i; wCandRssi[k] = WiFi.RSSI(best); wCandCh[k] = WiFi.channel(best);
    memcpy(wCandBssid[k], WiFi.BSSID(best), 6);
  }
}

void wifiTick() {   // call every loop(); returns within a few milliseconds
  if (portalOn) return;
  switch (wState) {
    case W_IDLE:
      if (savedCount() > 0 && wifiDue(wNextScan)) wifiStartScan(false);
      break;

    case W_SCAN: {
      int found = WiFi.scanComplete();
      if (found == WIFI_SCAN_RUNNING && millis() - wAt < WIFI_SCAN_TIMEOUT_MS) break;
      if (found < 0 && ++wScanFails < WIFI_SCAN_FAIL_LIMIT) {   // the scan itself failed: not the same as nothing in range
        WiFi.scanDelete();
        if (wRoam && WiFi.status() == WL_CONNECTED) { wState = W_ONLINE; break; }
        wState = W_IDLE;
        wNextScan = millis() + 1000;
        break;
      }
      wScanFails = 0;
      if (found < 0) found = 0;   // keeps failing: back off as if nothing were in range
#ifdef WIFI_TEST
      if (wTestHideUntil && !wifiDue(wTestHideUntil)) found = 0;
#endif
      wifiPickCandidates(found, false);
      if (wCandN == 0) wifiPickCandidates(found, true);   // a network without internet still beats none
      WiFi.scanDelete();
      wCandI = 0;
      if (wRoam && WiFi.status() == WL_CONNECTED) {   // still online: move only for a clearly stronger access point
        int gain = WIFI_ROAM_GAIN_DB;
#ifdef WIFI_TEST
        if (wTestRoam) { gain = -100; wTestRoam = false; }
#endif
        while (wCandI < wCandN && !memcmp(wCandBssid[wCandI], WiFi.BSSID(), 6)) wCandI++;   // skip where we already are
        if (wCandI >= wCandN || wCandRssi[wCandI] < WiFi.RSSI() + gain) { sayln("Wi-Fi: weak, nothing better in range"); wState = W_ONLINE; break; }
        sayln("Wi-Fi: stronger access point found");
        WiFi.disconnect();
      }
      if (wCandI >= wCandN) { sayln("Wi-Fi: no saved network in range"); wifiRetryLater(); break; }
      wifiStartJoin();
      break;
    }

    case W_JOIN:
      if (WiFi.status() == WL_CONNECTED) {
        wCur = wCand[wCandI];
        wState = W_ONLINE;
        wNewConn = true;
        wIoFails = wWeak = 0;
        wLostAt = 0;
        wRetry = WIFI_RETRY_MIN_MS;
        wRssiAt = millis();
        sayf("Wi-Fi online (%d dBm)\n", WiFi.RSSI());
      } else if (millis() - wAt >= WIFI_JOIN_TIMEOUT_MS) {
        WiFi.disconnect();
        if (++wCandI < wCandN) wifiStartJoin();
        else { sayln("Wi-Fi: join failed"); wifiRetryLater(); }
      }
      break;

    case W_ONLINE:
      if (WiFi.status() != WL_CONNECTED) {
        if (!wLostAt) { wLostAt = millis() | 1; sayln("Wi-Fi lost"); }
        else if (millis() - wLostAt >= WIFI_GRACE_MS) { wLostAt = 0; wifiRescanNow(); }
        break;
      }
      wLostAt = 0;
      if (wIoFails >= WIFI_IO_FAIL_LIMIT) {
        wIoFails = 0;
        if (wCur >= 0 && !wifiBad(wCur)) {   // not already marked: avoid it for a while and look elsewhere
          sayln("Wi-Fi: no internet here, trying others");
          wBadUntil[wCur] = (millis() + WIFI_BAD_COOLDOWN_MS) | 1;
          wifiRescanNow();
          break;
        }   // already marked and rejoined because nothing else is in range: stay, don't churn
      }
      if (millis() - wRssiAt >= WIFI_RSSI_CHECK_MS) {
        wRssiAt = millis();
        wWeak = WiFi.RSSI() < WIFI_WEAK_DBM ? wWeak + 1 : 0;
#ifdef WIFI_TEST
        if (wTestRoam) wWeak = WIFI_WEAK_COUNT;
#endif
        if (wWeak >= WIFI_WEAK_COUNT) { wWeak = 0; wifiStartScan(true); }
      }
      break;
  }
}
// ---- end Wi-Fi connection manager ----
#endif

#if TEST == 0

#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  loadDevice();
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);   // wait for the USB serial monitor
  sayln("\n== board check ==");
  sayf("Chip %s r%d x%d %d MHz\n", ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), ESP.getCpuFreqMHz());
  sayf("Flash %u KB, heap %u KB\n", ESP.getFlashChipSize() / 1024, ESP.getFreeHeap() / 1024);
  sayf("Temp %.1f C\n", temperatureRead());

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  sayf("MAC %s\n", WiFi.macAddress().c_str());   // valid only after the radio starts
  sayln("Scanning...");
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    sayln("No networks: check antenna");
  } else {
    sayf("%d networks\n", n);
    for (int i = 0; i < n; i++)
      sayf("  %-28s ch %2d  %4d dBm  %s\n", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
                    WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
  }
  sayln("done");
}

void loop() {}

#elif TEST == 1

void setup() {
  Serial.begin(115200);
  loadDevice();
}

void loop() {
  sayln("Hello from Pebble");
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
  loadDevice();
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

#elif TEST == 3

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "secrets.h"

void setup() {
  Serial.begin(115200);
  loadDevice();
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  Preferences prefs;
  prefs.begin("pebble", true);   // networks saved by test 5
  String ssid = prefs.getString("s0", "");
  String pass = prefs.getString("p0", "");
  prefs.end();
  if (ssid.length() == 0) {
    sayln("No saved Wi-Fi: run test 5");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.print("Joining");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  sayln(" connected");

  WiFiClientSecure client;
  client.setInsecure();   // test only: skips the certificate check
  HTTPClient http;
  http.begin(client, TOPIC_URL);
  http.addHeader("Title", "Pebble test");
  http.addHeader("Priority", "high");
  int code = http.POST(deviceName + " says hello");
  sayf("ntfy %d\n", code);
  http.end();
}

void loop() {}

#elif TEST == 4

#include <Adafruit_NeoPixel.h>

Adafruit_NeoPixel ring(12, RING_DATA, NEO_GRBW + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  loadDevice();
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
  sayf("Batt %.2f V\n", volts);

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
  loadDevice();
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
    sayln("No saved Wi-Fi: run test 5");
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
  sayf("\n#%d join %s %s %lums poll %d %lums awake %lums new %d%s\n", cycle, how,
                ok ? "ok" : "fail", joinMs, code, pollMs, awakeMs, fresh,
                cycle == 1 ? " (catch-up)" : "");
  if (texts.length()) Serial.println(texts);
  sayf("  msgs %d (last \"%s\") fastfail %d abort %d\n", totalMsgs, lastText, fastFails, aborts);
  sayln("  history join/poll/awake ms:");
  for (int c = max(1, cycle - HIST + 1); c <= cycle; c++)
    sayf("    %d: %u/%u/%u %c\n", c, hJoin[c % HIST], hPoll[c % HIST], hAwake[c % HIST], hHow[c % HIST]);
  sayf("  awake min/avg/max %lu/%.0f/%lu ms, ~%.2f mA, ~%.1f d\n", minAwakeMs,
                avgAwake, maxAwakeMs, avgMa, USABLE_MAH / avgMa / 24.0);

  if (stayAwake) {
    sayln("BOOT held: awake");
    return;
  }
  unsigned long sleepMs = awakeMs < CHECK_INTERVAL_MS ? CHECK_INTERVAL_MS - awakeMs : 1000;
  sayf("  sleep %lums\n", sleepMs);
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
  sayf("Time %s (%lums)\n", ok ? "ok" : "fail", millis() - t0);
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
  sayf("GET %s %d\n", url.substring(url.lastIndexOf('/') + 1).c_str(), code);
  return code == 200;
}

bool postNtfy(const char* title, const String& message) {
  WiFiClientSecure client;
  secureSetup(client);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  if (!http.begin(client, TOPIC_URL)) return false;
  http.addHeader("Title", title);
  int code = http.POST(deviceName + " " + message);   // every ntfy message starts with the device name
  http.end();
  sayf("ntfy %d: %s\n", code, message.c_str());
  return code == 200;
}

// After an update the old image records "from" and "to"; the next boot reports how it went.
void reportUpdateResult() {
  Preferences prefs;
  prefs.begin("pebble", false);
  int to = prefs.getInt("otaTo", 0), from = prefs.getInt("otaFrom", 0);
  if (to == 0) { prefs.end(); return; }
  bool ok = FW_VERSION == to;
  String msg = ok ? "updated from v" + String(from) + " to v" + String(to)
                  : "failed update to v" + String(to) + ", still on v" + String(FW_VERSION);
  if (postNtfy("Pebble", msg)) {
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
    sayf("Size %d != manifest %u\n", http.getSize(), (unsigned)size);
    http.end();
    otaReason = "bad size";
    return OTA_BAD_IMAGE;
  }
  if (!Update.begin(size)) {
    sayf("Begin failed: %s\n", Update.errorString());
    http.end();
    otaReason = "too big";
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
      sayf("Write failed: %s\n", Update.errorString());
      Update.abort();
      http.end();
      otaReason = "write failed";
      return OTA_NET_ERR;
    }
    mbedtls_sha256_update(&sha, buf, n);
    done += n;
    int pct = done * 100 / size;
    if (pct / 10 != lastPct / 10) { sayf("  %d%%\n", pct); lastPct = pct; }
  }
  http.end();
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  if (done != size) {
    sayln("Incomplete");
    Update.abort();
    otaReason = "incomplete";
    return OTA_NET_ERR;
  }
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", digest[i]);
  if (!wantSha.equalsIgnoreCase(hex)) {
    sayf("Bad SHA-256 %s\n", hex);
    Update.abort();   // nothing is installed
    otaReason = "bad hash";
    return OTA_BAD_IMAGE;
  }
  if (!Update.end(true)) {
    sayf("End failed: %s\n", Update.errorString());
    otaReason = "bad image";
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
  sayf("Running v%d, release v%d\n", FW_VERSION, remote);
  Preferences badPrefs;
  badPrefs.begin("pebble", true);
  int bad = badPrefs.getInt("otaBad", 0);
  badPrefs.end();
  if (remote == bad) { sayf("v%d failed before: skip\n", bad); checkNote = "skipped"; return false; }
  if (remote <= FW_VERSION) { sayln("Up to date"); checkNote = "current"; return false; }
  if (size == 0 || sha.length() != 64) { sayln("Bad manifest"); return false; }
  sayf("Installing v%d (%u B)\n", remote, (unsigned)size);
  int r = installUpdate(size, sha);
  if (r == OTA_NET_ERR) { sayf("v%d not finished (%s): retry later\n", remote, otaReason); return false; }
  Preferences prefs;
  prefs.begin("pebble", false);
  if (r == OTA_BAD_IMAGE) {
    prefs.putInt("otaBad", remote);   // never retried
    prefs.end();
    postNtfy("Pebble", "failed update to v" + String(remote) + ": " + otaReason + ", still on v" + String(FW_VERSION));
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
  int code = http.begin(client, url) ? http.GET() : -1;
  wifiIoResult(code == 200);
  if (code == 200) {
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
      sayf("Command: \"%s\"\n", msg.c_str());
      if (msg == "update") wantUpdate = true;   // anything else is ignored
#ifdef WIFI_TEST   // simulate trouble, to test the Wi-Fi manager from the phone
      if (msg == "drop") WiFi.disconnect();                                              // connection lost
      if (msg == "nointernet") wIoFails = WIFI_IO_FAIL_LIMIT;                            // joined but no internet
      if (msg == "hide") { wTestHideUntil = (millis() + 60000) | 1; WiFi.disconnect(); }       // out of range for 60 s
      if (msg == "roam") { wTestRoam = true; wRssiAt = millis() - WIFI_RSSI_CHECK_MS; }  // weak signal, better one nearby
#endif
    }
  }
  http.end();
  if (last.length() == 0 && prefs.getString("cmdId", "").length() == 0)
    prefs.putString("cmdId", String((unsigned long)time(nullptr)));   // empty topic: remember "now", so the first real command is not swallowed
  prefs.end();
  return wantUpdate;
}

bool imagePending = false;   // true until this boot has reached the update server (then the image is marked valid)

void validateImage() {
  esp_ota_mark_app_valid_cancel_rollback();
  imagePending = false;
  sayln("Image valid");
  reportUpdateResult();
}

// Reach the update server on the current network. Installs and reboots if there is a newer image.
// Returns true if the server answered (that proves Wi-Fi and TLS work).
bool tryServer() {
  bool reached = false;
  if (isHttps(OTA_BASE) && !syncTime()) { sayln("No time: skip update"); return false; }
  if (checkForUpdate(&reached)) {
    sayln("Installed, rebooting");
    delay(500);
    ESP.restart();
  }
  return reached;
}

void setup() {
  Serial.begin(115200);
  loadDevice();
  prefs.begin("pebble", false);   // global handle used by the setup portal
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
#ifdef FW_CRASH
  sayln("FW_CRASH: aborting");
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
    st = state == ESP_OTA_IMG_PENDING_VERIFY ? "pending" : (state == ESP_OTA_IMG_VALID ? "valid" : "other");
  sayf("\n== OTA v%d, slot %s, %s ==\n", FW_VERSION, running->label, st);

  imagePending = true;   // validated in loop(), once a network reaches the update server
  wifiBegin();
  if (savedCount() == 0) sayln("No saved Wi-Fi: hold BOOT 5s for setup");   // the setup hotspot only ever opens from the button
}

// Check for an update; tell the phone when the check was asked for, and reboot if one was installed.
void runCheck(bool asked) {
  bool reached;
  if (asked) postNtfy("Pebble", "checking for update");
  if (checkForUpdate(&reached)) {
    sayln("Update installed. Rebooting into it...");
    delay(500);
    ESP.restart();
  }
  wifiIoResult(reached);
  if (!asked) return;
  if (!reached) postNtfy("Pebble", "can't reach server, still on v" + String(FW_VERSION));
  else if (!strcmp(checkNote, "current")) postNtfy("Pebble", "up to date (v" + String(FW_VERSION) + ")");
  else if (!strcmp(checkNote, "skipped")) postNtfy("Pebble", "skipped v" + String(checkedVersion) + " (failed before), still on v" + String(FW_VERSION));
}   // installed, rejected and failed outcomes are reported by the update code itself

void loop() {
  static unsigned long lastPoll = 0, lastCheck = 0, pressedAt = 0;
  static bool told5 = false, told10 = false;
  bool tapped = false;
  if (digitalRead(BOOT_BTN) == LOW) {   // BOOT: tap = check now, hold 5 s + release = setup page, hold 10 s + release = forget networks
    if (!pressedAt) pressedAt = millis();
    unsigned long held = millis() - pressedAt;
    if (held >= SETUP_HOLD_MS && !told5) { told5 = true; sayln("5s: release=setup"); }
    if (held >= CLEAR_HOLD_MS && !told10) { told10 = true; sayln("10s: release=clear"); }
  } else if (pressedAt) {
    unsigned long held = millis() - pressedAt;
    pressedAt = 0;
    told5 = told10 = false;
    if (held >= CLEAR_HOLD_MS) { clearNetworks(); wifiListChanged(); wifiRescanNow(); sayln("Cleared networks"); }
    else if (held >= SETUP_HOLD_MS) { if (!portalOn) { wifiPause(); startPortal(); } }
    else tapped = true;
  }

  bool wasPortal = portalOn;
  if (portalTick()) wifiAdopt();                       // a network was joined through the setup page
  else if (wasPortal && !portalOn) wifiListChanged();  // closed without a join: networks may have been deleted
  wifiTick();
  if (wifiJustConnected()) {
    sayf("IP %s\n", WiFi.localIP().toString().c_str());
    bool reached = tryServer();
    wifiIoResult(reached);
    if (reached && imagePending) validateImage();
#ifdef WIFI_TEST
    postNtfy("Pebble", "online, network " + String(wCur + 1) + " of " + String(savedCount()) + ", " + String(WiFi.RSSI()) + " dBm");
#endif
    lastPoll = millis();
  }

  if (wifiOnline()) {
    if (tapped) { sayln("Check (BOOT tap)"); runCheck(false); }
    if (millis() - lastPoll >= COMMAND_POLL_MS) {
      lastPoll = millis();
      if (pollCommands()) runCheck(true);
    }
    if (millis() - lastCheck >= DAILY_CHECK_MS) {
      lastCheck = millis();
      sayln("Check (daily)");
      runCheck(false);
    }
  } else if (tapped) {
    wifiKick();   // offline: look for a network now
  }
  delay(portalOn ? 2 : 20);   // answer the phone quickly while the setup page is open
}

#elif TEST == 5

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

const int BOOT_BTN = 9;                    // onboard BOOT button (GPIO9), LOW when pressed
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
  if (best < 0) { sayln("No saved Wi-Fi in range"); return false; }
  String s = prefs.getString(("s" + String(best)).c_str(), "");
  sayf("Join \"%s\" (%d dBm)\n", s.c_str(), bestRssi);
  WiFi.begin(s.c_str(), prefs.getString(("p" + String(best)).c_str(), "").c_str());
  return waitForJoin();
}

void healthCheck() {
  WiFiClientSecure client;
  client.setInsecure();   // test only: skips the certificate check
  HTTPClient http;
  http.begin(client, "https://ntfy.sh/v1/health");
  int code = http.GET();
  sayf("ntfy health %d %s\n", code, code > 0 ? http.getString().c_str() : "");
  http.end();
}

void setup() {
  Serial.begin(115200);
  loadDevice();
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  sayln("\n== Wi-Fi setup ==");
  pinMode(BOOT_BTN, INPUT_PULLUP);
  prefs.begin("pebble", false);
  WiFi.mode(WIFI_STA);
  sayf("%d saved, color %s\n", savedCount(), deviceColor.c_str());
  if (joinSaved()) {
    sayf("IP %s\n", WiFi.localIP().toString().c_str());
    healthCheck();
  }
  sayln("BOOT 5s: setup, 10s: clear");
}

void loop() {
  static unsigned long pressedAt = 0;
  static bool told5 = false, told10 = false;
  if (digitalRead(BOOT_BTN) == LOW) {
    if (!pressedAt) pressedAt = millis();
    unsigned long held = millis() - pressedAt;
    if (held >= SETUP_HOLD_MS && !told5) { told5 = true; sayln("5s: release=setup"); }
    if (held >= CLEAR_HOLD_MS && !told10) { told10 = true; sayln("10s: release=clear"); }
  } else if (pressedAt) {
    unsigned long held = millis() - pressedAt;
    pressedAt = 0; told5 = told10 = false;
    if (held >= CLEAR_HOLD_MS) {
      clearNetworks();
      sayln("Cleared networks");
    } else if (held >= SETUP_HOLD_MS && !portalOn) {
      startPortal();
    }
  }
  if (portalTick()) {
    sayf("IP %s\n", WiFi.localIP().toString().c_str());
    healthCheck();
  }
  delay(10);
}

#endif
