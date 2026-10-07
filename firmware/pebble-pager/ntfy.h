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
const int NTFY_BATCH = 8;   // most messages handled per poll; any more wait for the next one

bool ntfyReady() { return topicBase.length() > 0; }   // a base name has been set on the setup page
String topicUrl(const String& who) { return String(NTFY_SERVER) + topicBase + "-" + slug(who); }

void ntfyOpen(WiFiClientSecure& client, HTTPClient& http) {
  client.setInsecure();   // certificates are not checked yet (README: Remote updates)
  http.setConnectTimeout(NTFY_CONNECT_MS);
  http.setTimeout(NTFY_READ_MS);
  http.useHTTP10(true);   // the server closes the connection after the body: no waiting on chunked reads
}

// Post `text` to <base>-<who>. Every result feeds the Wi-Fi manager's internet check.
bool ntfyPost(const String& who, const String& text) {
  if (!ntfyReady()) return false;
  WiFiClientSecure client;
  HTTPClient http;
  ntfyOpen(client, http);
  int code = -1;
  if (http.begin(client, topicUrl(who))) {
    http.addHeader("Title", "Pebble");
    code = http.POST(text);
  }
  http.end();
  wifiIoResult(code == 200);
  sayf("ntfy %s %d: %s\n", slug(who).c_str(), code, text.c_str());
  return code == 200;
}

// A status line for the phone. Every one starts with the device name: "Eliana up to date (v3)".
bool ntfySay(const String& text) { return ntfyPost("phone", deviceName + " " + text); }

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

typedef void (*NtfyHandler)(const String& message);

// Read this device's inbox and pass each new message to `handle`, oldest first.
// Returns how many were new, or -1 if the server could not be reached.
// "New" is tracked with the server's own message time and id, saved across restarts, so the device needs no clock:
// the very first read only catches up (old messages are never replayed), and a message is never handled twice.
int ntfyPoll(NtfyHandler handle) {
  if (!ntfyReady()) return -1;
  bool caughtUp = prefs.getBool("inInit", false);
  unsigned long lastTime = prefs.getULong("inTime", 0);
  String lastId = prefs.getString("inId", "");

  WiFiClientSecure client;
  HTTPClient http;
  ntfyOpen(client, http);
  String url = topicUrl(deviceName) + "/json?poll=1&since=" + (lastTime ? String(lastTime) : String("all"));
  int code = http.begin(client, url) ? http.GET() : -1;
  String body = code == 200 ? http.getString() : String();
  http.end();
  wifiIoResult(code == 200);
  if (code != 200) return -1;

  // The server lists messages from lastTime on, so the last one handled is usually listed again: new ones follow it.
  // If it has expired from the server, fall back to "strictly newer than lastTime".
  bool listed = lastId.length() && body.indexOf("\"id\":\"" + lastId + "\"") >= 0;
  bool passed = !listed;
  String batch[NTFY_BATCH], newId = lastId;
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
    if (caughtUp) batch[count++] = jsonText(line, "message");
  }

  // Save the markers before handling anything: a handler may restart the device (an update).
  if (newId != lastId) { prefs.putString("inId", newId); prefs.putULong("inTime", newTime); }
  if (!caughtUp) prefs.putBool("inInit", true);
  for (int i = 0; i < count; i++) handle(batch[i]);
  return count;
}

#endif
