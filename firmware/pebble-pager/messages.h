// Sending and receiving pulse messages.
//   Outbox: messages waiting to be posted (recorded while offline, or faster than they can be sent). Once ntfy
//           has one it is deleted here: ntfy holds it for the partner.
//   Unread: messages received and not yet played, oldest first.
//   Receipts: every message received from the partner is answered with "received <id>" as soon as it is read
//           from the inbox, before it is played. After sending, the sender waits a short while for that answer.
// Both lists are saved in settings, so they survive sleep, restarts and power cuts. Reports to the phone
// ("sent ...") go through ntfySay, which keeps them until they can be posted.
//   On the ring: nothing while a message is on its way (it is assumed to be sending), then its one status: a green
//           flash of the green pixel at the bottom when the partner's receipt comes, or two red pulses if it can't go now (it stays queued) or if no
//           receipt comes. Each comes with a short buzz.
#ifndef PEBBLE_MESSAGES_H
#define PEBBLE_MESSAGES_H
#include "config.h"
#include "wifi_manager.h"
#include "ntfy.h"
#include "pulse.h"
#include "output.h"

const SavedList OUTBOX = {"ob", OUTBOX_MAX};
const SavedList UNREAD = {"ur", UNREAD_MAX};
const SavedList RECEIPTS = {"rc", OUTBOX_MAX};   // ids of received messages whose receipt could not be posted yet
unsigned long receiptRetryAt = 0;

// ---- Sending ----
struct Awaited { String id; unsigned long at; };   // a sent message whose receipt has not arrived yet
Awaited awaited[OUTBOX_MAX];
unsigned long sendRetryAt = 0;
bool sendShown = false;   // a message recorded in this wake is still in the outbox: if it can't go, the ring says so once
unsigned long sendShownAt = 0;   // when it was recorded
unsigned long partnerHeardAt = 0;   // millis() of the last receipt or message from the partner (0 = not this wake)
bool partnerLive() { return partnerHeardAt && millis() - partnerHeardAt < CONVERSATION_S * 1000UL; }   // their device is awake and listening: answers come fast
void partnerHeard() { partnerHeardAt = millis() ? millis() : 1; }
RTC_DATA_ATTR unsigned long talkedAt = 0;   // clockSeconds() of the last message of a conversation (0 = none), through sleep
unsigned long talkedNow() { unsigned long n = clockSeconds(); return n ? n : 1; }   // never 0, which means "no conversation"; and never ahead of the clock

bool messagesCanSend() { return ntfyReady() && partnerName.length() > 0; }
bool messagesAwaiting() {   // true while any sent message is still waiting for its receipt
  for (const Awaited& a : awaited)
    if (a.id.length()) return true;
  return false;
}

// Queue a message for the partner. Returns nullptr if it was accepted, otherwise a short reason.
const char* messageQueue(const String& text) {
  if (!messagesCanSend()) return "set the topic base and partner's name on the setup page first";
  Pulse p;
  if (const char* problem = pulseParse(text, p)) return problem;
  listPush(OUTBOX, pulseText(p));
  sendRetryAt = millis();
  sendShown = true;
  sendShownAt = millis();
  // Recording a message starts the conversation at once, not when it is posted: the inbox is read in between, and
  // a reply that arrives then must play, not wait for a tap. (Offline there is nothing to hold open.)
  if (wifiOnline()) talkedAt = talkedNow();
  return nullptr;
}

// Post the oldest queued message. Once the partner's inbox has it, it leaves the outbox: a failed copy to the
// phone must not cause a second message to the partner.
void messageSendNext() {
  String text = listPeek(OUTBOX);
  String id = ntfyPublish(partnerName, deviceName, text, true);
  if (id.length() == 0) {   // not posted (throttled, or no answer): it stays queued and is tried again shortly. No red
    sendRetryAt = millis() + SEND_RETRY_MS;   // yet: that would say "failed" for something that usually goes a few
    return;                                   // seconds later. It shows only if it is still waiting after SEND_FAIL_MS.
  }
  listPop(OUTBOX);
  if (listCount(OUTBOX) == 0) sendShown = false;
  talkedAt = talkedNow();   // sending starts a conversation, or carries one on
  if (!partnerLive()) { sayln("Connecting: fill started"); showConnecting(RECEIPT_WAIT_MS / 1000.0f); }   // the first message of a series: a fill while their device wakes and answers
  ntfySay("sent " + text);
  int slot = 0;   // remember the id until its receipt comes; if all slots are busy, reuse the oldest
  for (int i = 0; i < OUTBOX_MAX; i++) {
    if (awaited[i].id.length() == 0) { slot = i; break; }
    if ((long)(awaited[i].at - awaited[slot].at) < 0) slot = i;
  }
  awaited[slot] = {id, millis()};
  sendRetryAt = millis();
}

// The partner answered "received <id>".
void messageReceipt(const String& id) {
  for (Awaited& a : awaited) {
    if (a.id != id) continue;
    sayf("Delivered in %lu ms\n", millis() - a.at);
    partnerHeard();
    showReceived();
    a.id = "";
  }
}

// ---- Receiving ----
int arrived = 0;                          // messages that came in and have not been announced yet

