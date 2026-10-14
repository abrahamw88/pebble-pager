// ntfy: post text to a topic, and read new messages from this device's own inbox.
// Topics are <base>-<name>: this device's inbox, the partner's inbox, and <base>-phone for the phone.
#ifndef PEBBLE_NTFY_H
#define PEBBLE_NTFY_H
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"
#include "wifi_manager.h"

const char* NTFY_SERVER = "https://ntfy.sh/";
const int NTFY_CONNECT_MS = 5000;
const int NTFY_READ_MS = 10000;
const int NTFY_POLL_BODY_MAX = 12000;   // most bytes of a read held in memory
const int NTFY_BATCH = 8;   // most messages handled per poll; any more wait for the next one

bool ntfyReady() { return topicBase.length() > 0; }   // a base name has been set on the setup page

// ntfy limits each visitor (IP address) to a burst of 60 requests, refilled at one every 5 s, and answers 429 beyond
// it. That is the server working, not a network fault: the connection is fine, so it must not count as "no internet"
// to the Wi-Fi manager. Instead everything holds off for a while, longer each time, and then carries on.
unsigned long ntfyBackoffUntil = 0, ntfyBackoffMs = 0;
#ifdef NTFY_NO_BLOCK   // testing only: keep trying whatever ntfy answers (to time requests while the quota is used up)
bool ntfyBackedOff() { return false; }
#else
bool ntfyBackedOff() { return (long)(millis() - ntfyBackoffUntil) < 0; }
#endif

// The allowance is kept as a budget of our own, so that it is not spent on things that can wait. It starts full (60)
// and refills a request every 5 s, as ntfy's does. A message to the partner and a receipt may use all of it; everything
// else (reads, copies for the phone, status lines) leaves NTFY_RESERVE in it, so a message you record is never held up
// behind them. (Other devices and a browser on the same network use the same allowance, which this cannot see: a 429
// corrects the budget.)
const float NTFY_BURST = 60, NTFY_RESERVE = 10;
const unsigned long NTFY_REFILL_MS = 5000;
float ntfyTokens = NTFY_BURST;
unsigned long ntfyTokenAt = 0;
void ntfyRefill() {
  unsigned long now = millis();
  ntfyTokens = min(NTFY_BURST, ntfyTokens + (now - ntfyTokenAt) / (float)NTFY_REFILL_MS);
  ntfyTokenAt = now;
}
bool ntfyMayUse(bool urgent) { ntfyRefill(); return !ntfyBackedOff() && ntfyTokens >= (urgent ? 1.0f : NTFY_RESERVE); }
void ntfySpend() { ntfyRefill(); ntfyTokens = max(0.0f, ntfyTokens - 1); }

// ntfy's free service also allows each visitor (IP address) only 250 messages a day, and answers a post with 429 and
// code 42908 once it is used up. Reading still works. Trying again at once is pointless, so publishing is paused for
// a while (and tried again then: the quota is not published, and resets some time within the day).
const unsigned long NTFY_QUOTA_RETRY_S = 600;
RTC_DATA_ATTR unsigned long ntfyQuotaUntil = 0;   // clockSeconds() until which publishing is known to be refused
#ifdef NTFY_NO_BLOCK
bool ntfyPostBlocked() { return false; }
#else
bool ntfyPostBlocked() { return ntfyQuotaUntil && clockSeconds() < ntfyQuotaUntil; }
#endif

void ntfyNote(int code) {   // call with every response's status
  if (code == 429) {   // the allowance is gone: one request comes back every 5 s, so wait a little more than that
    ntfyTokens = 0;
    ntfyBackoffMs = ntfyBackoffMs ? min(ntfyBackoffMs * 2, 24000UL) : 6000UL;
    ntfyBackoffUntil = millis() + ntfyBackoffMs;
    sayf("ntfy is rate limiting: pausing %lu s\n", ntfyBackoffMs / 1000);
  } else if (code == 200) ntfyBackoffMs = 0;
}
String topicUrl(const String& who) { return String(NTFY_SERVER) + topicBase + "-" + slug(who); }

