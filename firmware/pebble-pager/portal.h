// Setup page: a Wi-Fi hotspot with one web page for the device's name, color, partner, topic base and
// Wi-Fi networks. It opens only from the button (5 s hold), and closes after a join or after 5 minutes.
#ifndef PEBBLE_PORTAL_H
#define PEBBLE_PORTAL_H
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "config.h"

const char* SETUP_SSID = "pebblepager";
const char* SETUP_PASS = "pebblepager";   // placeholder; should become a per-device value
const unsigned long SETUP_TIMEOUT_MS = 5UL * 60 * 1000;
const unsigned long PORTAL_JOIN_TIMEOUT_MS = 15000;
const char* PAGE_HEAD = "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
                        "<title>Pebble setup</title><body style='font-family:sans-serif;max-width:28em;margin:1em auto;padding:0 1em'>";
const char* FIELD = " style='font-size:1.1em;width:100%;box-sizing:border-box'";
const char* BUTTON = "<p><button style='font-size:1.1em;padding:.6em 1.2em'>";

WebServer server(80);
DNSServer dns;
unsigned long portalStart = 0;
bool joinedViaPortal = false;
String scanHtml;   // networks seen by the last scan, as <option> tags

String esc(const String& s) {
  String o;
  for (char c : s) {
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

String netKey(char kind, int i) { return String(kind) + String(i); }   // "s3" = name, "p3" = password of saved network 3

// Newest network goes first; a repeated name is replaced; the list is capped.
void saveNetwork(const String& ssid, const String& pass) {
  String ss[MAX_NETWORKS], pp[MAX_NETWORKS];
  int n = 0, old = savedCount();
  ss[n] = ssid; pp[n] = pass; n++;
  for (int i = 0; i < old && n < MAX_NETWORKS; i++) {
    String s = prefs.getString(netKey('s', i).c_str(), "");
    if (s == ssid) continue;
    ss[n] = s; pp[n] = prefs.getString(netKey('p', i).c_str(), ""); n++;
  }
  for (int i = 0; i < n; i++) {
    prefs.putString(netKey('s', i).c_str(), ss[i]);
    prefs.putString(netKey('p', i).c_str(), pp[i]);
  }
  prefs.putInt("n", n);
}

// Remove saved network `index`, shifting later ones up.
void deleteNetwork(int index) {
  int n = savedCount();
  if (index < 0 || index >= n) return;
  for (int i = index; i < n - 1; i++) {
    prefs.putString(netKey('s', i).c_str(), prefs.getString(netKey('s', i + 1).c_str(), ""));
    prefs.putString(netKey('p', i).c_str(), prefs.getString(netKey('p', i + 1).c_str(), ""));
  }
  prefs.remove(netKey('s', n - 1).c_str());
  prefs.remove(netKey('p', n - 1).c_str());
  prefs.putInt("n", n - 1);
}

// Scan for networks. Only done before the hotspot starts or when the page asks: a scan while the hotspot
// is up makes the radio hop channels for a few seconds and can drop the phone's connection.
void refreshScan() {
  int found = WiFi.scanNetworks();
  scanHtml = "";
  for (int i = 0; i < found; i++) {
    String option = "<option>" + esc(WiFi.SSID(i)) + "</option>";
    if (WiFi.SSID(i).length() && scanHtml.indexOf(option) < 0) scanHtml += option;   // skip hidden and repeated names
  }
  WiFi.scanDelete();
  sayf("Scan: %d networks\n", found);
}

void redirectHome() {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Location", "http://192.168.4.1/");
  server.send(302, "text/plain", "");
}

// What to use in the ntfy app. Built fresh on every page load, so it follows the name, partner and base as they change.
String topicHelp() {
  String h = "<h3>On your phone</h3>";
  if (topicBase.length() == 0) return h + "<p>Enter a topic base above and save. The topics to use in the ntfy app will appear here.</p>";
  h += "<p>In the ntfy app (default server ntfy.sh), tap + and subscribe to this topic. Messages from the pager arrive there:</p>"
       "<p style='font-size:1.15em'><b>" + esc(topicBase) + "-phone</b></p>"
       "<p>To send to this device, post to <b>" + esc(topicBase) + "-" + slug(deviceName) + "</b>. "
       "The text <code>update</code> makes it check for new firmware.</p>";
  if (partnerName.length())
    h += "<p>To send to " + esc(partnerName) + ", post to <b>" + esc(topicBase) + "-" + slug(partnerName) + "</b>.</p>";
  return h;
}

void handleRoot() {
  String page = String(PAGE_HEAD) + "<h2>Pebble setup</h2><form method=post action=/device><h3>Device</h3>"
                "<p><label>Name<br><input name=name value=\"" + esc(deviceName) + "\" maxlength=" + String(NAME_MAX_LEN) + FIELD + "></label></p>"
                "<p><label>Color<br><select name=color" + FIELD + ">";
  for (int i = 0; i < COLOR_COUNT; i++)
    page += String("<option") + (deviceColor == COLOR_NAMES[i] ? " selected" : "") + ">" + COLOR_NAMES[i] + "</option>";
  page += String("</select></label></p>"
          "<p><label>Partner's name<br><input name=partner value=\"") + esc(partnerName) + "\" maxlength=" + String(NAME_MAX_LEN) + FIELD + "></label></p>"
          "<p><label>Topic base, the same on both devices and the phone<br><input name=base maxlength=" + String(BASE_MAX_LEN) +
          " placeholder='" + (topicBase.length() ? "saved; leave empty to keep" : "e.g. pebble-xxxxxxx") + "'" + FIELD + "></label></p>" +
          BUTTON + "Save device</button></p></form>" + topicHelp() +
          "<form method=post action=/save><h3>Wi-Fi</h3>"
          "<p><label>Network<br><select name=ssid" + FIELD + ">" + scanHtml + "</select></label></p>"
          "<p><label>Password<br><input name=pass type=password" + FIELD + "></label></p>" +
          BUTTON + "Save and join</button></p></form>"
          "<p>2.4 GHz networks only. <a href=/rescan>Rescan</a> (the page pauses for a few seconds).</p>";
  int n = savedCount();
  if (n > 0) page += "<h3>Saved networks (" + String(n) + " of " + String(MAX_NETWORKS) + ")</h3>";
  if (n >= MAX_NETWORKS) page += "<p>The list is full: saving another removes the oldest. Delete one below to choose which.</p>";
  for (int i = 0; i < n; i++)
    page += "<form method=post action=/delete style='display:flex;justify-content:space-between;align-items:center;margin:.4em 0'>"
            "<span>" + esc(prefs.getString(netKey('s', i).c_str(), "")) + "</span><input type=hidden name=i value=" + String(i) + ">"
            "<button style='font-size:1em;padding:.4em .9em'>Delete</button></form>";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", page + "</body>");
}

void handleDevice() {
  String base = server.arg("base");
  base.trim();
  saveIdentity(server.arg("name"), server.arg("color"), server.arg("partner"), base.length() ? base : topicBase);
  sayf("Device saved: %s, partner %s\n", deviceColor.c_str(), partnerName.length() ? partnerName.c_str() : "(none)");
  redirectHome();
}

void handleSave() {
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  if (ssid.length() == 0) { server.send(400, "text/plain", "Pick a network."); return; }
  saveNetwork(ssid, pass);
  WiFi.begin(ssid.c_str(), pass.c_str());
  for (unsigned long t0 = millis(); WiFi.status() != WL_CONNECTED && millis() - t0 < PORTAL_JOIN_TIMEOUT_MS;) delay(250);
  joinedViaPortal = WiFi.status() == WL_CONNECTED;
  server.send(200, "text/html", String(PAGE_HEAD) + (joinedViaPortal
    ? "<h2>Connected to " + esc(ssid) + "</h2><p>Saved. You can leave this page.</p>"
    : "<h2>Could not join " + esc(ssid) + "</h2><p>Saved anyway. Check the password and <a href=/>try again</a>.</p>") + "</body>");
  sayf("Setup: %s\n", joinedViaPortal ? "joined" : "join failed");
}

void startPortal() {
  sayf("Setup: join \"%s\"\n", SETUP_SSID);
  WiFi.mode(WIFI_AP_STA);
  refreshScan();
  WiFi.softAP(SETUP_SSID, SETUP_PASS);
  WiFi.AP.enableDhcpCaptivePortal();     // tells phones where the setup page is, so it opens by itself
  dns.start(53, "*", WiFi.softAPIP());   // every name points here
  server.on("/", handleRoot);
  server.on("/device", HTTP_POST, handleDevice);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/delete", HTTP_POST, []() { deleteNetwork(server.arg("i").toInt()); redirectHome(); });
  server.on("/rescan", []() { refreshScan(); redirectHome(); });
  server.onNotFound(redirectHome);       // a phone's own "is there a setup page" check lands here
  server.begin();
  portalOn = true;
  joinedViaPortal = false;
  portalStart = millis();
}

void stopPortal() {
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalOn = false;
  sayln("Setup ended");
}

// Call every loop(). Returns true once, when a network was joined through the page (the hotspot is closed by then).
bool portalTick() {
  if (!portalOn) return false;
  server.handleClient();
  if (joinedViaPortal) {
    for (unsigned long t0 = millis(); millis() - t0 < 3000;) { server.handleClient(); delay(5); }   // let the phone show the result
    stopPortal();
    return true;
  }
  if (millis() - portalStart > SETUP_TIMEOUT_MS) stopPortal();
  return false;
}

#endif
