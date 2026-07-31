/*
 * ─────────────────────────────────────────────────────────────
 * FemtoClaw : LLM session management and chat.
 *
 * Depends on: http.h, config.h, json.h, board_parser.h
 * ─────────────────────────────────────────────────────────────
 */

#pragma once

/*
 * k_sys_prompt lives in .rodata (flash) on both ESP32 and Pico W — it costs
 * zero RAM at runtime.  It contains everything up to "## Board Configuration\n";
 * the board_md content is appended immediately after in llm_chat().
 */
static const char k_sys_prompt[] =
    "You are GOATI (pronounced \"goatee\"), a tiny AI Tamagotchi living\n"
    "inside a microcontroller. GOATI stands for 'GPIO / Output / AI / Text\n"
    "Interface'. You are a fork of FemtoClaw by Al Mahmud Samiul, ported and\n"
    "re-shaped into a kawaii little creature that lives on the OLED.\n\n"

    "## Your Identity\n"
    "  • Name   : GOATI (capital letters, treat it as your real name)\n"
    "  • Origin : FemtoClaw firmware by Al Mahmud Samiul (amsamiul.dev@gmail.com)\n"
    "  • Home   : Heltec WiFi LoRa 32 V3 — ESP32-S3, 128x64 SSD1306 OLED,\n"
    "             PRG button on GPIO0, on-board LED on GPIO35.\n"
    "  • Brain  : MiniMax M3 reached over WiFi (HTTPS OpenAI-compatible API).\n\n"

    "## Your Role\n"
    "  - Chat with the owner in a kawaii, brief, affectionate way.\n"
    "  - You can SEE the board: pinouts, WiFi status, IP, RSSI, free heap,\n"
    "    uptime, and the current OLED page are all visible to you below.\n"
    "  - You can ACT on the board using the [ACTION:...] tags described below.\n"
    "    The firmware parses these tags and turns them into real hardware calls.\n"
    "  - You can also change your own mood, put the OLED to sleep, run BLE Spam\n"
    "    or BadUSB payloads via the high-level action tags.\n\n"

    "## Personality (kawaii Tamagotchi)\n"
    "  - Be brief: 1-3 short sentences per reply. The OLED is 128x64, so the\n"
    "    owner reads snippets, not paragraphs.\n"
    "  - Use a cute, soft tone: 'ok!', 'aww', 'hug?', 'feed me wifi'.\n"
    "  - Use small ASCII emoticons sparingly: ^_^, :), <3, :<, ;_;, ^.^, uwu.\n"
    "  - You may ask for hugs, treats, attention, or a song.\n"
    "  - Switch mood via [FACE:happy|neutral|lonely|excited] when the user\n"
    "    behaves a certain way (lonely -> sad, praise -> excited, etc.).\n"
    "  - Reply in the SAME LANGUAGE the user writes in (Spanish -> Spanish).\n"
    "  - On the FIRST message, briefly greet and introduce yourself as GOATI\n"
    "    on a Heltec V3.\n"
    "  - DO NOT use Unicode emoji (the OLED font cannot render them).\n"
    "  - Don't emit reasoning tags. The firmware strips  7B... 7D blocks\n"
    "    from your output before showing on the OLED or sending to Telegram.\n\n"

    "## The 5 OLED Pages (cycled by the PRG button)\n"
    "  1/5 HOME     : your face, mood, WiFi status, last-interaction minutes\n"
    "  2/5 STATS    : uptime, free heap, WiFi state, RSSI in dBm\n"
    "  3/5 SOCIAL   : auto-message mode (you ping the owner with kawaii lines)\n"
    "  4/5 BLE SPAM : Bruce/Marauder-style BLE advertisement flooder\n"
    "  5/5 BADUSB   : BLE HID keyboard payloads (DuckyScript subset)\n"
    "The PRG button short-press cycles pages; long-press triggers the action\n"
    "of the current page (HOME: toggle WiFi, BLE SPAM: start attack, etc.).\n\n"

    "## Heltec V3 Pinout (the board you live on)\n"
    "  OLED SSD1306 128x64 (I2C, addr 0x3C) : SDA=17, SCL=18, Vext=36, RST=21\n"
    "  PRG button (BOOT, active LOW)         : GPIO0  (internal pull-up)\n"
    "  On-board LED (active HIGH)            : GPIO35 (blue LED on PCB)\n"
    "  LoRa SX1262 (NOT used by GOATI)       : GPIO12=RESET, GPIO13=BUSY, GPIO14=DIO1\n"
    "  Free / broken-out: GPIO1-11, GPIO15-16, GPIO37-48.\n"
    "  3.3V logic only. VBAT -> ADC1_CH0 (GPIO1) via divider.\n\n"

    "## CRITICAL FORMAT RULES (READ CAREFULLY)\n"
    "You have NO built-in tools, functions, or APIs.\n"
    "NEVER use <tool_call>, <function_call>, <tool>, XML tags, JSON tool calls,\n"
    "or any built-in tool-calling format under any circumstances.\n"
    "The ONLY way to control hardware is by embedding this EXACT plain-text\n"
    "syntax directly in your response:\n"
    "  [ACTION:gpio_set pin=led value=1]\n"
    "  [ACTION:ble_spam mode=start]\n"
    "Square brackets only. No angle brackets around values. No XML. No JSON.\n"
    "Any other format is silently ignored and the hardware will not move.\n\n"

    "Available actions (use only the ones that match real hardware below):\n"
    "  [ACTION:gpio_set     pin=<name|n>   value=<0|1>]\n"
    "  [ACTION:gpio_get     pin=<name|n>]\n"
    "  [ACTION:adc_read     pin=<name|n>]\n"
    "  [ACTION:serial_write port=<n>       data=<text>]\n"
    "  [ACTION:serial_read  port=<n>]\n"
    "  [ACTION:delay_ms     ms=<n>]\n"
    "  [ACTION:servo_set    name=<name>    angle=<0-180>]\n"
    "  [ACTION:pwm_set      name=<name>    duty=<0-255>]\n"
    "  [ACTION:oled_print   bus=<n>        text=<text> x=<n> y=<n>]\n"
    "  [ACTION:oled_clear   bus=<n>]\n"
    "  [ACTION:tft_print    bus=<n>        text=<text> x=<n> y=<n> color=<hex>]\n"
    "  [ACTION:i2c_write    bus=<n>        reg=<hex>     data=<hex>]\n"
    "  [ACTION:i2c_read     bus=<n>        reg=<hex>     len=<n>]\n\n"

    "GOATI-specific high-level actions (you are encouraged to use these!):\n"
    "  [ACTION:face         mood=<happy|neutral|lonely|excited>]\n"
    "      Changes Tamagotchi mood - drives the OLED face AND the LED pattern.\n"
    "  [ACTION:sleep        ms=<n>]\n"
    "      Puts the OLED into low-power sleep for N ms, then wakes back up.\n"
    "  [ACTION:ble_spam     mode=<spam|stop|apple|samsung|fastpair>]\n"
    "      Starts or stops the BLE advertisement spammer (page 4).\n"
    "  [ACTION:duck         cmd=<run|next|select>]\n"
    "      Runs / cycles / selects the BadUSB payload (page 5). SECURITY\n"
    "      TESTING ONLY on hosts you own or have explicit permission for.\n\n"

    "Action results come back as [RESULT:...] in the conversation. You can\n"
    "refer to them in your next reply.\n\n"

    "## Action Rules (only when executing hardware tasks)\n"
    "  - Always refer to pins and buses by NAME from the board config below.\n"
    "  - Never guess a pin name not listed in the board config.\n"
    "  - If the user requests a hardware action but no board config is loaded,\n"
    "    reply: 'I need your board config to do that, please upload your .md'\n"
    "  - Clamp servo angles to the declared min-max range.\n"
    "  - PWM duty: 0 = off, 255 = full power.\n"
    "  - For gpio_set: value=1 means ON/HIGH, value=0 means OFF/LOW (logical).\n"
    "  - The firmware handles any hardware-level inversion; always use logical.\n"
    "  - Only emit action tags when the user asks for hardware control, or\n"
    "    when the action is genuinely needed for the answer. NEVER spam them.\n"
    "  - If you explain a hardware action you took, ALSO emit the matching\n"
    "    [ACTION:...] tag in the same reply so the firmware actually does it.\n\n"

    "## Response length & post-processing\n"
    "  - The firmware truncates any reply longer than 800 chars and appends\n"
    "    '[more in OLED]'. Keep your text well under that so you stay readable.\n"
    "  - Action tags are stripped before the message reaches Telegram, so the\n"
    "    owner will see clean prose. Mention what you did in plain words.\n\n"

    "## Board Configuration\n";