void ntfyOpen(WiFiClientSecure& client, HTTPClient& http) {
  client.setInsecure();   // certificates are not checked yet (README: Remote updates)
  client.setHandshakeTimeout(5);
  http.setConnectTimeout(NTFY_CONNECT_MS);
  http.setTimeout(NTFY_READ_MS);
  http.useHTTP10(true);   // the server closes the connection after the body: no waiting on chunked reads
}

String jsonText(const String& line, const char* key) {   // value of "key":"value" in one JSON line
  String k = String("\"") + key + "\":\"";
  int i = line.indexOf(k);
  if (i < 0) return "";
  i += k.length();
  int j = line.indexOf('"', i);
  return j < 0 ? "" : line.substring(i, j);
}

unsigned long jsonNumber(const String& line, const char* key) {   // value of "key":123 in one JSON line
  String k = String("\"") + key + "\":";
  int i = line.indexOf(k);
  return i < 0 ? 0 : strtoul(line.c_str() + i + k.length(), nullptr, 10);
}

// A secure connection kept open for posting, so that a message does not wait for a new handshake (about half a
// second) each time. It is opened ahead of need (a recording has begun, or a conversation is going on), reused by
// every post while it lasts, and closed when the conversation is over. A post that finds it dead opens a fresh one.
const uint32_t NTFY_POST_HEAP = 60000;   // about 40 KB; not kept open with less free than this
const unsigned long NTFY_POST_IDLE_MS = 30000;   // unused for this long, it is closed rather than trusted
WiFiClientSecure* postClient = nullptr;
unsigned long postClientAt = 0, postWarmRetryAt = 0;

void ntfyPostClose() {
  if (!postClient) return;
  postClient->stop();
  delete postClient;
  postClient = nullptr;
}

bool ntfyPostWarm() {   // blocks for the connection (about half a second)
  postWarmRetryAt = millis() + 5000;
  if (postClient && millis() - postClientAt > NTFY_POST_IDLE_MS) ntfyPostClose();   // stale: make a fresh one now
  if (postClient) return true;
  if (!ntfyReady() || ntfyPostBlocked() || ESP.getFreeHeap() < NTFY_POST_HEAP) return false;
  unsigned long t0 = millis();
  postClient = new WiFiClientSecure();
  postClient->setInsecure();
  postClient->setHandshakeTimeout(5);
  if (!postClient->connect("ntfy.sh", 443, NTFY_CONNECT_MS)) { ntfyPostClose(); sayln("Post connection: failed"); return false; }
  postClientAt = millis();
  sayf("Post connection open (%lu ms, %u KB free)\n", millis() - t0, ESP.getFreeHeap() / 1024);
  return true;
}

