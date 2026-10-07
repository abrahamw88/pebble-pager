// Pebble pager firmware. Behavior is specified in README.md.
//
// Tabs, in the order they build on each other:
//   config.h   pins, values to tune, saved settings, serial output and console
//   wifi_manager.h  Wi-Fi connection manager
//   ntfy.h     post to topics, read this device's inbox
//   pulse.h    the message format
//   messages.h sending, receiving, receipts, the outbox and the unread list
//   ota.h      remote firmware updates
//   portal.h   setup page
//   tests.h    hardware checks, built instead of the firmware with -DTEST=n
//
// Built so far: settings, setup page, Wi-Fi manager, messages between devices with receipts, remote updates.
// Still to build: buttons and recording, ring, motor, battery, sleep. Until then the serial console stands in:
// "send pink 200 200 500" queues a message as a recording would, and "play" plays the next unread one.
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
#include "pulse.h"
#include "messages.h"
#include "ota.h"
#include "portal.h"

#if TEST
#include "tests.h"
#else

bool wantUpdate = false;   // the phone sent "update"

// One message from this device's inbox: a command, a receipt, or a pulse message. Anything else is ignored.
void onMessage(const NtfyMessage& m) {
  String text = m.text;
  text.trim();
  String lower = text;
  lower.toLowerCase();
  sayf("Inbox: \"%s\"\n", text.c_str());
  if (lower == "update") { wantUpdate = true; return; }
  if (lower.startsWith("received ")) { messageReceipt(text.substring(9)); return; }
#ifdef WIFI_TEST   // simulate trouble, to test the Wi-Fi manager from the phone
  if (lower == "drop") WiFi.disconnect();                                              // connection lost
  if (lower == "nointernet") wIoFails = WIFI_IO_FAIL_LIMIT;                            // joined but no internet
  if (lower == "hide") { wTestHideUntil = (millis() + 60000) | 1; WiFi.disconnect(); } // out of range for 60 s
  if (lower == "roam") { wTestRoam = true; wRssiAt = millis() - WIFI_RSSI_CHECK_MS; }  // weak signal, better one nearby
#endif
  if (!pulseLooksLike(lower)) return;
  Pulse p;
  if (const char* problem = pulseParse(lower, p)) ntfySay("can't read \"" + text + "\": " + problem);
  else messageArrived(m, p);
}

// Console commands that stand in for the buttons until they are wired.
void onConsole(const String& line) {
  if (line.startsWith("send ")) {
    const char* problem = messageQueue(line.substring(5));
    sayf("%s\n", problem ? problem : "Queued");
  } else if (line == "play") {
    if (!messagePlay()) sayln("Nothing to play");
  } else {
    consoleSettings(line);
    sayf("unread=%d outbox=%d reports=%d\n", listCount(UNREAD), listCount(OUTBOX), listCount(NOTES));
  }
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
  String line;
  if (consoleRead(line)) onConsole(line);

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
    if (millis() - lastPoll >= (messagesAwaiting() ? RECEIPT_POLL_MS : INBOX_POLL_MS)) { lastPoll = millis(); ntfyPoll(onMessage); }
    if (wantUpdate) { wantUpdate = false; lastCheck = millis(); updateCheck(true); }
    if (checkNow || millis() - lastCheck >= DAILY_CHECK_MS) { lastCheck = millis(); updateCheck(false); }
  } else if (press == PRESS_TAP) {
    wifiKick();   // offline: look for a network now
  }
  messagesTick();
  ntfyFlush();
  delay(portalOn ? 2 : 20);   // answer the phone quickly while the setup page is open
}

#endif