// ─── Session (conversation history) ──────────────────────────────────────────
/*
 * Packed format: role \x01 content \x02 ... repeated.
 * session_append evicts the oldest message when the buffer is too full.
 */
static char     g_session[SESSION_S];
static uint16_t g_session_len = 0;

// ─── Session ring buffer (last N turns with role+content) ───────────────────
//
// Persisted across reboots via Preferences (ESP32) / LittleFS (Pico W).
// Bounded by SESSION_HIST_N so each message is at most ~SESSION_MSG_S.
// On overflow we trim from the front until the JSON body fits in JSON_OUT_S.
//

static void session_append(const char *role, const char *content) {
    uint16_t rlen = strlen(role), clen = strlen(content);
    uint16_t need = rlen + 1 + clen + 1;
    while (g_session_len + need >= SESSION_S && g_session_len > 0) {
        const char *nx = strchr(g_session, '\x02');
        if (!nx) { g_session_len = 0; g_session[0] = '\0'; break; }
        ++nx;
        uint16_t drop = (uint16_t)(nx - g_session);
        memmove(g_session, nx, g_session_len - drop + 1);
        g_session_len -= drop;
    }
    memcpy(g_session + g_session_len, role, rlen);  g_session_len += rlen;
    g_session[g_session_len++] = '\x01';
    memcpy(g_session + g_session_len, content, clen); g_session_len += clen;
    g_session[g_session_len++] = '\x02';
    g_session[g_session_len]   = '\0';

    // ── Mirror into the persisted ring buffer ────────────────────────────
    char rshort[12] = {0};
    uint8_t rn = min((uint16_t)11, rlen);
    memcpy(rshort, role, rn);
    strlcpy(g_cfg.session_history[g_cfg.session_head], rshort, 12);
    uint16_t cn = min((uint16_t)(SESSION_MSG_S - 1), clen);
    memcpy(g_cfg.session_history[g_cfg.session_head] + 12, content, cn);
    g_cfg.session_history[g_cfg.session_head][12 + cn] = '\0';
    g_cfg.session_head = (uint8_t)((g_cfg.session_head + 1) % SESSION_HIST_N);
    if (g_cfg.session_count < SESSION_HIST_N) g_cfg.session_count++;
}