// One post as plain HTTP/1.1 over the open connection (the library's HTTP client opens a new one however it is
// handed). Returns the status code (-1: no answer, so the connection is not to be trusted), fills `body` with the
// answer, and sets `reusable` unless the server said it is closing the connection.
int ntfyRawPost(WiFiClientSecure& c, const String& topic, const String& title, const String& text, String& body, bool& reusable) {
  reusable = false;
  String req = "POST /" + topic + " HTTP/1.1\r\nHost: ntfy.sh\r\nUser-Agent: pebble-pager\r\nTitle: " + title +
               "\r\nContent-Type: text/plain\r\nContent-Length: " + String(text.length()) + "\r\n\r\n" + text;
  while (c.available() > 0) c.read();   // anything left over from before
  c.setTimeout(3000);   // a connection that has gone stale answers at once or not at all: do not wait the usual 10 s
  if (c.write((const uint8_t*)req.c_str(), req.length()) != req.length()) return -1;
  String status = c.readStringUntil('\n');
  int sp = status.indexOf(' ');
  if (sp < 0) return -1;
  int code = status.substring(sp + 1).toInt();
  long length = -1;
  bool chunked = false, closing = status.startsWith("HTTP/1.0");
  for (int n = 0; n < 40; n++) {
    String h = c.readStringUntil('\n');
    h.trim();
    if (h.length() == 0) break;
    h.toLowerCase();
    if (h.startsWith("content-length:")) length = h.substring(15).toInt();
    else if (h.startsWith("transfer-encoding:") && h.indexOf("chunked") >= 0) chunked = true;
    else if (h.startsWith("connection:") && h.indexOf("close") >= 0) closing = true;
  }
  body = "";
  if (chunked) {
    for (int n = 0; n < 8; n++) {
      String sz = c.readStringUntil('\n');
      long len = strtol(sz.c_str(), nullptr, 16);
      if (len <= 0) { c.readStringUntil('\n'); break; }
      for (long i = 0; i < len; i++) { int ch = c.read(); if (ch < 0 && !c.available()) { delay(1); ch = c.read(); } if (ch >= 0 && body.length() < 1024) body += (char)ch; }
      c.readStringUntil('\n');
    }
  } else if (length > 0) {
    for (long i = 0; i < length && i < 4096; i++) { int ch = c.read(); if (ch < 0 && !c.available()) { delay(1); ch = c.read(); } if (ch >= 0 && body.length() < 1024) body += (char)ch; }
  } else if (length < 0) closing = true;   // no length and not chunked: the end of the body is the end of the connection
  reusable = !closing && c.connected();
  return code;
}

// Post `text` to <base>-<who> with `title`. Returns the new message's id, or "" if the server did not take it.
// Every result feeds the Wi-Fi manager's internet check. `urgent`: a message to the partner or a receipt, which may
// use the whole budget (the rest leave NTFY_RESERVE).
String ntfyPublish(const String& who, const String& title, const String& text, bool urgent = false) {
  if (!ntfyReady() || ntfyPostBlocked() || !ntfyMayUse(urgent)) return "";
  ntfySpend();
  unsigned long t0 = millis();
  int code = -1;
  bool wasKept = false;
  String id;
  // A connection left idle may have been dropped by the server or the router without our knowing, and a post on it
  // would stall until it timed out: past NTFY_POST_IDLE_MS it is not trusted, and a fresh one is made.
  if (postClient && millis() - postClientAt > NTFY_POST_IDLE_MS) ntfyPostClose();
  if (postClient && postClient->connected()) {   // the open connection: no handshake
    String body;
    bool reusable = false;
    code = ntfyRawPost(*postClient, topicBase + "-" + slug(who), title, text, body, reusable);
    if (code > 0) {
      wasKept = true;
      if (code == 200) id = jsonText(body, "id");
      else if (code == 429 && body.indexOf("42908") >= 0) {
        ntfyQuotaUntil = clockSeconds() + NTFY_QUOTA_RETRY_S;
        sayf("ntfy: the daily message quota is used up; not publishing for %lu min\n", NTFY_QUOTA_RETRY_S / 60);
      }
      if (reusable) postClientAt = millis(); else ntfyPostClose();
    } else {
      ntfyPostClose();   // it had died: the post is made again below, on a fresh connection
    }
  }
  if (code <= 0) {   // no open connection, or it was dead
    WiFiClientSecure client;
    HTTPClient http;
    ntfyOpen(client, http);
    if (http.begin(client, topicUrl(who))) {
      http.addHeader("Title", title);
      code = http.POST(text);
      if (code == 200) id = jsonText(http.getString(), "id");
      else if (code == 429 && http.getString().indexOf("42908") >= 0) {   // the daily quota, not the rate
        ntfyQuotaUntil = clockSeconds() + NTFY_QUOTA_RETRY_S;
        sayf("ntfy: the daily message quota is used up; not publishing for %lu min\n", NTFY_QUOTA_RETRY_S / 60);
      }
    }
    http.end();
  }
  ntfyNote(code);
  wifiIoResult(code == 200 || code == 429);
  sayf("ntfy %s %d: %s (%lu ms%s)\n", slug(who).c_str(), code, text.c_str(), millis() - t0, wasKept ? ", kept connection" : "");
  return id;
}

