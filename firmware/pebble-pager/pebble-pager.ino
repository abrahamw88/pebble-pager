// Pebble pager firmware. Behavior is specified in README.md.
//
// Tabs, in the order they build on each other:
//   config.h   pins, values to tune, saved settings, serial output and console
//   wifi_manager.h  Wi-Fi connection manager
//   ntfy.h     post to topics, read this device's inbox
//   pulse.h    the message format
//   messages.h sending, receiving, receipts, the outbox and the unread list
//   input.h    buttons: taps, holds and recording
//   ota.h      remote firmware updates
//   portal.h   setup page
//   tests.h    hardware checks, built instead of the firmware with -DTEST=n
//
// Built so far: settings, setup page, Wi-Fi manager, messages between devices with receipts, buttons and
// recording, remote updates. Still to build: ring, motor, battery, sleep. Until the buttons are wired the
// serial console can stand in: "press 1 700 300 600" simulates presses, "boot 1" makes the onboard button act
// as button 1, "send pink 200 200 500" queues a message, "play" plays the next unread one.
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
#include "input.h"
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
  } else if (line.startsWith("press ")) {
    inputSimulate(line.substring(6).toInt() == 2, line.substring(8));
  } else if (line.startsWith("boot ")) {
    inputBootButton(line.substring(5).toInt() == 2);
  } else {
    consoleSettings(line);
    sayf("unread=%d outbox=%d reports=%d\n", listCount(UNREAD), listCount(OUTBOX), listCount(NOTES));
  }
}

// One thing the user did with the buttons.
void onInput(int event) {
  wifiKick();   // any press: if offline, look for a network now
  switch (event) {
    case EV_PLAY: if (!messagePlay()) sayln("Nothing to play"); break;
    case EV_RECORD_START:
      if (listCount(UNREAD) > 0) {   // unread messages come first: play them all, then record from scratch
        while (listCount(UNREAD) > 0) messagePlay();
        inputArm();
      }
      sayln("Recording");
      break;
    case EV_RECORD_CANCEL: sayln("Recording cancelled"); break;
    case EV_RECORD_DONE: {
      String text = pulseText(recorded);
      const char* problem = messageQueue(text);
      sayf("Recorded %s: %s\n", text.c_str(), problem ? problem : "queued");
      break;
    }
    case EV_BATTERY: sayln("Battery: not built yet"); break;
    case EV_SETUP:   // the same hold opens the setup page and closes it
      if (portalOn) stopPortal();
      else { wifiPause(); startPortal(); }
      break;
    case EV_OFF: sayln("Off: not built yet"); break;
  }
}

void setup() {
  Serial.begin(115200);
  settingsBegin();
  inputBegin();
  sayf("\n== Pebble v%d ==\n", FW_VERSION);
  updateBoot();
  wifiBegin();
  if (savedCount() == 0) sayln("No saved Wi-Fi: hold button 2 for 5 s for setup");
  if (!ntfyReady()) sayln("No topic base: set it on the setup page");
}

void loop() {
  static unsigned long lastPoll = 0, lastCheck = 0;
  static bool checkNow = false;   // look for an update at the next free moment
  String line;
  if (consoleRead(line)) onConsole(line);

  for (int event; (event = inputTick()) != EV_NONE;) onInput(event);

  bool wasPortal = portalOn;
  if (portalTick()) wifiAdopt();                       // a network was joined through the setup page
  else if (wasPortal && !portalOn) wifiListChanged();  // closed without a join: networks may have been deleted
  wifiTick();

  if (wifiJustConnected()) {
    sayf("IP %s\n", WiFi.localIP().toString().c_str());
    checkNow = true;   // also proves the internet works, and validates a freshly updated image
    lastPoll = millis();
#ifdef WIFI_TEST
    ntfySay("online, network " + String(wCur + 1) + " of " + String(savedCount()) + ", " + String(WiFi.RSSI()) + " dBm");
#endif
  }

  if (inputBusy()) {
    // someone is pressing or recording: no network work, so loop() stays quick
  } else if (wifiOnline()) {
    if (millis() - lastPoll >= (messagesAwaiting() ? RECEIPT_POLL_MS : INBOX_POLL_MS)) { lastPoll = millis(); ntfyPoll(onMessage); }
    if (wantUpdate) { wantUpdate = false; lastCheck = millis(); updateCheck(true); }
    if (checkNow || millis() - lastCheck >= DAILY_CHECK_MS) { checkNow = false; lastCheck = millis(); updateCheck(false); }
  }
  if (!inputBusy()) { messagesTick(); ntfyFlush(); }
  delay(portalOn ? 2 : 20);   // answer the phone quickly while the setup page is open
}

#endif
