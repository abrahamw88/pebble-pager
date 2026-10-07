// Pebble pager firmware. Behavior is specified in README.md.
//
// Tabs, in the order they build on each other:
//   config.h   pins, values to tune, saved settings, serial output and console
//   wifi_manager.h  Wi-Fi connection manager
//   ntfy.h     post to topics, read this device's inbox
//   ota.h      remote firmware updates
//   portal.h   setup page
//   tests.h    hardware checks, built instead of the firmware with -DTEST=n
//
// Built so far: settings, setup page, Wi-Fi manager, inbox with the "update" command, remote updates.
// Still to build: buttons and recording, pulse messages and receipts, ring, motor, battery, sleep.
//
// Build flags (--build-property compiler.cpp.extra_flags="..."):
//   -DFW_VERSION=N       version of this build, compared with the release's manifest (default 1)
//   -DTEST=n             1 board check, 2 buttons + ring + motor, 4 battery, 6 wake-and-check timing
//   -DWIFI_TEST          adds inbox commands that simulate Wi-Fi trouble: drop, nointernet, hide, roam
//   -DFW_CRASH           an image that crashes at startup, to test update rollback
//   -DOTA_LOCAL_IP=a.b.c.d   take updates from a local web server on port 8000 instead of GitHub
//   -DDAILY_CHECK_MS=n   shorter update check interval, for testing

#ifndef TEST
#define TEST 0
#endif

#include "config.h"
#include "wifi_manager.h"
#include "ntfy.h"
#include "ota.h"
#include "portal.h"

#if TEST
#include "tests.h"
#else

bool wantUpdate = false;   // the phone sent "update"

// One message from this device's inbox. Only known commands do anything.
void onMessage(const String& raw) {
  String msg = raw;
  msg.trim();
  msg.toLowerCase();
  sayf("Inbox: \"%s\"\n", msg.c_str());
  if (msg == "update") wantUpdate = true;
#ifdef WIFI_TEST   // simulate trouble, to test the Wi-Fi manager from the phone
  if (msg == "drop") WiFi.disconnect();                                              // connection lost
  if (msg == "nointernet") wIoFails = WIFI_IO_FAIL_LIMIT;                            // joined but no internet
  if (msg == "hide") { wTestHideUntil = (millis() + 60000) | 1; WiFi.disconnect(); } // out of range for 60 s
  if (msg == "roam") { wTestRoam = true; wRssiAt = millis() - WIFI_RSSI_CHECK_MS; }  // weak signal, better one nearby
#endif
}

// The button: a tap, or a hold that acts on release (5 s = setup page, 10 s = forget Wi-Fi networks).
enum { PRESS_NONE, PRESS_TAP, PRESS_SETUP, PRESS_CLEAR };
int buttonTick() {
  static unsigned long downAt = 0;
  static int told = PRESS_NONE;
  if (digitalRead(BOOT_BTN) == LOW) {
    if (!downAt) downAt = millis() | 1;
    unsigned long held = millis() - downAt;
    int now = held >= CLEAR_HOLD_MS ? PRESS_CLEAR : held >= SETUP_HOLD_MS ? PRESS_SETUP : PRESS_NONE;
    if (now != told) { told = now; sayln(now == PRESS_CLEAR ? "Release to forget Wi-Fi networks" : "Release for setup"); }
    return PRESS_NONE;
  }
  if (!downAt) return PRESS_NONE;
  downAt = 0;
  int press = told == PRESS_NONE ? PRESS_TAP : told;
  told = PRESS_NONE;
  return press;
}

void setup() {
  Serial.begin(115200);
  settingsBegin();
  pinMode(BOOT_BTN, INPUT_PULLUP);
  sayf("\n== Pebble v%d ==\n", FW_VERSION);
  updateBoot();
  wifiBegin();
  if (savedCount() == 0) sayln("No saved Wi-Fi: hold the button 5 s for setup");
  if (!ntfyReady()) sayln("No topic base: set it on the setup page");
}

void loop() {
  static unsigned long lastPoll = 0, lastCheck = 0;
  consoleTick();

  int press = buttonTick();
  if (press == PRESS_SETUP && !portalOn) { wifiPause(); startPortal(); }
  if (press == PRESS_CLEAR) { clearNetworks(); wifiListChanged(); wifiRescanNow(); sayln("Wi-Fi networks forgotten"); }

  bool wasPortal = portalOn;
  if (portalTick()) wifiAdopt();                       // a network was joined through the setup page
  else if (wasPortal && !portalOn) wifiListChanged();  // closed without a join: networks may have been deleted
  wifiTick();

  bool checkNow = press == PRESS_TAP;
  if (wifiJustConnected()) {
    sayf("IP %s\n", WiFi.localIP().toString().c_str());
    checkNow = true;   // also proves the internet works, and validates a freshly updated image
    lastPoll = millis();
#ifdef WIFI_TEST
    ntfySay("online, network " + String(wCur + 1) + " of " + String(savedCount()) + ", " + String(WiFi.RSSI()) + " dBm");
#endif
  }

  if (wifiOnline()) {
    if (millis() - lastPoll >= INBOX_POLL_MS) { lastPoll = millis(); ntfyPoll(onMessage); }
    if (wantUpdate) { wantUpdate = false; lastCheck = millis(); updateCheck(true); }
    if (checkNow || millis() - lastCheck >= DAILY_CHECK_MS) { lastCheck = millis(); updateCheck(false); }
  } else if (press == PRESS_TAP) {
    wifiKick();   // offline: look for a network now
  }
  delay(portalOn ? 2 : 20);   // answer the phone quickly while the setup page is open
}

#endif
