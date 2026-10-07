// Wi-Fi connection manager.
#ifndef PEBBLE_WIFI_MANAGER_H
#define PEBBLE_WIFI_MANAGER_H
#include <WiFi.h>
#include "config.h"
#include "power.h"

// Keeps the device on the best saved network without ever blocking loop():
//   scan in the background -> join the strongest saved access point by its exact address -> stay online.
//   After a sleep it skips the scan: it rejoins the same access point with the same IP settings (the fast join),
//   and falls back to a scan if that fails, the signal has been weak, or the IP lease is due for renewal.
//   A dropout gets a short grace period, then a rescan. Nothing in range: retry with a growing wait, up to 5 min.
//   Callers report each internet request with wifiIoResult(); repeated failures (a login page, a dead router)
//   mark the network bad for a while and move to the next one. A weak signal triggers a rescan and a move
//   only to a clearly stronger access point.
// What must outlive a sleep is kept in RTC memory; times that must outlive it use clockSeconds().
const unsigned long WIFI_SCAN_TIMEOUT_MS = 10000;
const unsigned long WIFI_SETTLE_MS = 300;                    // radio settles after a disconnect before scanning
const int WIFI_SCAN_FAIL_LIMIT = 5;                          // failed scans retried quickly before backing off
const unsigned long WIFI_JOIN_TIMEOUT_MS = 6000;             // per access point
const unsigned long WIFI_FAST_TIMEOUT_MS = 4000;             // a fast join that takes longer has failed
const unsigned long WIFI_LEASE_REFRESH_S = 3600;             // do a full join this often, so the IP lease stays valid
const unsigned long WIFI_GRACE_MS = 5000;                    // let a brief dropout heal itself before rescanning
const unsigned long WIFI_RETRY_MIN_MS = 10000;               // first retry after finding nothing
const unsigned long WIFI_RETRY_MAX_MS = 5UL * 60 * 1000;     // away from every saved network: look every 5 min
const int WIFI_IO_FAIL_LIMIT = 3;                            // failed internet requests in a row before moving on
const unsigned long WIFI_BAD_COOLDOWN_S = 10 * 60;           // how long a network without internet is avoided
const unsigned long WIFI_RSSI_CHECK_MS = 30000;
const int WIFI_WEAK_DBM = -78;                               // below this for WIFI_WEAK_COUNT checks: look for better
const int WIFI_WEAK_COUNT = 3;
const int WIFI_ROAM_GAIN_DB = 10;                            // move only if the other access point is this much stronger

enum { W_IDLE, W_SCAN, W_JOIN, W_ONLINE };
int wState = W_IDLE, wCur = -1, wCandN = 0, wCandI = 0, wScanFails = 0;
bool wRoam = false, wNewConn = false, wFast = false, wWasFast = false;   // wWasFast: the current connection came from a fast join
unsigned long wAt = 0, wNextScan = 0, wLostAt = 0, wRssiAt = 0;
int wCand[MAX_NETWORKS], wCandRssi[MAX_NETWORKS], wCandCh[MAX_NETWORKS];   // saved networks in range, strongest first
uint8_t wCandBssid[MAX_NETWORKS][6];

// Kept through deep sleep (reset by a power cut or restart).
RTC_DATA_ATTR int wIoFails = 0, wWeak = 0;
RTC_DATA_ATTR unsigned long wRetry = WIFI_RETRY_MIN_MS;
RTC_DATA_ATTR unsigned long wBadUntil[MAX_NETWORKS];         // clockSeconds() until which saved network i is avoided; 0 = not avoided
RTC_DATA_ATTR struct {
  bool valid;
  int network, channel;                                      // which saved network, and its access point
  uint8_t bssid[6];
  uint32_t ip, gateway, mask, dns;
  unsigned long at;                                          // clockSeconds() of the full join it came from
} wLast;
RTC_DATA_ATTR unsigned long wFastJoins = 0, wFullJoins = 0, wFastFails = 0;   // counts, for the status report

#ifdef WIFI_TEST   // test hooks, driven by ntfy commands (see the main tab)
RTC_DATA_ATTR unsigned long wTestHideUntil = 0;   // pretend no network is in range until this clockSeconds() (0 = off)
bool wTestRoam = false;             // pretend the signal is weak and any other access point is better
#endif

bool wifiDue(unsigned long t) { return (long)(millis() - t) >= 0; }   // safe across millis() rollover
bool wifiBad(int i) {   // still inside its no-internet cooldown?
  if (wBadUntil[i] && clockSeconds() >= wBadUntil[i]) wBadUntil[i] = 0;
  return wBadUntil[i] != 0;
}
void wifiListChanged() {   // saved networks were added or removed: what was remembered about them is void
  memset(wBadUntil, 0, sizeof(wBadUntil));
  wLast.valid = false;
  wCur = -1;
}
String savedSsid(int i) { return prefs.getString(("s" + String(i)).c_str(), ""); }
String savedPass(int i) { return prefs.getString(("p" + String(i)).c_str(), ""); }

void wifiOnlineNow(int network) {
  wCur = network;
  wState = W_ONLINE;
  wNewConn = true;
  wLostAt = 0;
  wRetry = WIFI_RETRY_MIN_MS;
  wRssiAt = millis();
}

void wifiDhcp() { WiFi.config(IPAddress(), IPAddress(), IPAddress()); }   // forget fixed IP settings