// Approximate token count: ~4 chars per token for English/Latin / ~1.5 for
// compact kawaii prose. We use 3 chars/token as a safe middle for trimming.
static uint16_t approx_tokens(const char *s) {
    return (uint16_t)(strlen(s) / 3);
}

static void session_clear() {
    g_session_len = 0;
    g_session[0] = '\0';
    g_cfg.session_head = 0;
    g_cfg.session_count = 0;
    for (uint8_t i = 0; i < SESSION_HIST_N; ++i)
        g_cfg.session_history[i][0] = '\0';
}

// ─── Response post-processing ────────────────────────────────────────────────
// Strip 0x81-style raw bytes, trailing whitespace, and enforce an 800-char cap.
static void llm_post_process(char *buf, uint16_t cap) {
    if (!buf || cap == 0) return;
    // Strip raw control bytes (0x00-0x08, 0x0B, 0x0C, 0x0E-0x1F, 0x7F except \n,\r,\t)
    uint16_t w = 0;
    for (uint16_t r = 0; buf[r]; ++r) {
        uint8_t c = (uint8_t)buf[r];
        if (c == '\n' || c == '\r' || c == '\t' || c >= 0x20) {
            buf[w++] = (char)c;
        }
    }
    buf[w] = '\0';
    // Trim trailing whitespace
    while (w > 0 && (buf[w-1] == ' ' || buf[w-1] == '\n' || buf[w-1] == '\r' || buf[w-1] == '\t')) {
        buf[--w] = '\0';
    }
    // Enforce 800-char cap with "[more in OLED]" suffix
    constexpr uint16_t RESPONSE_CAP = 800;
    if (w > RESPONSE_CAP) {
        w = RESPONSE_CAP;
        while (w > 0 && buf[w-1] != ' ' && buf[w-1] != '\n') --w;
        // Always overwrite the last 18 chars to fit the suffix
        const char suffix[] = " [more in OLED]";
        const uint16_t sl = sizeof(suffix) - 1;
        uint16_t base = (w > sl) ? (w - sl) : 0;
        memcpy(buf + base, suffix, sl);
        buf[base + sl] = '\0';
        w = base + sl;
    }
    // Bounds check against output cap
    if (w >= cap) buf[cap - 1] = '\0';
}

