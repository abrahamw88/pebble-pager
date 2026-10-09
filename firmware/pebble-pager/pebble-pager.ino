// Pebble pager firmware. Behavior is specified in README.md.
//
// Tabs, in the order they build on each other:
//   config.h   pins, values to tune, saved settings, serial output and console
//   power.h    deep sleep, wake reasons, the stall guard
//   wifi_manager.h  Wi-Fi connection manager
//   ntfy.h     post to topics, read this device's inbox
//   pulse.h    the message format
//   input.h    buttons: taps, holds and recording
//   ring.h     the light ring's animation engine: pure math
//   output.h   the ring and the motor as hardware, and what the device shows on them
//   battery.h  battery level
//   messages.h sending, receiving, receipts, the outbox and the unread list
//   ota.h      remote firmware updates
//   portal.h   setup page
//   tests.h    hardware checks, built instead of the firmware with -DTEST=n
//
// The ring, motor and battery code is written but has not run on the parts yet (README: The ring's animation).
//
// The serial console and the inbox help with testing:
//   console: show, set name|color|partner|base <value>, send <message>, play, press <1|2> <times>, boot <1|2>,
//            battery, show received|failed|waiting|low, awake, sleep
//   inbox:   update, status (wake counts, time awake and battery, to the phone), battery, battery on|off (report
//            every hour), awake (stay awake 10 minutes), sleep
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
#include "input.h"
#include "ring.h"
#include "output.h"
#include "battery.h"
#include "messages.h"
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
         " outbox " + String(listCount(OUTBOX)) + ", " + batteryText();
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
  if (lower == "battery") { ntfySay(batteryText()); return; }
  if (lower == "battery on" || lower == "battery off") {
    batteryReports(lower.endsWith("on"));
    ntfySay(battReports ? "battery report every " + String(BATT_REPORT_S / 60) + " min; now " + batteryText() : String("battery report off"));
    return;
  }
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

void batteryTap() {   // button 2's tap: the level on the ring
  batteryRead();
  sayf("%s\n", batteryText().c_str());
  showBattery(batteryFitted() ? batteryPercent() : 0);
}

// Console commands that stand in for the buttons, and show each display, for testing.
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
  } else if (line == "battery") {
    batteryTap();
  } else if (line.startsWith("show ")) {
    String what = line.substring(5);
    if (what == "received") showReceived();
    else if (what == "failed") showFailed();
    else if (what == "waiting") showWaiting(outOwnRgb);
    else if (what == "low") showLow();
    else sayln("show received|failed|waiting|low");
  } else if (line == "awake") {
    powerHold(AWAKE_COMMAND_MS);
  } else if (line == "sleep") {
    powerRelease();
  } else {
    consoleSettings(line);
    sayf("unread=%d outbox=%d reports=%d\n", listCount(UNREAD), listCount(OUTBOX), listCount(NOTES));
  }
}

bool inboxRead = false;    // this wake: the inbox has been read (clear it to read again at once)
bool tapWaiting = false;   // a tap found nothing stored: read the inbox, then play
bool remindDue = false;    // show that a message is waiting: once at each wake, and after each later look at the inbox
bool starting = false;     // just switched on, and joining Wi-Fi for the first time: the chase

// One thing the user did with the buttons.
void onInput(int event) {
  wifiKick();              // any press: if offline, look for a network now
  powerHold(LINGER_MS);    // ...and stay awake a little, in case more follows
  switch (event) {
    case EV_PLAY:   // nothing stored: a message may have arrived since the last check, so look before answering
      if (listCount(UNREAD) > 0) messagePlay();
      else { tapWaiting = true; inboxRead = false; }
      break;
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
      if (problem) showFailed();
      inboxRead = false;   // the recording is done: look for a new message straight away
      break;
    }
    case EV_BATTERY: batteryTap(); break;
    case EV_SETUP:   // the same hold opens the setup page and closes it
      showSetup(!portalOn);
      if (portalOn) stopPortal();
      else { wifiPause(); startPortal(); }
      break;
    case EV_OFF:   // sleep with no timer: only a long hold of button 2 brings it back
      sayln("Off");
      outputStop();
      powerIsOff = true;
      powerSleep(0);
      break;
  }
}

