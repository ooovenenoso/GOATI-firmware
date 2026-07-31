#pragma once
#include <WiFi.h>
#include "display.h"   // For disp_set_state() + DISP_* enum values

// ─── WiFi ────────────────────────────────────────────────────────────────────
static void wifi_connect(uint8_t retries) {
  if (!g_cfg.wifi_ssid[0]) {
    Serial.println(F("[WiFi] no SSID set"));
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("[WiFi] already connected"));
    disp_set_state(DISP_HOME);
    return;
  }

  Serial.printf("[WiFi] connecting to '%s'\r\n", g_cfg.wifi_ssid);
  disp_set_state(DISP_WIFI_CONNECTING);
#ifndef ARDUINO_USB_CDC_ON_BOOT
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(100);
#endif
  WiFi.begin(g_cfg.wifi_ssid, g_cfg.wifi_pass);

  // Wait up to retries × 200ms = 8s for connection
  for (uint8_t i = 0; i < retries && WiFi.status() != WL_CONNECTED; ++i) {
    Serial.print(".");
    delay(200);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\r\n[WiFi] OK → IP %s RSSI %d dBm\r\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    disp_set_state(DISP_HOME);
  } else {
    Serial.println(F("\r\n[WiFi] FAIL (timeout)"));
    disp_set_state(DISP_WIFI_OFF);
  }
}