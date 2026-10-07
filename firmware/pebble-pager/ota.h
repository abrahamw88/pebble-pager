// Remote firmware updates from a GitHub release.
// The release holds manifest.txt (version, size, sha256) and the firmware file. A newer file is installed only
// if its size and SHA-256 match. The new image is on probation until it reaches the update server; if it crashes
// first, the bootloader goes back to the old image and that version is never tried again. A power cut during an
// update is not a crash: the old image keeps running and the update is tried again.
#ifndef PEBBLE_OTA_H
#define PEBBLE_OTA_H
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "mbedtls/sha256.h"
#include "config.h"
#include "ntfy.h"

#ifdef OTA_LOCAL_IP   // -DOTA_LOCAL_IP=192.168.x.y : test against a local web server on port 8000
#define OTA_STR_(x) #x
#define OTA_STR(x) OTA_STR_(x)
#define OTA_BASE "http://" OTA_STR(OTA_LOCAL_IP) ":8000/"
#else
#define OTA_BASE "https://github.com/abrahamw88/pebble-pager/releases/latest/download/"
#endif
#ifndef DAILY_CHECK_MS   // fallback check when no "update" command arrives; override for testing
#define DAILY_CHECK_MS (24UL * 60 * 60 * 1000)
#endif

const char* MANIFEST_FILE = "manifest.txt";   // lines: version=N, size=BYTES, sha256=HEX
const char* BIN_FILE = "pebble-pager.bin";
const int OTA_OK = 0, OTA_RETRY = 1, OTA_BAD_IMAGE = 2;   // install result: retry later, or never again
const int OTA_CRASHED = 1, OTA_INTERRUPTED = 2;           // why an update did not stay ("otaWhy")

// Keep a new image on probation until updateValidate() runs.
extern "C" bool verifyRollbackLater() { return true; }

bool updatePending = true;       // true until this boot has reached the update server
const char* updateNote = "";     // why the last check installed nothing: "current", "skipped", "none" or ""
int updateSeen = 0;              // the release version the last check saw
int updateHttp = 0;              // last HTTP status; 0 or less means the server was never reached
const char* updateReason = "";   // why the last install failed

// GET `url`, following redirects (GitHub release downloads redirect to another host).
bool updateOpen(HTTPClient& http, WiFiClient& plain, WiFiClientSecure& secure, const String& url) {
  secure.setInsecure();   // certificates are not checked yet; the SHA-256 guards against a corrupted file only
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  http.useHTTP10(true);
  bool begun = url.startsWith("https") ? http.begin(secure, url) : http.begin(plain, url);
  updateHttp = begun ? http.GET() : -1;
  sayf("GET %s %d\n", url.substring(url.lastIndexOf('/') + 1).c_str(), updateHttp);
  return updateHttp == 200;
}

String manifestField(const String& text, const char* key) {   // value of key=value on its own line
  String k = String(key) + "=";
  int i = text.indexOf(k);
  if (i < 0) return "";
  i += k.length();
  int j = text.indexOf('\n', i);
  String v = j < 0 ? text.substring(i) : text.substring(i, j);
  v.trim();
  return v;
}

int updateInstall(size_t size, const String& wantSha) {
  WiFiClient plain;
  WiFiClientSecure secure;
  HTTPClient http;
  if (!updateOpen(http, plain, secure, String(OTA_BASE) + BIN_FILE)) { http.end(); updateReason = "download failed"; return OTA_RETRY; }
  if (http.getSize() != (int)size) { http.end(); updateReason = "bad size"; return OTA_BAD_IMAGE; }
  if (!Update.begin(size)) { http.end(); updateReason = "too big"; return OTA_BAD_IMAGE; }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  size_t done = 0;
  int lastTenth = -1;
  bool wrote = true;
  for (unsigned long lastData = millis(); done < size && wrote && millis() - lastData < 20000;) {
    int avail = stream->available();
    if (avail <= 0) { delay(5); continue; }
    int n = stream->readBytes(buf, min((size_t)avail, sizeof(buf)));
    if (n <= 0) continue;
    lastData = millis();
    wrote = Update.write(buf, n) == (size_t)n;
    mbedtls_sha256_update(&sha, buf, n);
    done += n;
    if ((int)(done * 10 / size) != lastTenth) { lastTenth = done * 10 / size; sayf("  %d%%\n", lastTenth * 10); }
  }
  http.end();
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  if (!wrote || done != size) {   // flash trouble or the download stalled: nothing is installed
    Update.abort();
    updateReason = wrote ? "incomplete" : "write failed";
    return OTA_RETRY;
  }
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", digest[i]);
  if (!wantSha.equalsIgnoreCase(hex)) { Update.abort(); updateReason = "bad hash"; return OTA_BAD_IMAGE; }
  if (!Update.end(true)) { updateReason = "bad image"; return OTA_BAD_IMAGE; }
  return OTA_OK;
}