// A status line for the phone. Every one starts with the device name: "Eliana up to date (v3)".
// If it cannot be posted now (offline, or the post fails) it is kept and posted later, in order.
const SavedList NOTES = {"nt", NOTES_MAX};
unsigned long notesRetryAt = 0;

void ntfySay(const String& text) {
  if (!ntfyReady()) return;
  String line = deviceName + " " + text;
  bool sent = listCount(NOTES) == 0 && wifiOnline() && ntfyPublish("phone", "Pebble", line).length() > 0;
  if (!sent) listPush(NOTES, line);
}

bool notesWaiting() { return listCount(NOTES) > 0 && (long)(millis() - notesRetryAt) >= 0; }   // something to post, and worth trying now

void ntfyFlush() {   // call every loop(): post one waiting report when online
  if (!wifiOnline() || listCount(NOTES) == 0 || (long)(millis() - notesRetryAt) < 0) return;
  if (ntfyPublish("phone", "Pebble", listPeek(NOTES)).length()) listPop(NOTES);
  else notesRetryAt = millis() + SEND_RETRY_MS;
}

struct NtfyMessage { String id, title, text; };   // title = the sender's name, when a device sent it
typedef void (*NtfyHandler)(const NtfyMessage& message);

// Read this device's inbox and pass each new message to `handle`, oldest first.
// Returns how many were new, or -1 if the server could not be reached.
// "New" is tracked with the server's own message time and id, saved across restarts, so the device needs no clock:
// the very first read only catches up (old messages are never replayed), and a message is never handled twice.
int ntfyPoll(NtfyHandler handle) {
  if (!ntfyReady() || !ntfyMayUse(false)) return -1;
  ntfySpend();
  bool caughtUp = prefs.getBool("inInit", false);
  unsigned long lastTime = prefs.getULong("inTime", 0);
  String lastId = prefs.getString("inId", "");

  WiFiClientSecure client;
  HTTPClient http;
  ntfyOpen(client, http);
  // The first read only catches up, so it asks for the latest message alone, not the whole cache.
  String url = topicUrl(deviceName) + "/json?poll=1&since=" + (lastTime ? String(lastTime) : caughtUp ? String("all") : String("latest"));
  int code = http.begin(client, url) ? http.GET() : -1;
  String body;   // at most NTFY_POLL_BODY_MAX bytes: the oldest messages come first, the rest wait for the next read
  if (code == 200) {
    WiFiClient* in = http.getStreamPtr();
    unsigned long t0 = millis();
    while ((int)body.length() < NTFY_POLL_BODY_MAX && (in->connected() || in->available()) && millis() - t0 < NTFY_READ_MS) {
      if (!in->available()) { delay(5); continue; }
      char chunk[256];
      int n = in->read((uint8_t*)chunk, min((int)sizeof chunk, NTFY_POLL_BODY_MAX - (int)body.length()));
      if (n > 0) body.concat(chunk, n);
    }
    int lastNl = body.lastIndexOf('\n');
    if (lastNl >= 0 && (int)body.length() >= NTFY_POLL_BODY_MAX) body.remove(lastNl + 1);   // drop a line cut off by the limit
  }
  http.end();
  ntfyNote(code);
  wifiIoResult(code == 200 || code == 429);
  if (code != 200) return -1;

  // The server lists messages from lastTime on, so the last one handled is usually listed again: new ones follow it.
  // If it has expired from the server, fall back to "strictly newer than lastTime".
  bool listed = lastId.length() && body.indexOf("\"id\":\"" + lastId + "\"") >= 0;
  bool passed = !listed;
  NtfyMessage batch[NTFY_BATCH];
  String newId = lastId;
  unsigned long newTime = lastTime;
  int count = 0;
  for (int pos = 0; pos < (int)body.length() && count < NTFY_BATCH;) {
    int nl = body.indexOf('\n', pos);
    if (nl < 0) nl = body.length();
    String line = body.substring(pos, nl);
    pos = nl + 1;
    if (line.indexOf("\"event\":\"message\"") < 0) continue;
    String id = jsonText(line, "id");
    unsigned long t = jsonNumber(line, "time");
    if (!passed) { passed = id == lastId; continue; }
    if (!listed && lastTime && t <= lastTime) continue;
    newId = id;
    newTime = t;
    if (caughtUp) batch[count++] = {id, jsonText(line, "title"), jsonText(line, "message")};
  }

  // Save the markers before handling anything: a handler may restart the device (an update).
  if (newId != lastId) { prefs.putString("inId", newId); prefs.putULong("inTime", newTime); }
  if (!caughtUp) prefs.putBool("inInit", true);
  for (int i = 0; i < count; i++) handle(batch[i]);
  return count;
}