// Quiet reminders, when nothing else is on the ring: a message is waiting (its sender's color, at the top), and
// the battery is low (amber, at the bottom).
void remind() {
  if (inputBusy() || outputActive()) return;
  if (remindDue) {
    remindDue = false;
    long rgb = messageWaitingColor();
    if (rgb >= 0) { showWaiting(rgb); return; }
  }
  if (batteryLowDue()) showLow();
}

// True when nothing needs the device awake.
bool canSleep() {
#ifdef NO_SLEEP
  return false;
#endif
  if (powerHeld() || portalOn || tapWaiting || inputBusy() || inputPending() || messagesAwaiting() || wantUpdate) return false;
  if (outputActive()) return false;                         // let the ring and the motor finish
  if (!wifiSettled()) return false;                         // a scan or join is under way
  if (!wifiOnline()) return true;                           // nothing in range: sleep, and look again later
  return inboxRead && !messagesWaiting() && !notesWaiting();   // online: the inbox is read and nothing is left to post
}

void setup() {
  Serial.begin(115200);
  settingsBegin();
  powerBegin();
  outputOnGate();   // a device that was off stays asleep unless button 2 is held
  inputBegin();
  batteryBegin();   // before the radio starts
  outputBegin();
  remindDue = wakeReason == WAKE_TIMER;
  starting = wakeReason == WAKE_COLD && savedCount() > 0;
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
  static bool checked = false;   // this wake: an update check was made
  powerTouch();
  String line;
  if (consoleRead(line)) { powerHold(60000); onConsole(line); }

  for (int event; (event = inputTick()) != EV_NONE;) onInput(event);
  if (starting && (wifiOnline() || wifiSettled())) starting = false;
  outputTick(starting);
  remind();

  bool wasPortal = portalOn;
  if (portalTick()) wifiAdopt();                       // a network was joined through the setup page
  else if (wasPortal && !portalOn) wifiListChanged();  // closed without a join: networks may have been deleted
  wifiTick();

  if (wifiJustConnected()) {
    inboxRead = false;   // read the inbox straight away
#ifdef WIFI_TEST
    if (!wWasFast) ntfySay("online, network " + String(wCur + 1) + " of " + String(savedCount()) + ", " + String(WiFi.RSSI()) + " dBm");
#endif
  }

  if (!inputBusy()) {   // no network work while someone is pressing or recording, so loop() stays quick
    if (wifiOnline()) {
      if (!inboxRead || millis() - lastPoll >= (messagesAwaiting() ? RECEIPT_POLL_MS : INBOX_POLL_MS)) {
        bool first = !inboxRead;
        inboxRead = true;
        lastPoll = millis();
        int fresh = ntfyPoll(onMessage);
        if (first && wakeReason == WAKE_TIMER) statPollMs += millis() - lastPoll;
        sayf("Inbox: %d new (%lu ms)\n", fresh, millis() - lastPoll);
        if (tapWaiting) arrived = 0;                           // a tap is about to play what came in
        if (!messagesAnnounce() && (!first || fresh > 0)) remindDue = true;   // a new message plays; otherwise show that one is waiting
      }
      if (batteryReportDue()) {
        if (millis() > 60000) batteryRead();   // kept awake a long time: the reading from start-up is stale
        ntfySay(batteryText());
      }
      if (wantUpdate || (!checked && updateDue())) {   // asked for, or the once-a-start / once-a-day check
        bool asked = wantUpdate;
        wantUpdate = false;
        checked = true;
        updateCheck(asked);
      }
    }
    if (tapWaiting && (inboxRead || (wifiSettled() && !wifiOnline()))) {   // the tap's answer, now the inbox is read (or can't be)
      tapWaiting = false;
      if (!messagePlay()) sayln("Nothing to play");
    }
    messagesTick();
    ntfyFlush();
  }

  if (canSleep()) {
    unsigned long awake = millis() + WAKE_LEAD_MS;
    sayf("Sleep after %lu ms\n", awake);
    outputStop();
    powerSleep(!wifiOnline() ? (savedCount() ? wifiRetryMs() : WIFI_RETRY_MAX_MS)
               : awake < CHECK_INTERVAL_MS - 1000 ? CHECK_INTERVAL_MS - awake : 1000);
  }
  delay(portalOn ? 2 : 20);   // answer the phone quickly while the setup page is open
}

#endif
