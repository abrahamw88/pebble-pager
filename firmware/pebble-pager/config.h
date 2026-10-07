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
const int BOOT_BTN = 9;    // onboard button: stands in for button 2 until the parts are wired

// ---- Values to tune ----
const unsigned long SETUP_HOLD_MS = 5000;        // hold, then release: open the setup page
const unsigned long CLEAR_HOLD_MS = 10000;       // hold, then release: forget all Wi-Fi networks
const unsigned long INBOX_POLL_MS = 30000;       // how often the device reads its own topic
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

// ---- USB serial console, for setup without a phone ----
//   show                                   current settings (the base is not printed)
//   set name|color|partner|base <value>    change one setting
void consoleTick() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (line.length() < 100) line += c; continue; }
    line.trim();
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
    if (line.length())
      sayf("v%d color=%s partner=%s base=%s networks=%d\n", FW_VERSION, deviceColor.c_str(),
           partnerName.length() ? partnerName.c_str() : "(none)", topicBase.length() ? "(set)" : "(none)", savedCount());
    line = "";
  }
}

#endif
