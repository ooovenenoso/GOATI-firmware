// Stub ble_spam.h — BLE Spam was removed from the firmware in v0.6.x.
// The rest of the code (llm.h, telegram.h, shell.h) still references
// ble_spam symbols; this stub provides them as no-ops so the rest of
// the firmware compiles without modification.
#pragma once
enum BleSpamMode : uint8_t {
  BLE_SPAM_OFF = 0,
  BLE_SPAM_APPLE,
  BLE_SPAM_SAMSUNG,
  BLE_SPAM_FASTPAIR,
  BLE_SPAM__COUNT
};
static const char* const BLE_SPAM_NAMES[] = {"OFF", "APPLE", "SAMSUNG", "FASTPAIR"};
static BleSpamMode g_ble_spam_mode = BLE_SPAM_OFF;
static bool  g_ble_spam_running = false;
static uint32_t g_ble_spam_pkt_count = 0;
static uint32_t g_ble_spam_start_ms = 0;
static bool ble_spam_init() { return true; }
static void ble_spam_start(uint8_t m) { (void)m; }
static void ble_spam_start_combined() {}
static void ble_spam_stop() {}
static void ble_spam_loop() {}
static void ble_spam_cycle_mode() {}