// A valid pulse message arrived. Keep it for playing and answer it straight away.
void messageArrived(const NtfyMessage& m, const Pulse& p) {
  bool fromPartner = partnerName.length() && slug(m.title) == slug(partnerName);
  String text = pulseText(p);
  listPush(UNREAD, (fromPartner ? partnerName : String("phone")) + "\t" + text);
  arrived++;
  if (talkedAt && clockSeconds() - talkedAt < CONVERSATION_S) talkedAt = talkedNow();   // a reply carries the conversation on
  if (fromPartner) {
    if (ntfyPublish(partnerName, deviceName, "received " + m.id, true).length() == 0) listPush(RECEIPTS, m.id);   // retried by messagesTick()
    partnerHeard();
    bool answered = false;   // a reply from the partner proves they have what we sent: it counts as the receipt
    for (Awaited& a : awaited) if (a.id.length()) { a.id = ""; answered = true; }
    if (answered) showReceived();
  } else ntfySay("received " + text);
}

// The color of the oldest unread message, which is its sender's color (0xRRGGBB), or -1 if nothing is waiting.
// The ring's "message waiting" blip uses it.
long messageWaitingColor() {
  String item = listPeek(UNREAD);
  if (item.length() == 0) return -1;
  String text = item.substring(item.indexOf('\t') + 1);
  int c = colorIndex(text.substring(0, text.indexOf(' ')));
  return COLOR_RGB[c < 0 ? 0 : c];
}

// Play one stored item, "<sender>\t<text>", on the ring and the motor. Returns when it has finished.
void messageShow(const char* what, const String& item) {
  int tab = item.indexOf('\t');
  String from = item.substring(0, tab), text = item.substring(tab + 1);
  sayf("%s from %s: %s (%d unread)\n", what, from.c_str(), text.c_str(), listCount(UNREAD));
  Pulse p;
  if (!pulseParse(text, p)) showPlay(p);
}

// Play the oldest unread message. Returns false if there is none. A message plays once: there is no replay.
// Nothing is posted: the sender already has its receipt, and the phone saw the message when it was sent.
bool messagePlay() {
  String item = listPeek(UNREAD);
  if (item.length() == 0) return false;
  listPop(UNREAD);
  if (wifiOnline()) talkedAt = talkedNow();   // reading a message is the start of a conversation: the next one arrives live
  messageShow("Play", item);
  return true;
}

// A message went out from here a moment ago, or has come in since: a conversation is going on.
// While it is, the device stays awake with its inbox held open as a stream (main tab), so a reply plays at once.
// Sending starts one; after that each message sent or received gives it CONVERSATION_S more.
bool messagesConversing() { return talkedAt && clockSeconds() - talkedAt < CONVERSATION_S; }

// Call after reading the inbox. A message normally waits for a tap of button 1, with only the ring's waiting light
// to show it. During a conversation, what has just arrived plays at once, and counts as played. Returns true if
// anything was played.
bool messagesAnnounce() {
  bool any = arrived > 0;
  arrived = 0;
  if (!any || !messagesConversing()) return false;
  while (listCount(UNREAD) > 0 && !playInterrupted) messagePlay();
  return true;
}


// Something is queued and worth trying to post right now (not just after a failed attempt).
bool messagesWaiting() { return messagesCanSend() && listCount(OUTBOX) > 0 && (long)(millis() - sendRetryAt) >= 0; }

// Call every loop() while online: post what is queued, and give up on receipts that never came.
void messagesTick() {
  static unsigned long expireAt = 0;
  if ((long)(millis() - expireAt) >= 0) {
    expireAt = millis() + 60000;
    listExpire(OUTBOX);
    listExpire(UNREAD);
    listExpire(NOTES);
  }
  for (Awaited& a : awaited)
    if (a.id.length() && millis() - a.at >= RECEIPT_WAIT_MS) { sayln("No receipt"); showFailed(); a.id = ""; }
  if (sendShown && listCount(OUTBOX) > 0 && millis() - sendShownAt >= SEND_FAIL_MS) {   // online, but still not gone
    sendShown = false;
    sayln("Not sent yet: queued");
    showFailed();
  }
  if (sendShown && listCount(OUTBOX) > 0 && wifiSettled() && !wifiOnline()) {   // no network to send it on: it waits in the outbox
    sendShown = false;
    sayln("Can't send now: queued");
    showFailed();
  }
  // The sender gives up on a receipt after RECEIPT_WAIT_MS: later is no use. This must run even when offline, because
  // a receipt that is waiting keeps the device awake.
  listExpire(RECEIPTS, RECEIPT_WAIT_MS / 1000 + 5);
  if (!wifiOnline() || !messagesCanSend() || ntfyBackedOff() || ntfyPostBlocked()) return;
  if (listCount(RECEIPTS) > 0 && (long)(millis() - receiptRetryAt) >= 0) {   // a receipt first: the sender is waiting for it
    if (ntfyPublish(partnerName, deviceName, "received " + listPeek(RECEIPTS), true).length()) listPop(RECEIPTS);
    else receiptRetryAt = millis() + SEND_RETRY_MS;
    return;
  }
  if (listCount(OUTBOX) > 0 && (long)(millis() - sendRetryAt) >= 0) messageSendNext();
}

#endif
