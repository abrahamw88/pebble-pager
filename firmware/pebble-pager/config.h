// Pins, tunable values, saved settings and serial output. Included first: every other tab builds on it.
#ifndef PEBBLE_CONFIG_H
#define PEBBLE_CONFIG_H
#include <Arduino.h>
#include <Preferences.h>
#include <stdarg.h>

#ifndef FW_VERSION
#define FW_VERSION 1
#endif

// ---- Pins (never use D0, D6, D7, D8, D9) ----
const int BATT = D1;
const int BTN1 = D2;       // large: view and send
const int BTN2 = D3;       // small: battery, setup, off
const int MOTOR = D4;
const int RING_PWR = D5;
const int RING_DATA = D10;
const int BOOT_BTN = 9;    // onboard button: acts as button 2 (or button 1, console "boot 1") until the parts are wired

// ---- Values to tune ----
const unsigned long SETUP_HOLD_MS = 5000;        // hold, then release: open (or close) the setup page
const unsigned long OFF_HOLD_MS = 10000;         // hold, then release: turn the device off
const unsigned long DEBOUNCE_MS = 25;            // a button must stay changed this long to count
const unsigned long RECORD_HOLD_MS = 500;        // hold button 1 this long to start recording
const unsigned long SEND_PAUSE_MS = 2000;        // a recording is sent after this long without a press
const unsigned long INBOX_POLL_MS = 30000;       // how often the device reads its own topic
const unsigned long RECEIPT_POLL_MS = 5000;      // ...and how often while a sent message waits for its receipt
const unsigned long RECEIPT_WAIT_MS = 45000;     // give up on a receipt after this
const unsigned long SEND_RETRY_MS = 10000;       // wait between attempts to post a queued message
const int MAX_PRESSES = 12;                      // presses in one message
const unsigned long MAX_RECORD_MS = 15000;       // total length of one message
const int OUTBOX_MAX = 5;                        // recorded messages waiting to be posted
const int UNREAD_MAX = 3;                        // received messages waiting to be played
const int NOTES_MAX = 5;                         // reports for the phone waiting to be posted
#ifndef QUEUE_MAX_AGE_S                          // anything waiting longer is deleted; override for testing
#define QUEUE_MAX_AGE_S (24UL * 60 * 60)
#endif
const int MAX_NETWORKS = 10;
const int NAME_MAX_LEN = 20;
const int BASE_MAX_LEN = 40;

// ---- Settings, saved under "pebble" ----
// name, color, partner, base, n + s0..s9 + p0..p9 (Wi-Fi), plus markers owned by the ntfy and update tabs.
// Nothing secret is compiled in: the firmware file is public, so topics and networks live only on the device.
const char* DEFAULT_NAME = "Eliana";
const char* DEFAULT_COLOR = "Pink";
const int COLOR_COUNT = 8;   // dropdown order on the setup page; the RGB values are for the ring
const char* COLOR_NAMES[COLOR_COUNT] = {"Pink", "Blue", "Green", "Purple", "Orange", "Teal", "Yellow", "Red"};
const uint32_t COLOR_RGB[COLOR_COUNT] = {0xF29BB5, 0x7FB2F0, 0x6CC795, 0xA98BE0, 0xF08A4B, 0x5CC9C0, 0xF2D45C, 0xE8736B};

Preferences prefs;
String deviceName = DEFAULT_NAME;   // also this device's inbox: <base>-<name>
String deviceColor = DEFAULT_COLOR;
String partnerName;                 // the other device's name: its inbox is <base>-<partner>
String topicBase;                   // shared by both devices and the phone, e.g. pebble-xxxxxxx
bool portalOn = false;              // true while the setup page is open

int colorIndex(const String& name) {
  for (int i = 0; i < COLOR_COUNT; i++)
    if (name.equalsIgnoreCase(COLOR_NAMES[i])) return i;
  return -1;
}

// Keep letters, digits and the characters in `extra`; trim; cut to `maxLen`.
String cleanText(const String& raw, const char* extra, int maxLen) {
  String out;
  for (unsigned i = 0; i < raw.length() && (int)out.length() < maxLen; i++) {
    char c = raw[i];
    if (isalnum((unsigned char)c) || (c && strchr(extra, c))) out += c;
  }
  out.trim();
  return out;
}

// A name as it appears in a topic: lower case, with runs of anything else as one hyphen. "Eliana B." -> "eliana-b"
String slug(const String& name) {
  String out;
  for (unsigned i = 0; i < name.length(); i++) {
    char c = name[i];
    if (isalnum((unsigned char)c)) out += (char)tolower(c);
    else if (out.length() && !out.endsWith("-")) out += '-';
  }
  while (out.endsWith("-")) out.remove(out.length() - 1);
  return out;
}

int savedCount() { return prefs.getInt("n", 0); }

void loadSettings() {
  deviceName = cleanText(prefs.getString("name", DEFAULT_NAME), " -_.", NAME_MAX_LEN);
  if (slug(deviceName).length() == 0 || slug(deviceName) == "phone") deviceName = DEFAULT_NAME;
  int c = colorIndex(prefs.getString("color", DEFAULT_COLOR));
  deviceColor = COLOR_NAMES[c < 0 ? 0 : c];
  partnerName = cleanText(prefs.getString("partner", ""), " -_.", NAME_MAX_LEN);
  if (slug(partnerName) == slug(deviceName) || slug(partnerName) == "phone") partnerName = "";   // must be someone else
  topicBase = cleanText(prefs.getString("base", ""), "-_", BASE_MAX_LEN);
}