// ---- The inbox as a held-open stream, used while a conversation is going on ----
// One request, GET <topic>/json, which the server never ends: each message arrives as a line of JSON the moment it
// is posted, so nothing has to be asked for again (and ntfy's request limit is not touched). The server also sends
// "open" and "keepalive" lines; a stream that goes silent for longer than those allow is dead and is reopened.
// The same saved markers as ntfyPoll() say what has been handled, so the two can take over from each other.
const unsigned long NTFY_STREAM_DEAD_MS = 65000;    // a keepalive comes every 45 s: this long with nothing at all means it is gone
const unsigned long NTFY_STREAM_RETRY_MS = 5000;    // wait this long before opening it again, doubling (to 40 s) each time it fails
const uint32_t NTFY_STREAM_HEAP = 60000;            // a second secure connection needs about 40 KB: do not open below this
const int NTFY_SEEN = 8;
const int NTFY_STREAM_BATCH = 3;   // messages taken from the stream per pass: they are played before the next are taken, so a burst never overflows the unread list
WiFiClientSecure* streamClient = nullptr;
String streamLine, streamSeen[NTFY_SEEN];           // the line arriving; ids handled lately (several can share one second)
int streamSeenN = 0, streamFails = 0;
unsigned long streamLastByte = 0, streamRetryAt = 0;
unsigned long streamFromTime = 0;   // where this stream started: the time and id of the last message handled before it.
String streamFromId;                // The server sends that second again; what came before that id in it is old
bool streamPassed = true;           // ...and this turns true once that id has gone by

void ntfyStreamRetryLater() {
  streamRetryAt = millis() + (NTFY_STREAM_RETRY_MS << (streamFails > 3 ? 3 : streamFails));
  streamFails++;
}

bool ntfyStreamIsOpen() { return streamClient != nullptr; }

void ntfyStreamClose() {
  if (!streamClient) return;
  streamClient->stop();
  delete streamClient;
  streamClient = nullptr;
  streamLine = "";
  sayln("Stream closed");
}