// Look for a newer release. Returns true if one was installed (the caller then restarts).
// `*reached` is set when the server answered at all: even a 404 proves Wi-Fi and the internet work.
bool updateLook(bool* reached) {
  updateNote = "";
  WiFiClient plain;
  WiFiClientSecure secure;
  HTTPClient http;
  bool ok = updateOpen(http, plain, secure, String(OTA_BASE) + MANIFEST_FILE);
  String manifest = ok ? http.getString() : String();
  http.end();
  *reached = updateHttp > 0;
  if (!ok) { if (*reached) updateNote = "none"; return false; }   // answered, but no release to read

  int remote = manifestField(manifest, "version").toInt();
  size_t size = manifestField(manifest, "size").toInt();
  String sha = manifestField(manifest, "sha256");
  updateSeen = remote;
  sayf("Running v%d, release v%d\n", FW_VERSION, remote);
  if (remote == prefs.getInt("otaBad", 0)) { updateNote = "skipped"; return false; }
  if (remote <= FW_VERSION) { updateNote = "current"; return false; }
  if (size == 0 || sha.length() != 64) { sayln("Bad manifest"); return false; }

  sayf("Installing v%d (%u B)\n", remote, (unsigned)size);
  int result = updateInstall(size, sha);
  if (result == OTA_RETRY) { sayf("v%d not installed (%s): will retry\n", remote, updateReason); return false; }
  if (result == OTA_BAD_IMAGE) {
    prefs.putInt("otaBad", remote);
    ntfySay("failed update to v" + String(remote) + ": " + updateReason + ", still on v" + String(FW_VERSION));
    return false;
  }
  prefs.putInt("otaFrom", FW_VERSION);
  prefs.putInt("otaTo", remote);
  prefs.remove("otaWhy");
  return true;
}

// Call once at boot, before any network use. If an update was started but this is not the version it installed,
// the new image did not stay: work out why, once. Only a crash marks the version as bad.
void updateBoot() {
#ifdef FW_CRASH   // an image that crashes before it can validate, to test rollback
  sayln("FW_CRASH: aborting");
  delay(500);
  abort();
#endif
  int to = prefs.getInt("otaTo", 0);
  if (to == 0 || to == FW_VERSION || prefs.getInt("otaWhy", 0)) return;
  esp_reset_reason_t why = esp_reset_reason();
  bool crashed = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT;
  if (crashed) prefs.putInt("otaBad", to);
  prefs.putInt("otaWhy", crashed ? OTA_CRASHED : OTA_INTERRUPTED);
}

// The running image reached the server: mark it good, and tell the phone how the last update went.
void updateValidate() {
  esp_ota_mark_app_valid_cancel_rollback();
  updatePending = false;
  sayln("Image valid");
  int to = prefs.getInt("otaTo", 0), from = prefs.getInt("otaFrom", 0);
  if (to == 0) return;
  String msg = to == FW_VERSION ? "updated from v" + String(from) + " to v" + String(to)
               : prefs.getInt("otaWhy", 0) == OTA_INTERRUPTED ? "update to v" + String(to) + " interrupted, will retry"
               : "failed update to v" + String(to) + ", still on v" + String(FW_VERSION);
  if (!ntfySay(msg)) return;   // not delivered: said again on the next boot
  prefs.remove("otaTo");
  prefs.remove("otaFrom");
  prefs.remove("otaWhy");
}

// Check for an update now. `asked` = the phone sent "update", so it gets an answer either way.
// Returns true if the update server answered.
bool updateCheck(bool asked) {
  bool reached = false;
  if (asked) ntfySay("checking for update");
  if (updateLook(&reached)) {
    sayln("Installed, restarting");
    delay(500);
    ESP.restart();
  }
  wifiIoResult(reached);
  if (reached && updatePending) updateValidate();
  if (!asked) return reached;
  if (!reached) ntfySay("can't reach server, still on v" + String(FW_VERSION));
  else if (!strcmp(updateNote, "current")) ntfySay("up to date (v" + String(FW_VERSION) + ")");
  else if (!strcmp(updateNote, "none")) ntfySay("no update published, still on v" + String(FW_VERSION));
  else if (!strcmp(updateNote, "skipped")) ntfySay("skipped v" + String(updateSeen) + " (failed before), still on v" + String(FW_VERSION));
  return reached;   // installed, rejected and failed outcomes are reported where they happen
}

#endif
