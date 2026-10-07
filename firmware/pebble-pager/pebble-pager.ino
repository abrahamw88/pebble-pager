// Pebble pager firmware. Behavior is specified in README.md.
//
// Tabs, in the order they build on each other:
//   config.h   pins, values to tune, saved settings, serial output and console
//   power.h    deep sleep, wake reasons, the stall guard
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
// recording, sleep, remote updates. Still to build: ring, motor, battery.
//
// The serial console and the inbox help with testing:
//   console: show, set name|color|partner|base <value>, send <message>, play, press <1|2> <times>, boot <1|2>,
//            awake, sleep
//   inbox:   update, status (wake counts and time awake, to the phone), awake (stay awake 10 minutes), sleep
//
// Build flags (--build-property compiler.cpp.extra_flags="..."):
//   -DFW_VERSION=N       version of this build, compared with the release's manifest (default 1)
//   -DTEST=n             1 board check, 2 buttons + ring + motor, 4 battery
//   -DNO_SLEEP           never sleep (for development)
//   -DWIFI_TEST          adds inbox commands that simulate Wi-Fi trouble (drop, nointernet, hide, roam) and an
//                        "online" report to the phone after every full join
//   -DFW_CRASH           an image that crashes at startup, to test update rollback
//   -DOTA_LOCAL_IP=a.b.c.d   take updates from a local web server on port 8000 instead of GitHub
//   -DDAILY_CHECK_S=n    shorter update check interval, for testing
//   -DQUEUE_MAX_AGE_S=n  shorter age limit for queued messages, for testing

#ifndef TEST
#define TEST 0
#endif

#include "config.h"
#include "power.h"
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

// A line for the phone about how the device is doing. The time awake per check is measured, and is what decides
// battery life. The mA figure is only an estimate from it, using the assumed currents in README (Power budget).
String statusText() {
  unsigned long n = statChecks ? statChecks : 1, avg = statCheckMs / n;
  float ma = (90.0 * avg + 0.053 * (CHECK_INTERVAL_MS - avg)) / CHECK_INTERVAL_MS;   // README: Power budget
  return "status: v" + String(FW_VERSION) + ", " + String(statChecks) + " checks, awake avg " + String(avg) + " ms max " +
         String(statMaxCheckMs) + " ms (estimated " + String(ma, 1) + " mA, not measured; start " + String(statStartMs / n) + " join " +
         String(statJoinMs / n) + " inbox " + String(statPollMs / n) + "), joins " + String(wFastJoins) + " fast " +
         String(wFullJoins) + " full " + String(wFastFails) + " failed, " + String(WiFi.RSSI()) + " dBm, " +
         String(statButtonWakes) + " button wakes, " + String(statStalls) + " stalls, unread " + String(listCount(UNREAD)) +
         " outbox " + String(listCount(OUTBOX));
}

// One message from this device's inbox: a command, a receipt, or a pulse message. Anything else is ignored.
void onMessage(const NtfyMessage& m) {
  String text = m.text;
  text.trim();
  String lower = text;
  lower.toLowerCase();
  sayf("Inbox: \"%s\"\n", text.c_str());
  if (lower == "update") { wantUpdate = true; return; }
  if (lower == "status") { ntfySay(statusText()); return; }
  if (lower == "awake") { powerHold(AWAKE_COMMAND_MS); ntfySay("awake for " + String(AWAKE_COMMAND_MS / 60000) + " min"); return; }
  if (lower == "sleep") { powerRelease(); ntfySay("back to sleeping"); return; }
  if (lower.startsWith("received ")) { messageReceipt(text.substring(9)); return; }
#ifdef WIFI_TEST   // simulate trouble, to test the Wi-Fi manager from the phone
  if (lower == "drop") WiFi.disconnect();                                              // connection lost
  if (lower == "nointernet") wIoFails = WIFI_IO_FAIL_LIMIT;                            // joined but no internet
  if (lower == "hide") { wTestHideUntil = clockSeconds() + 120; wLast.valid = false; WiFi.disconnect(); } // out of range for 2 min
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
  } else if (line == "awake") {
    powerHold(AWAKE_COMMAND_MS);
  } else if (line == "sleep") {
    powerRelease();
  } else {
    consoleSettings(line);
    sayf("unread=%d outbox=%d reports=%d\n", listCount(UNREAD), listCount(OUTBOX), listCount(NOTES));
  }
}

