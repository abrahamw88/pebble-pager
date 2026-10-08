// Sending and receiving pulse messages.
//   Outbox: messages waiting to be posted (recorded while offline, or faster than they can be sent). Once ntfy
//           has one it is deleted here: ntfy holds it for the partner.
//   Unread: messages received and not yet played, oldest first.
//   Receipts: every message received from the partner is answered with "received <id>" as soon as it is read
//           from the inbox, before it is played. After sending, the sender waits a short while for that answer.
// Both lists are saved in settings, so they survive sleep, restarts and power cuts. Reports to the phone
// ("sent ...") go through ntfySay, which keeps them until they can be posted.
#ifndef PEBBLE_MESSAGES_H
#define PEBBLE_MESSAGES_H
#include "config.h"
#include "wifi_manager.h"
#include "ntfy.h"
#include "pulse.h"

const SavedList OUTBOX = {"ob", OUTBOX_MAX};
const SavedList UNREAD = {"ur", UNREAD_MAX};

// ---- Sending ----
struct Awaited { String id; unsigned long at; };   // a sent message whose receipt has not arrived yet
Awaited awaited[OUTBOX_MAX];
unsigned long sendRetryAt = 0;

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
  return nullptr;
}

// Post the oldest queued message. Once the partner's inbox has it, it leaves the outbox: a failed copy to the
// phone must not cause a second message to the partner.
void messageSendNext() {
  String text = listPeek(OUTBOX);
  String id = ntfyPublish(partnerName, deviceName, text);
  if (id.length() == 0) { sendRetryAt = millis() + SEND_RETRY_MS; return; }
  listPop(OUTBOX);
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
    sayf("Delivered in %lu ms\n", millis() - a.at);   // the ring's "delivered" sweep goes here
    a.id = "";
  }
}

// ---- Receiving ----
RTC_DATA_ATTR char lastPlayed[96] = "";   // "<sender>\t<text>", kept for a replay (through sleep)

// A valid pulse message arrived. Keep it for playing and answer it straight away.
void messageArrived(const NtfyMessage& m, const Pulse& p) {
  bool fromPartner = partnerName.length() && slug(m.title) == slug(partnerName);
  String text = pulseText(p);
  listPush(UNREAD, (fromPartner ? partnerName : String("phone")) + "\t" + text);
  if (fromPartner) ntfyPublish(partnerName, deviceName, "received " + m.id);
  else ntfySay("received " + text);
}

// Play the oldest unread message, or replay the last one played. Returns false if there is nothing to play.
// Playing is only printed here for now; the ring and motor will show it. Nothing is posted: the sender already
// has its receipt, and the phone saw the message when it was sent.
bool messagePlay() {
  String item = listPeek(UNREAD);
  bool fresh = item.length() > 0;
  if (fresh) { listPop(UNREAD); item.toCharArray(lastPlayed, sizeof(lastPlayed)); }
  if (!lastPlayed[0]) return false;
  String last = lastPlayed;
  int tab = last.indexOf('\t');
  String from = last.substring(0, tab), text = last.substring(tab + 1);
  sayf("%s from %s: %s (%d unread left)\n", fresh ? "Play" : "Replay", from.c_str(), text.c_str(), listCount(UNREAD));
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
    if (a.id.length() && millis() - a.at >= RECEIPT_WAIT_MS) { sayln("No receipt"); a.id = ""; }
  if (wifiOnline() && messagesCanSend() && listCount(OUTBOX) > 0 && (long)(millis() - sendRetryAt) >= 0) messageSendNext();
}

#endif