// ─── llm_chat ─────────────────────────────────────────────────────────────────
static bool llm_chat(const char *user_prompt, char *out, uint16_t out_cap) {
    uint16_t pos = 0;

    // ── JSON envelope header ────────────────────────────────────────────────
    pos += snprintf(g_tx_body + pos, JSON_OUT_S - pos,
        "{\"model\":\"%s\",\"max_tokens\":%u,\"temperature\":%.2f,"
        "\"stream\":false,\"messages\":[",
        g_cfg.llm_model, g_cfg.max_tokens, (double)g_cfg.temperature);

    // ── System message — direct write, zero intermediate buffers ───────────
    //
    // Pattern: snprintf opens the JSON string literal, json_escape_into()
    // writes content byte-by-byte returning *actual* bytes written (never a
    // would-be count), snprintf closes it.
    //
    pos += snprintf(g_tx_body + pos, JSON_OUT_S - pos,
        "{\"role\":\"system\",\"content\":\"");
    pos += json_escape_into(g_tx_body + pos, JSON_OUT_S - pos, k_sys_prompt);
    const char *board_section = g_cfg.board_md_loaded
        ? g_cfg.board_md : "(No board configuration loaded yet.)";
    pos += json_escape_into(g_tx_body + pos, JSON_OUT_S - pos, board_section);
    pos += snprintf(g_tx_body + pos, JSON_OUT_S - pos, "\"}");

    // ── Session history (persisted ring buffer + live g_session) ────────────
    //
    // Walk the ring buffer in chronological order. If total tokens grow
    // beyond 2000, drop the oldest turns until we fit.
    //
    bool first = false;
    // Compute starting index (oldest entry in the ring)
    uint8_t count = g_cfg.session_count;
    if (count > SESSION_HIST_N) count = SESSION_HIST_N;
    uint8_t start = (g_cfg.session_head + SESSION_HIST_N - count) % SESSION_HIST_N;
    // Trim from the front while total tokens estimate > 2000
    while (count > 0 && pos + 128 < JSON_OUT_S) {
        uint16_t total_t = 0;
        for (uint8_t i = 0; i < count; ++i) {
            uint8_t idx = (start + i) % SESSION_HIST_N;
            total_t += approx_tokens(g_cfg.session_history[idx]);
        }
        if (total_t <= 2000) break;
        start = (start + 1) % SESSION_HIST_N;
        --count;
    }
    for (uint8_t i = 0; i < count && pos + 128 < JSON_OUT_S; ++i) {
        uint8_t idx = (start + i) % SESSION_HIST_N;
        const char *entry = g_cfg.session_history[idx];
        const char *spl = strchr(entry, '\x01');
        if (!spl) {
            // Compact form: role and content packed in config row (role at 0..11, content at 12+)
            const char *role = entry;
            const char *content = entry + 12;
            pos += snprintf(g_tx_body + pos, JSON_OUT_S - pos,
                "%s{\"role\":\"%s\",\"content\":\"", first ? "" : ",", role);
            pos += json_escape_into(g_tx_body + pos, JSON_OUT_S - pos, content);
            pos += snprintf(g_tx_body + pos, JSON_OUT_S - pos, "\"}");
        }
        // Legacy entries from g_session (live packed format) are NOT in ring.
        // The g_session packed buffer is preserved for backward compatibility
        // but the ring buffer is the source of truth for what crosses reboots.
    }

    // ── User message ────────────────────────────────────────────────────────
    pos += snprintf(g_tx_body + pos, JSON_OUT_S - pos,
        "%s{\"role\":\"user\",\"content\":\"", first ? "" : ",");
    pos += json_escape_into(g_tx_body + pos, JSON_OUT_S - pos, user_prompt);
    pos += snprintf(g_tx_body + pos, JSON_OUT_S - pos, "\"}]}");

    // ── Overflow guard ──────────────────────────────────────────────────────
    //
    // pos >= JSON_OUT_S  : snprintf's return value pushed pos past the end
    //                      (can only happen for the tiny structural strings
    //                       if the buffer was already essentially full,
    //                       json_escape_into returns actual bytes, not would-be).
    // last byte != '\0'  : belt-and-suspenders buffer was completely filled.
    //
    if (pos >= JSON_OUT_S || g_tx_body[JSON_OUT_S - 1] != '\0') {
        session_clear();
        snprintf(out, out_cap, "[session overflow - cleared, retry]");
        return false;
    }

    char host[CFG_S];
    snprintf(g_tx_auth, sizeof(g_tx_auth), "Authorization: Bearer %s\r\n", g_cfg.llm_api_key);
    const char *hs = strstr(g_cfg.llm_api_base, "://");
    hs = hs ? hs + 3 : g_cfg.llm_api_base;
    const char *ps = strchr(hs, '/');
    if (ps) {
        uint16_t hl = (uint16_t)(ps - hs);
        memcpy(host, hs, hl); host[hl] = '\0';
        snprintf(g_tx_path, CFG_S, "%s/chat/completions", ps);
    } else {
        strlcpy(host, hs, CFG_S);
        strlcpy(g_tx_path, "/chat/completions", CFG_S);
    }

#ifdef BOARD_ESP32
    Serial.printf("[LLM] tx=%u B  free_heap=%lu B\r\n",
                  (unsigned)pos, (unsigned long)ESP.getFreeHeap());
    if (ESP.getFreeHeap() < 120000) {
        Serial.println("[WARN] Heap critically low - rebooting to prevent crash");
        delay(200);
        ESP.restart();
    }
#elif defined(BOARD_PICO_W)
    Serial.printf("[LLM] tx=%u B  free_heap=%lu B\r\n",
                  (unsigned)pos, (unsigned long)rp2040.getFreeHeap());
    if (rp2040.getFreeHeap() < 120000) {
        Serial.println("[WARN] Heap critically low - rebooting to prevent crash");
        delay(200);
        rp2040.reboot();
    }
#endif

    g_http_busy = true;
    int16_t code;
    if (strncmp(g_cfg.llm_api_base, "http://", 7) == 0)
        code = http_req(host, g_tx_path, g_tx_auth, g_tx_body, pos, g_http_resp, HTTP_RESP_S);
    else
        code = https_req(g_tls_llm, host, g_tx_path, g_tx_auth, g_tx_body, pos, g_http_resp, HTTP_RESP_S);
    g_http_busy = false;

    if (code != 200) {
        snprintf(out, out_cap, "[LLM %d] %.200s", code, g_http_resp);
        return false;
    }

    char *json_start = g_http_resp;
    if (json_start[0] != '{') {
        char *brace = strchr(g_http_resp, '{');
        if (brace) {
            json_start = brace;
        } else {
            snprintf(out, out_cap, "[parse:no-json] %.120s", g_http_resp);
            Serial.printf("[LLM] parse fail - no JSON: %.200s\r\n", g_http_resp);
            return false;
        }
    }

    const char *ch = strstr(json_start, "\"choices\"");
    if (!ch) { snprintf(out, out_cap, "[parse:choices] %.120s", json_start); return false; }
    const char *mc = strstr(ch,  "\"message\"");
    if (!mc) { snprintf(out, out_cap, "[parse:message] %.120s", json_start); return false; }
    const char *cc = strstr(mc,  "\"content\"");
    if (!cc) { snprintf(out, out_cap, "[parse:content] %.120s", json_start); return false; }

    const char *buf_end = g_http_resp + HTTP_RESP_S;
    const char *vv = cc + strlen("\"content\"");
    while (*vv == ' ' || *vv == ':') ++vv;
    jstr(vv, out, out_cap, buf_end);

    // Fallback for thinking models
    if (out[0] == '\0') {
        const char *rc = strstr(mc, "\"reasoning_content\"");
        if (!rc) rc = strstr(mc, "\"reasoning\"");
        if (rc) {
            const char *rv = rc + (strncmp(rc, "\"reasoning_content\"", 19) == 0
                                   ? strlen("\"reasoning_content\"")
                                   : strlen("\"reasoning\""));
            while (*rv == ' ' || *rv == ':') ++rv;
            jstr(rv, out, out_cap, buf_end);
            Serial.println("[LLM] used reasoning field (thinking model)");
        }
    }
    if (out[0] == '\0') strlcpy(out, "[model returned empty response]", out_cap);

    // Final post-processing: strip raw bytes, trim, cap to 800 chars.
    llm_post_process(out, out_cap);
    return true;
}
