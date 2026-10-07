// Pebble: full pager firmware. Behavior is specified in README.md.
//
// Built so far:
//   - Settings: device name, saved Wi-Fi networks (shared with the prototype's setup page)
//   - Wi-Fi connection manager (the same block as in firmware/prototype; keep the two in step)
// To build, in order:
//   1. Buttons: tap vs hold, recording, 2 s send pause
//   2. Ring: RGBW (NEO_GRBW) animations for every state, power switched on D5
//   3. Motor: buzz patterns
//   4. Wi-Fi setup page ("Pebble-Setup", button 2 held 5 s), from the prototype's portal
//   5. ntfy: per-device topics, poll=1 check (plan B), "delivered" and "seen" receipts
//   6. Sleep: deep sleep with 30 s wake, away mode (5 min scan), off mode
//   7. Battery: divider on D1, low-battery blink under 20%
//   8. Offline queue and remote firmware updates (from the prototype's update test)

#include <WiFi.h>
#include <Preferences.h>
#include <stdarg.h>

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
const int RING_PIXELS = 12;
const int RING_BRIGHTNESS = 30;   // of 255, keeps the 3V3 pin within budget

// Settings, stored under "pebble": name, color, n, s0..s9, p0..p9 (written by the setup page).
const char* DEFAULT_DEVICE_NAME = "Eliana";
const int MAX_NETWORKS = 10;
Preferences prefs;
String deviceName = DEFAULT_DEVICE_NAME;
bool portalOn = false;   // true while the setup page is open (the page itself is not in this sketch yet)

int savedCount() { return prefs.getInt("n", 0); }

// Serial output with the device name in front: "Eliana: ...". Indented lines stay plain.
void sayf(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  const char* m = buf;
  while (*m == '\n') { Serial.print('\n'); m++; }
  if (*m != ' ' && *m != 0) Serial.printf("%s: ", deviceName.c_str());
  Serial.print(m);
}
void sayln(const char* msg) { sayf("%s\n", msg); }

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

void setup() {
  Serial.begin(115200);
  prefs.begin("pebble", false);
  deviceName = prefs.getString("name", DEFAULT_DEVICE_NAME);
  wifiBegin();
  if (savedCount() == 0) sayln("No saved Wi-Fi: add one with the prototype's setup page");
}

void loop() {
  wifiTick();
  if (wifiJustConnected()) sayf("IP %s\n", WiFi.localIP().toString().c_str());
  delay(20);
}
