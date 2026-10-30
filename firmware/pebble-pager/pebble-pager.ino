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
// The real firmware prints nothing to serial (the hardware checks do); -DDEBUG_SERIAL turns the output back on.
// The serial console and the inbox help with testing:
//   console: show, set name|color|partner|base <value>, send <message>, play, press <1|2> <times>, boot <1|2>,
//            battery, show received|failed|waiting|low, awake, sleep
//   inbox:   update, status (wake counts, time awake and battery, to the phone), battery, battery on|off (report
//            every hour), awake (stay awake 10 minutes), sleep
//
// Build flags (--build-property compiler.cpp.extra_flags="..."):
//   -DFW_VERSION=N       version of this build, compared with the release's manifest (default 1)
//   -DTEST=n             1 board check, 2 buttons + ring + motor, 4 battery, 5 button stress
//   -DNO_SLEEP           never sleep (for development)
//   -DNTFY_NO_BLOCK      keep posting through ntfy's 429s and quota pause (to time requests while the quota is used up)
//   -DDEBUG_SERIAL       print what the firmware is doing over USB serial (off in the real firmware)
//   -DWIFI_TEST          adds inbox commands that simulate Wi-Fi trouble (drop, nointernet, hide, roam) and an
//                        "online" report to the phone after every full join
//   -DFW_CRASH           an image that crashes at startup, to test update rollback
//   -DOTA_LOCAL_IP=a.b.c.d   take updates from a local web server on port 8000 instead of GitHub
//   -DOTA_TAG=name       take updates from that one GitHub release (a pre-release, for testing) instead of the latest
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

RTC_DATA_ATTR bool wantUpdate = false;   // the phone sent "update": kept through sleep until it can run

