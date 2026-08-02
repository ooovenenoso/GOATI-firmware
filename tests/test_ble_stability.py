"""Behavioral tests for the BLE Spam / BadUSB firmware modules.

These tests validate *intent* — they parse the firmware sources to verify
that the build satisfies documented behavior:

* BadUSB never calls BLEDevice::init a second time after BleKeyboard
  already initialized it.
* BadUSB does not block 300 ms between isConnected() and the first HID
  write.
* BadUSB restarts advertising via BLEAdvertising::start() after a spam
  session releases the controller, so GOATI-KB reappears in scan
  results immediately.
* BadUSB emits the first HID report on the connection event without
  demanding 500 ms of "stable" connectivity.
* BLE Spam registers a GAP callback that waits for the
  ADV_DATA_RAW_SET_COMPLETE_EVT before starting advertising.
* The Apple SourApple payload uses the documented Continuity Protocol
  layout.
* The Apple AppleJuice (proximity-pairing) payload carries the required
  0x07 subtype byte and the Apple manufacturer ID 0x4C00.
* BLE Spam rotates the random MAC on every emit and uses the official
  0xC0 top-bit marker for static random addresses.
* BLE Spam resets the controller's random address to all zeros on stop.
* BLE Spam emits at a burst interval (50-200 ms) that gives Bluedroid
  enough time to apply each advertising buffer.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "firmware" / "include"


def source(name: str) -> str:
    return (INCLUDE / name).read_text(encoding="utf-8")


BADUSB = source("badusb.h")
SPAM = source("ble_spam.h")


def _function_body(src: str, signature: str) -> str:
    """Return the body of the first *definition* of `signature` in `src`.

    A definition is detected by walking backwards from the identifier
    and requiring a declaration keyword (e.g. `void`, `static`) to
    appear in the immediately preceding whitespace-trimmed text.
    Call sites inside another function and references inside
    comments are filtered out.
    """
    needle = signature + "("
    idx = -1
    cursor = 0
    while cursor < len(src):
        found = src.find(needle, cursor)
        if found == -1:
            break
        prev = found - 1
        preceding = src[prev] if prev >= 0 else " "
        if preceding.isalnum() or preceding == "_" or preceding == "`":
            cursor = found + 1
            continue
        # Skip occurrences inside /* */ block comments.
        before = src.rfind("/*", 0, found)
        close = src.rfind("*/", 0, found)
        if before != -1 and (close == -1 or close < before):
            cursor = found + 1
            continue
        # Skip occurrences inside // line comments.
        line_start = src.rfind("\n", 0, found)
        if line_start != -1 and "//" in src[line_start + 1 : found]:
            cursor = found + 1
            continue
        # The first non-whitespace token before the identifier must be a
        # C declaration keyword. If it's an identifier (e.g. another
        # function name), we're at a call site / reference, not the
        # definition.
        j = found - 1
        while j >= 0 and src[j] in (" ", "\t"):
            j -= 1
        end = j + 1
        while j >= 0 and src[j] not in (" ", "\t", "\n", "*", "("):
            j -= 1
        preceding_token = src[j + 1 : end]
        # Tokens may include trailing '*' for pointer return types; strip
        # those so we only see the base keyword.
        preceding_token = preceding_token.rstrip("*")
        # Capture full "static void" / "static inline" prefixes too.
        back = j
        while back >= 0 and src[back] in (" ", "\t", "*"):
            back -= 1
        # back now points at the last char of the previous token; walk
        # to its start.
        k = back
        while k > 0 and src[k - 1] not in (" ", "\t", "\n", ";", "{", "("):
            k -= 1
        prev_prev_token = src[k : back + 1].strip()
        accepted = {
            "void", "static", "inline", "const", "extern", "bool",
            "int", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
            "int8_t", "int16_t", "int32_t", "int64_t", "char",
            "float", "double", "size_t", "BleSpamMode",
            "esp_err_t",
        }
        if (preceding_token not in accepted
                and prev_prev_token not in accepted):
            cursor = found + 1
            continue
        # Distinguish a definition from a forward declaration: skip
        # signatures that close their paren and end with `;` without
        # a body. We look at the characters between '(' and the matching
        # ')' and check that there's a '{' before the next ';'.
        depth = 1
        m = found + len(needle)
        while m < len(src) and depth > 0:
            if src[m] == "(":
                depth += 1
            elif src[m] == ")":
                depth -= 1
            m += 1
        tail = src[m:m + 16].lstrip()
        if not tail.startswith("{"):
            cursor = found + 1
            continue
        idx = found
        break
    if idx == -1:
        raise AssertionError(f"signature not found: {signature}")
    open_brace = src.find("{", idx)
    if open_brace == -1:
        raise AssertionError(f"open brace not found after: {signature}")
    depth = 1
    i = open_brace + 1
    while i < len(src) and depth:
        c = src[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    if depth:
        raise AssertionError(f"unbalanced braces in: {signature}")
    return src[open_brace + 1 : i - 1]


class BleStabilityContracts(unittest.TestCase):
    # ────── BadUSB (BLE HID keyboard) ──────
    def test_badusb_init_does_not_reinitialize_bluedroid(self):
        # Allow "BLEDevice::init" only inside comments / docstrings.
        code = re.sub(r"/\*.*?\*/", "", BADUSB, flags=re.DOTALL)
        code = re.sub(r"//.*?$", "", code, flags=re.MULTILINE)
        self.assertNotIn("BLEDevice::init", code,
            "BadUSB must rely on BleKeyboard's begin() for init")

    def test_badusb_run_payload_does_not_block_300ms_before_first_keystroke(self):
        body = _function_body(BADUSB, "badusb_run_payload")
        # The guard is `g_badusb_connected_latched` (debounced inside
        # badusb_loop). It must precede any delay(300) and any call to
        # the inner executor that issues HID writes.
        guard = body.find("g_badusb_connected_latched")
        self.assertNotEqual(guard, -1,
            "badusb_run_payload must guard on g_badusb_connected_latched")
        first_dispatch = body.find("badusb_run_line")
        self.assertNotEqual(first_dispatch, -1,
            "badusb_run_payload must dispatch via badusb_run_line")
        head = body[guard:first_dispatch]
        self.assertNotIn("delay(300)", head,
            "guard must precede any 300ms delay")
        self.assertNotIn("delay(500)", head,
            "guard must precede any 500ms delay")

    def test_badusb_run_payload_emits_on_first_connection_event(self):
        self.assertNotIn("badusb_wait_stable", BADUSB,
            "stable-connection gate rejects iOS' one-shot confirm")
        self.assertIn("g_badusb_conn_since_ms", BADUSB)

    def test_badusb_resumes_advertising_after_spam_releases_the_controller(self):
        body = _function_body(BADUSB, "badusb_resume_advertising")
        self.assertIn("g_badusb_adv->start()", body)

    def test_badusb_uses_low_latency_hid_send_window(self):
        delays = [int(x) for x in re.findall(r"delay\((\d+)\);", BADUSB)]
        self.assertTrue(delays, "expected delay() calls in badusb.h")
        self.assertGreaterEqual(min(delays), 20,
            "inter-keystroke delay must be >= 20ms")
        self.assertLessEqual(max(delays), 80,
            "inter-keystroke delay must be <= 80ms")

    def test_badusb_power_level_set_after_begin(self):
        body = _function_body(BADUSB, "badusb_init")
        self.assertIn("setPower", body)
        self.assertIn("ESP_PWR_LVL_P9", body)
        self.assertGreater(body.index("setPower"),
                          body.index("g_ble_kbd.begin"),
                          "setPower must be re-applied after begin()")

    # ────── BLE Spam ──────
    def test_ble_spam_registers_a_gap_callback(self):
        body = _function_body(SPAM, "ble_spam_init")
        self.assertIn("esp_ble_gap_register_callback", body)

    def test_ble_spam_adv_data_raw_wait_callback(self):
        cb = _function_body(SPAM, "ble_spam_gap_cb")
        self.assertIn("ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT", cb)

    def test_ble_spam_apple_payload_is_compatible_with_continuity_protocol(self):
        body = _function_body(SPAM, "ble_spam_build")
        for token in ("0xFF", "0x4C", "0x0F", "0xC1", "0x05"):
            self.assertIn(token, body, f"missing Apple payload token {token}")

    def test_ble_spam_apple_juice_carries_proximity_pairing_subtype(self):
        body = _function_body(SPAM, "ble_spam_build")
        self.assertRegex(body, r"0x1[eE].*0x[fF][fF].*0x4[cC].*0x00.*0x07.*0x19.*0x07",
            "AppleJuice payload must keep its 0x07 proximity-pairing subtype")

    def test_ble_spam_random_mac_marker_is_static_random(self):
        body = _function_body(SPAM, "ble_spam_random_mac")
        self.assertIn("mac[0] |= 0xC0", body)

    def test_ble_spam_resets_random_address_on_stop(self):
        body = _function_body(SPAM, "ble_spam_stop")
        self.assertIn("esp_ble_gap_set_rand_addr(zero_addr)", body)

    def test_ble_spam_emits_in_bursts_not_one_per_loop(self):
        m = re.search(r"BLE_SPAM_BURST_MS\s+(\d+)", SPAM)
        self.assertIsNotNone(m, "BLE_SPAM_BURST_MS must be defined")
        burst = int(m.group(1))
        self.assertGreaterEqual(burst, 50,
            f"BLE_SPAM_BURST_MS={burst}ms; must be >= 50ms")
        self.assertLessEqual(burst, 200,
            f"BLE_SPAM_BURST_MS={burst}ms; must be <= 200ms")

    def test_ble_spam_waits_for_config_complete_before_restart(self):
        self.assertIn("g_ble_spam_adv_config_pending", SPAM,
            "burst loop must track the controller's config-pending state")
        loop = _function_body(SPAM, "ble_spam_loop")
        self.assertIn("g_ble_spam_adv_config_pending", loop,
            "loop must early-return while config is pending")


if __name__ == "__main__":
    unittest.main()