// Save the four identity settings. A changed name or base means a different inbox, so its read markers are reset.
void saveIdentity(const String& name, const String& color, const String& partner, const String& base) {
  String oldInbox = topicBase + "-" + slug(deviceName);
  prefs.putString("name", name);
  prefs.putString("color", color);
  prefs.putString("partner", partner);
  prefs.putString("base", base);
  loadSettings();
  if (topicBase + "-" + slug(deviceName) != oldInbox) {
    prefs.remove("inInit");
    prefs.remove("inTime");
    prefs.remove("inId");
  }
}

void settingsBegin() {
  prefs.begin("pebble", false);
  loadSettings();
}

// ---- A short list of text items saved in settings, oldest first, each stamped with when it was added ----
struct SavedList { const char* key; int cap; };

// Seconds on the chip's own clock. It keeps counting through deep sleep and restarts from zero after a power cut,
// which is all an age limit needs: no network time is used.
unsigned long clockSeconds() { return (unsigned long)time(nullptr); }

String listKey(const SavedList& l, int i) { return String(l.key) + String(i); }
int listCount(const SavedList& l) { return prefs.getInt((String(l.key) + "n").c_str(), 0); }
void listSetCount(const SavedList& l, int n) { prefs.putInt((String(l.key) + "n").c_str(), n); }
String listRaw(const SavedList& l, int i) { return prefs.getString(listKey(l, i).c_str(), ""); }   // "<stamp> <text>"

String listPeek(const SavedList& l) {   // the oldest item's text, or "" if the list is empty
  if (listCount(l) == 0) return "";
  String raw = listRaw(l, 0);
  return raw.substring(raw.indexOf(' ') + 1);
}

void listPop(const SavedList& l) {   // remove the oldest item
  int n = listCount(l);
  if (n == 0) return;
  for (int i = 0; i < n - 1; i++) prefs.putString(listKey(l, i).c_str(), listRaw(l, i + 1));
  prefs.remove(listKey(l, n - 1).c_str());
  listSetCount(l, n - 1);
}

void listPush(const SavedList& l, const String& text) {   // add as the newest; a full list drops its oldest
  if (listCount(l) >= l.cap) listPop(l);
  int n = listCount(l);
  prefs.putString(listKey(l, n).c_str(), String(clockSeconds()) + " " + text);
  listSetCount(l, n + 1);
}

// Delete items older than QUEUE_MAX_AGE_S. A stamp from before a power cut is later than "now": restart its age.
void listExpire(const SavedList& l) {
  unsigned long now = clockSeconds();
  for (int i = listCount(l) - 1; i >= 0; i--) {
    String raw = listRaw(l, i);
    if (strtoul(raw.c_str(), nullptr, 10) > now) prefs.putString(listKey(l, i).c_str(), String(now) + raw.substring(raw.indexOf(' ')));
  }
  while (listCount(l) > 0 && now - strtoul(listRaw(l, 0).c_str(), nullptr, 10) > QUEUE_MAX_AGE_S) listPop(l);
}

// ---- Serial output with the device name in front: "Eliana: ...". Indented lines and dots stay plain. ----
void sayf(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  const char* m = buf;
  while (*m == '\n') { Serial.print('\n'); m++; }
  if (*m != ' ' && *m != '.' && *m != 0) Serial.printf("%s: ", deviceName.c_str());
  Serial.print(m);
}
void sayln(const char* msg) { sayf("%s\n", msg); }

// ---- USB serial console, for setup and testing without a phone ----
//   show                                   current settings (the base is not printed)
//   set name|color|partner|base <value>    change one setting
// The main tab adds: send <message>, play, press <1|2> <times>, boot <1|2>.
bool consoleRead(String& line) {   // true when a whole line has been typed
  static String typed;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (typed.length() < 120) typed += c; continue; }
    typed.trim();
    line = typed;
    typed = "";
    if (line.length()) return true;
  }
  return false;
}

void consoleSettings(const String& line) {
  if (line.startsWith("set ")) {
    int sp = line.indexOf(' ', 4);
    String key = sp < 0 ? line.substring(4) : line.substring(4, sp), value = sp < 0 ? "" : line.substring(sp + 1);
    value.trim();
    if (key == "name") saveIdentity(value, deviceColor, partnerName, topicBase);
    else if (key == "color") saveIdentity(deviceName, value, partnerName, topicBase);
    else if (key == "partner") saveIdentity(deviceName, deviceColor, value, topicBase);
    else if (key == "base") saveIdentity(deviceName, deviceColor, partnerName, value);
    else sayln("set name|color|partner|base <value>");
  }
  sayf("v%d color=%s partner=%s base=%s networks=%d\n", FW_VERSION, deviceColor.c_str(),
       partnerName.length() ? partnerName.c_str() : "(none)", topicBase.length() ? "(set)" : "(none)", savedCount());
}

void consoleTick() {   // settings commands only (the hardware checks use this)
  String line;
  if (consoleRead(line)) consoleSettings(line);
}

#endif