// A line for the phone about how the device is doing. The time awake per check is measured, and is what decides
// battery life.
String statusText() {
  unsigned long n = statChecks ? statChecks : 1, avg = statCheckMs / n;
  return "status: v" + String(FW_VERSION) + ", " + String(statChecks) + " checks, awake avg " + String(avg) + " ms max " +
         String(statMaxCheckMs) + " ms (start " + String(statStartMs / n) + " join " +
         String(statJoinMs / n) + " inbox " + String(statPollMs / n) + "), joins " + String(wFastJoins) + " fast " +
         String(wFullJoins) + " full " + String(wFastFails) + " failed, " + String(WiFi.RSSI()) + " dBm, " +
         String(statButtonWakes) + " button wakes, " + String(statStalls) + " stalls, frame gap max " + String(statFrameGapMs) +
         " ms at " + String(statFrameGapAt) + " ms, unread " + String(listCount(UNREAD)) +
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

// Button 2's tap: the battery level on the ring, then, when that has faded, the Wi-Fi signal.
const unsigned long TAP_WIFI_WAIT_MS = 6000;   // most it waits for a join in progress before showing "not connected"
bool tapWifiPending = false;
unsigned long tapAt = 0;

void batteryTap() {
  batteryRead();
  sayf("%s\n", batteryText().c_str());
  showBattery(batteryFitted(), batteryPercent());
  tapWifiPending = true;
  tapAt = millis();
}

void tapWifiTick() {   // call every loop()
  if (!tapWifiPending || gaugeShowing()) return;
  if (!wifiOnline() && !wifiSettled() && millis() - tapAt < TAP_WIFI_WAIT_MS) return;   // still joining: wait for the answer
  tapWifiPending = false;
  bool online = wifiOnline();
  sayf("Wi-Fi %s\n", online ? (String(WiFi.RSSI()) + " dBm").c_str() : "not connected");
  showWifi(online, online ? WiFi.RSSI() : 0);
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
bool remindDue = false;    // show that a message is waiting: once at each wake, and after each later look at the inbox
bool starting = false;     // just switched on, and joining Wi-Fi for the first time: the chase

// One thing the user did with the buttons.
void onInput(int event) {
  wifiKick();              // any press: if offline, look for a network now
  powerHold(LINGER_MS);    // ...and stay awake a little, in case more follows
  switch (event) {
    case EV_PRESS_PLAY:   // a press with messages waiting: play them all at once, oldest first, each in its sender's color
      while (listCount(UNREAD) > 0 && !playInterrupted) messagePlay();
      if (inputHeldPlayed && !button[0].down && !playInterrupted) {   // it was a hold, and is over: a fresh recording
        inputHeldPlayed = false;
        inputArm();
        sayln("Recording");
      }
      break;
    case EV_PLAY:   // a tap with messages waiting (the press that woke the device): play them all
      while (listCount(UNREAD) > 0 && !playInterrupted) messagePlay();
      break;
    case EV_RECORD_START:
      if (listCount(UNREAD) > 0 || inputHeldPlayed) {   // unread messages come first: play them all, then record from scratch
        inputHeldPlayed = false;
        while (listCount(UNREAD) > 0 && !playInterrupted) messagePlay();
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
  if (powerHeld() || portalOn || inputBusy() || inputPending() || messagesAwaiting() || listCount(RECEIPTS) > 0) return false;
  if (outputActive() || remindDue) return false;            // let the ring and the motor finish, and show a reminder that is due
  if (messagesConversing()) return false;                   // a conversation is going on: stay awake and connected
  if (!wifiSettled()) return false;                         // a scan or join is under way
  if (!wifiOnline()) return true;                           // nothing in range: sleep, and look again later
  return inboxRead && !messagesWaiting() && !notesWaiting();   // online: the inbox is read and nothing is left to post
}

void setup() {
  serialBegin();
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

  // A press that cut a message short is the start of a reply: it does not play the next waiting one instead.
  if (playInterrupted && recState == R_IDLE && !button[0].down && !inputPending()) playInterrupted = false;
  bool waiting = listCount(UNREAD) > 0 && !portalOn && !playInterrupted;   // a press plays a waiting message at once
  inputPlayOnPress = waiting;
  outPlayPending = waiting;
  for (int event; (event = inputTick()) != EV_NONE;) onInput(event);
  if (starting && (wifiOnline() || wifiSettled())) starting = false;
  outputTick(starting);
  tapWifiTick();
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

  // The posting connection is opened as a recording begins (its short pause hides the half second it takes; the
  // presses are time-stamped by interrupts, so the wait does not disturb them), and closed when the conversation is
  // over and it has sat unused.
  if (wifiOnline() && recState != R_IDLE && (!postClient || millis() - postClientAt > NTFY_POST_IDLE_MS) && (long)(millis() - postWarmRetryAt) >= 0) ntfyPostWarm();
  if (postClient && (!wifiOnline() || (!messagesConversing() && recState == R_IDLE && millis() - postClientAt > 10000))) ntfyPostClose();

  if (!inputBusy()) {   // no network work while someone is pressing or recording, so loop() stays quick
    if (inboxMoved) { inboxMoved = false; ntfyStreamClose(); streamRetryAt = 0; }   // a changed name or topic base
    if (!wifiOnline() || !messagesConversing()) ntfyStreamClose();   // the stream belongs to a conversation
    if (wifiOnline()) {
      // In a conversation the inbox is held open as a stream: a reply, or a receipt, is here the moment it is posted.
      // (not while a message is waiting to go: it is posted first, and the stream opens on the next pass)
      if (messagesConversing() && !ntfyStreamIsOpen() && listCount(OUTBOX) == 0 && (long)(millis() - streamRetryAt) >= 0 && ntfyStreamOpen()) {
        inboxRead = true;   // it starts from where the inbox was last read
        lastPoll = millis();
      }
      if (ntfyStreamIsOpen() && ntfyStreamRead(onMessage) > 0) messagesAnnounce();   // what arrived plays at once
      // Otherwise it is read at intervals: every check, and quickly while a receipt is awaited.
      unsigned long every = messagesAwaiting() ? RECEIPT_POLL_MS : INBOX_POLL_MS;
      if (!inboxRead && ntfyBackedOff() && !messagesConversing()) inboxRead = true;   // throttled: skip this read, don't wait up for it
      if (!ntfyStreamIsOpen() && (!inboxRead || millis() - lastPoll >= every) && !ntfyBackedOff()) {
        bool first = !inboxRead;
        inboxRead = true;
        lastPoll = millis();
        int fresh = ntfyPoll(onMessage);
        if (first && wakeReason == WAKE_TIMER) statPollMs += millis() - lastPoll;
        sayf("Inbox: %d new (%lu ms)\n", fresh, millis() - lastPoll);
        bool played = messagesAnnounce();
        if (!played && (!first || fresh > 0) && !waitingLit) remindDue = true;   // a new message plays; otherwise show that one is waiting
        waitingLit = false;   // (a new arrival has lit it already, in messageArrived)
      }
      if (batteryReportDue()) {
        if (millis() > 60000) batteryRead();   // kept awake a long time: the reading from start-up is stale
        ntfySay(batteryText());
      }
      // Asked for, or the once-a-start / once-a-day check. Not while the setup page is open: a download would stall it.
      if (!portalOn && (wantUpdate || (!checked && updateDue()))) {
        bool asked = wantUpdate;
        wantUpdate = false;
        checked = true;
        ntfyStreamClose();   // free their memory: the download needs a secure connection of its own
        ntfyPostClose();
        updateCheck(asked);
      }
    }
    messagesTick();
    ntfyFlush();
  }

  if (canSleep()) {
    unsigned long awake = millis() + WAKE_LEAD_MS;
    sayf("Sleep after %lu ms\n", awake);
    ntfyStreamClose();
    ntfyPostClose();
    outputStop();
    powerSleep(!wifiOnline() ? (savedCount() ? wifiRetryMs() : WIFI_RETRY_MAX_MS)
               : awake < CHECK_INTERVAL_MS - 1000 ? CHECK_INTERVAL_MS - awake : 1000);
  }
  delay(portalOn ? 2 : 20);   // answer the phone quickly while the setup page is open
}

#endif