// One thing the user did with the buttons.
void onInput(int event) {
  wifiKick();              // any press: if offline, look for a network now
  powerHold(LINGER_MS);    // ...and stay awake a little, in case more follows
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
    case EV_OFF:   // sleep with no timer: only a button brings it back
      sayln("Off");
      powerSleep(0);
      break;
  }
}

// True when nothing needs the device awake.
bool canSleep(bool polled) {
#ifdef NO_SLEEP
  return false;
#endif
  if (powerHeld() || portalOn || inputBusy() || inputPending() || messagesAwaiting() || wantUpdate) return false;
  if (!wifiSettled()) return false;                         // a scan or join is under way
  if (!wifiOnline()) return true;                           // nothing in range: sleep, and look again later
  return polled && !messagesWaiting() && !notesWaiting();   // online: the inbox is read and nothing is left to post
}

void setup() {
  Serial.begin(115200);
  settingsBegin();
  powerBegin();
  inputBegin();
  unsigned long setupMs = millis();
  if (wakeReason == WAKE_COLD) {
    sayf("\n== Pebble v%d ==\n", FW_VERSION);
    if (savedCount() == 0) sayln("No saved Wi-Fi: hold button 2 for 5 s for setup");
    if (!ntfyReady()) sayln("No topic base: set it on the setup page");
  }
  updateBoot();
  wifiBegin();
  if (wakeReason == WAKE_TIMER) statStartMs += setupMs + WAKE_LEAD_MS;
  if (wakeReason != WAKE_COLD) sayf("Woke (%s), ready in %lu ms\n", wakeReason == WAKE_BUTTON ? "button" : "timer", setupMs);
}

void loop() {
  static unsigned long lastPoll = 0;
  static bool polled = false, checked = false;   // this wake: the inbox has been read / an update check was made
  powerTouch();
  String line;
  if (consoleRead(line)) { powerHold(60000); onConsole(line); }

  for (int event; (event = inputTick()) != EV_NONE;) onInput(event);

  bool wasPortal = portalOn;
  if (portalTick()) wifiAdopt();                       // a network was joined through the setup page
  else if (wasPortal && !portalOn) wifiListChanged();  // closed without a join: networks may have been deleted
  wifiTick();

  if (wifiJustConnected()) {
    polled = false;   // read the inbox straight away
#ifdef WIFI_TEST
    if (!wWasFast) ntfySay("online, network " + String(wCur + 1) + " of " + String(savedCount()) + ", " + String(WiFi.RSSI()) + " dBm");
#endif
  }

  if (!inputBusy()) {   // no network work while someone is pressing or recording, so loop() stays quick
    if (wifiOnline()) {
      if (!polled || millis() - lastPoll >= (messagesAwaiting() ? RECEIPT_POLL_MS : INBOX_POLL_MS)) {
        bool first = !polled;
        polled = true;
        lastPoll = millis();
        int fresh = ntfyPoll(onMessage);
        if (first && wakeReason == WAKE_TIMER) statPollMs += millis() - lastPoll;
        sayf("Inbox: %d new (%lu ms)\n", fresh, millis() - lastPoll);
      }
      if (wantUpdate || (!checked && updateDue())) {   // asked for, or the once-a-start / once-a-day check
        bool asked = wantUpdate;
        wantUpdate = false;
        checked = true;
        updateCheck(asked);
      }
    }
    messagesTick();
    ntfyFlush();
  }

  if (canSleep(polled)) {
    unsigned long awake = millis() + WAKE_LEAD_MS;
    sayf("Sleep after %lu ms\n", awake);
    powerSleep(!wifiOnline() ? (savedCount() ? wifiRetryMs() : WIFI_RETRY_MAX_MS)
               : awake < CHECK_INTERVAL_MS - 1000 ? CHECK_INTERVAL_MS - awake : 1000);
  }
  delay(portalOn ? 2 : 20);   // answer the phone quickly while the setup page is open
}

#endif
