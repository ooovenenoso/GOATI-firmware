import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "firmware" / "include"


def source(name: str) -> str:
    return (INCLUDE / name).read_text(encoding="utf-8")


class FirmwareContracts(unittest.TestCase):
    def test_minimax_highspeed_is_default_on_both_persistence_backends(self):
        persist = source("persist.h")
        self.assertGreaterEqual(persist.count('"MiniMax-M2.7-highspeed"'), 3)
        self.assertIn('"https://api.minimax.io/v1"', persist)

    def test_every_request_contains_measured_device_context(self):
        llm = source("llm.h")
        for field in (
            "free_heap_bytes",
            "wifi=",
            "ip=",
            "rssi_dbm",
            "oled_page",
            "board_config",
            "ble_spam",
            "ble_hid",
        ):
            self.assertIn(field, llm)

    def test_oled_content_stays_between_header_and_footer(self):
        display = source("display.h")
        self.assertIn("const int16_t topY = 19", display)
        self.assertIn("bodyW = 42, bodyH = 30", display)
        self.assertIn('disp_kv(46, "elapsed", v);', display)
        self.assertNotIn('disp_kv(50, "elapsed", v);', display)
        self.assertIn("14 + row * 10", display)

    def test_telegram_buffers_cover_the_complete_reply_budget(self):
        constants = source("constants.h")
        telegram = source("telegram.h")
        self.assertIn("TG_MSG_CHUNK      = 1800", constants)
        self.assertIn("static char clean_reply[RESP_S]", telegram)
        self.assertIn("static char final_reply[RESP_S]", telegram)
        self.assertIn("static char tg_body[4608]", telegram)
        self.assertIn("Handle one\n        // update per poll", telegram)

    def test_warning_suppression_is_not_global(self):
        ini = (ROOT / "firmware" / "platformio.ini").read_text(encoding="utf-8")
        self.assertNotRegex(ini, re.compile(r"^\s*-w\s*$", re.MULTILINE))
        self.assertIn("-Wall", ini)
        self.assertIn("-Wextra", ini)


if __name__ == "__main__":
    unittest.main()