// Call once at start-up. After a sleep this starts the fast join straight away.
void wifiBegin() {
  WiFi.persistent(false);   // the saved list lives in our own settings, not the Wi-Fi driver's
  WiFi.mode(WIFI_STA);
  wNextScan = millis();
  bool fresh = clockSeconds() - wLast.at < WIFI_LEASE_REFRESH_S;
  // (A network marked bad is not excluded here: marking it clears wLast, so a remembered bad one was rejoined
  // on purpose, because nothing better was in range.)
  if (!wLast.valid || !fresh || wLast.network >= savedCount() || wWeak >= WIFI_WEAK_COUNT) {
    wLast.valid = false;
    wWeak = 0;
    return;
  }
  WiFi.config(IPAddress(wLast.ip), IPAddress(wLast.gateway), IPAddress(wLast.mask), IPAddress(wLast.dns));
  WiFi.begin(savedSsid(wLast.network).c_str(), savedPass(wLast.network).c_str(), wLast.channel, wLast.bssid);
  wFast = true;
  wState = W_JOIN;
  wAt = millis();
}

bool wifiOnline() { return wState == W_ONLINE && WiFi.status() == WL_CONNECTED; }
bool wifiJustConnected() { bool b = wNewConn; wNewConn = false; return b; }   // true once per new connection
void wifiIoResult(bool ok) { wIoFails = ok ? 0 : wIoFails + 1; }              // call after every internet request
// Nothing is in progress and nothing is about to start: online with no pending verdict on this network, or offline
// and waiting for the next look. (Too many failed requests must be acted on by wifiTick() before any sleep.)
bool wifiSettled() {
  if (wState == W_ONLINE) return wIoFails < WIFI_IO_FAIL_LIMIT;
  return wState == W_IDLE && (savedCount() == 0 || !wifiDue(wNextScan));
}
unsigned long wifiRetryMs() { return wRetry; }                                // how long to wait before looking again
void wifiKick() { if (wState == W_IDLE) { wRetry = WIFI_RETRY_MIN_MS; wNextScan = millis(); } }   // look now (button press)
void wifiPause() {   // before the setup portal scans: stop any search in progress
  if (wState != W_SCAN && wState != W_JOIN) return;
  if (wState == W_JOIN) WiFi.disconnect();
  WiFi.scanDelete();
  wFast = false;
  wState = W_IDLE;
}
void wifiAdopt() {   // the setup portal joined a network itself (it is saved as network 0)
  wifiListChanged();
  wIoFails = wWeak = 0;
  if (WiFi.status() == WL_CONNECTED) wifiOnlineNow(0);
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
  wifiDhcp();
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
      if (wTestHideUntil && clockSeconds() < wTestHideUntil) found = 0;
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
        if (wFast) {
          wFastJoins++;
          if (wakeReason == WAKE_TIMER) statJoinMs += millis() - wAt;
          wWeak = WiFi.RSSI() < WIFI_WEAK_DBM ? wWeak + 1 : 0;   // weak on several wakes in a row: scan next time
          wifiOnlineNow(wLast.network);
        } else {   // a full join: remember it for the fast joins that follow
          wFullJoins++;
          wLast.valid = true;
          wLast.network = wCand[wCandI];
          wLast.channel = WiFi.channel();
          memcpy(wLast.bssid, WiFi.BSSID(), 6);
          wLast.ip = (uint32_t)WiFi.localIP();
          wLast.gateway = (uint32_t)WiFi.gatewayIP();
          wLast.mask = (uint32_t)WiFi.subnetMask();
          wLast.dns = (uint32_t)WiFi.dnsIP();
          wLast.at = clockSeconds();
          wIoFails = wWeak = 0;
          wifiOnlineNow(wCand[wCandI]);
        }
        sayf("Wi-Fi online%s (%d dBm, %lu ms)\n", wFast ? ", fast" : "", WiFi.RSSI(), millis() - wAt);
        wWasFast = wFast;
        wFast = false;
      } else if (wFast && millis() - wAt >= WIFI_FAST_TIMEOUT_MS) {   // the remembered access point is not answering
        sayln("Wi-Fi: fast join failed");
        wFastFails++;
        wFast = false;
        wLast.valid = false;
        wifiRescanNow();
      } else if (!wFast && millis() - wAt >= WIFI_JOIN_TIMEOUT_MS) {
        WiFi.disconnect();
        if (++wCandI < wCandN) wifiStartJoin();
        else { sayln("Wi-Fi: join failed"); wifiRetryLater(); }
      }
      break;

    case W_ONLINE:
      if (WiFi.status() != WL_CONNECTED) {
        if (!wLostAt) { wLostAt = millis() | 1; sayln("Wi-Fi lost"); }
        else if (millis() - wLostAt >= WIFI_GRACE_MS) { wLostAt = 0; wLast.valid = false; wifiRescanNow(); }
        break;
      }
      wLostAt = 0;
      if (wIoFails >= WIFI_IO_FAIL_LIMIT) {
        wIoFails = 0;
        if (wCur >= 0 && !wifiBad(wCur)) {   // not already marked: avoid it for a while and look elsewhere
          sayln("Wi-Fi: no internet here, trying others");
          wBadUntil[wCur] = clockSeconds() + WIFI_BAD_COOLDOWN_S;
          wLast.valid = false;
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

#endif