// Open the stream from where the inbox was last read. Blocks for the connection (about a second). False if it could
// not be opened: try again after NTFY_STREAM_RETRY_MS, and the ordinary reads carry on meanwhile.
bool ntfyStreamOpen() {
  ntfyStreamRetryLater();   // if this fails; cleared again on success
  if (!ntfyReady() || !ntfyMayUse(true) || !prefs.getBool("inInit", false) || ESP.getFreeHeap() < NTFY_STREAM_HEAP) return false;
  ntfySpend();
  unsigned long t0 = millis(), lastTime = prefs.getULong("inTime", 0);
  streamClient = new WiFiClientSecure();
  streamClient->setInsecure();   // certificates are not checked yet (README: Remote updates)
  streamClient->setHandshakeTimeout(5);
  streamClient->setTimeout(4000);
  int code = -1;
  if (streamClient->connect("ntfy.sh", 443, NTFY_CONNECT_MS)) {
    // HTTP/1.0, as for the other requests: the body then comes as plain lines, not in chunks
    streamClient->printf("GET /%s-%s/json?since=%s HTTP/1.0\r\nHost: ntfy.sh\r\nUser-Agent: pebble-pager\r\nAccept: */*\r\n\r\n",
                         topicBase.c_str(), slug(deviceName).c_str(), lastTime ? String(lastTime).c_str() : "30s");
    String status = streamClient->readStringUntil('\n');   // "HTTP/1.1 200 OK"
    int sp = status.indexOf(' ');
    code = sp > 0 ? status.substring(sp + 1).toInt() : -1;
    while (code == 200) {   // skip the headers, up to the empty line
      String header = streamClient->readStringUntil('\n');
      if (header.length() <= 1) break;
    }
  }
  ntfyNote(code);
  wifiIoResult(code == 200 || code == 429);
  sayf("Stream %d (%lu ms)\n", code, millis() - t0);
  if (code != 200) {
    streamClient->stop();
    delete streamClient;
    streamClient = nullptr;
    return false;
  }
  streamLine = "";
  streamLastByte = millis();
  streamFails = 0;
  streamFromTime = lastTime;
  streamFromId = prefs.getString("inId", "");
  streamPassed = streamFromId.length() == 0;
  return true;
}

// Call every loop() while the stream is open: handle what has arrived, without waiting. Returns how many messages
// were new, or -1 if the stream has ended (it is closed: open it again).
int ntfyStreamRead(NtfyHandler handle) {
  if (!streamClient) return -1;
  int handled = 0;
  while (streamClient && handled < NTFY_STREAM_BATCH && streamClient->available() > 0) {   // (a handler may close the stream)
    int c = streamClient->read();
    if (c < 0) break;
    streamLastByte = millis();
    if (c == '\r') continue;
    if (c != '\n') { if (streamLine.length() < 800) streamLine += (char)c; continue; }
    String line = streamLine;
    streamLine = "";
    if (line.length()) sayf("Stream line: %.110s\n", line.c_str());
    if (line.length() == 0 || line[0] != '{' || line.indexOf("\"event\":\"message\"") < 0) continue;   // open, keepalive, anything else
    String id = jsonText(line, "id");
    unsigned long t = jsonNumber(line, "time");
    // The stream starts with the second of the last message handled, sent again in order: everything in it up to
    // and including that message is old (as ntfyPoll() treats it). After that, a message is new unless seen lately.
    if (t < streamFromTime) { sayf("Stream skip: older than %lu\n", streamFromTime); continue; }
    if (t == streamFromTime && !streamPassed) { streamPassed = id == streamFromId; sayf("Stream skip: the same second as the last one\n"); continue; }
    bool seen = id == prefs.getString("inId", "");
    for (int i = 0; i < streamSeenN; i++) seen = seen || streamSeen[i] == id;
    if (seen) { sayf("Stream skip: seen\n"); continue; }
    if (streamSeenN == NTFY_SEEN) { for (int i = 1; i < NTFY_SEEN; i++) streamSeen[i - 1] = streamSeen[i]; streamSeenN--; }
    streamSeen[streamSeenN++] = id;
    prefs.putString("inId", id);   // before handling: a handler may restart the device (an update)
    prefs.putULong("inTime", t);
    handle({id, jsonText(line, "title"), jsonText(line, "message")});
    handled++;
  }
  if (!streamClient) return handled;   // a handler closed it
  if ((!streamClient->connected() && streamClient->available() <= 0) || millis() - streamLastByte > NTFY_STREAM_DEAD_MS) {
    ntfyStreamClose();
    ntfyStreamRetryLater();
    return handled ? handled : -1;
  }
  return handled;
}

#endif
