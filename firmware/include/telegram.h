/*
 * ─────────────────────────────────────────────────────────────
 * FemtoClaw : Telegram long-polling channel.
 *
 * Depends on: http.h, agent.h, config.h, json.h, persist.h
 * ─────────────────────────────────────────────────────────────
 */

#pragma once

// Forward decls from display.h (avoids circular include)
static void disp_set_state(int s);
static void disp_force_redraw();
static void disp_show();
static void disp_draw_state(uint32_t now);

static uint32_t g_tg_last_ms = 0;

// ─── tg_send ──────────────────────────────────────────────────────────────────
// Send text to Telegram chat, splitting into TG_MSG_CHUNK-byte chunks.
static int16_t tg_send(const char *chat_id, const char *text) {
    static char tg_esc[4096];
    static char tg_path[CFG_S];
    static char tg_body[4096];

    uint16_t tlen = strlen(text);
    int16_t last_code = 0;
    uint16_t sent = 0;
    while (sent < tlen) {
        uint16_t chunk = min((uint16_t)(tlen - sent), TG_MSG_CHUNK);
        json_escape(text + sent, chunk, tg_esc, JSON_OUT_S);
        sent += chunk;
        snprintf(tg_path, CFG_S, "/bot%s/sendMessage", g_cfg.telegram.token);
        snprintf(tg_body, JSON_OUT_S,
                 "{\"chat_id\":\"%s\",\"text\":\"%s\"}", chat_id, tg_esc);

        g_suppress_tls_logs = true;
        g_http_busy = true;
        last_code = https_req(g_tls_tg, "api.telegram.org", tg_path, nullptr,
                              tg_body, strlen(tg_body), g_http_resp, HTTP_RESP_S);
        g_http_busy = false;
        g_suppress_tls_logs = false;

        Serial.printf("[Telegram] sendMessage code=%d\r\n", last_code);
    }
    return last_code;
}

// ─── tg_poll ──────────────────────────────────────────────────────────────────
static void tg_poll() {
    if (!g_cfg.telegram.enabled || !g_cfg.telegram.token[0]) return;
    if ((millis() - g_tg_last_ms) < TG_POLL_MS) return;
    if (g_http_busy) return;
    g_tg_last_ms = millis();

    snprintf(g_tx_path, CFG_S, "/bot%s/getUpdates?offset=%lld&timeout=1&limit=5",
             g_cfg.telegram.token, (long long)g_tg_offset);

    g_suppress_tls_logs = true;
    g_http_busy = true;
    int16_t code = https_req(g_tls_tg, "api.telegram.org", g_tx_path, nullptr,
                              nullptr, 0, g_http_resp, HTTP_RESP_S);
    g_http_busy = false;
    g_suppress_tls_logs = false;

    if (code != 200) {
      static uint32_t s_last_tg_err = 0;
      if (millis() - s_last_tg_err > 60000) {
        s_last_tg_err = millis();
        Serial.printf("[Telegram] poll failed code=%d (len=%u)\r\n", code, (unsigned)strlen(g_http_resp));
      }
      return;
    }

    if (code != 200) {
        Serial.printf("[Telegram] poll failed code=%d resp=%.150s\r\n", code, g_http_resp);
        return;
    }

    const char *p = g_http_resp;
    while ((p = strstr(p, "\"update_id\"")) != nullptr) {
        int64_t uid = jint(p + strlen("\"update_id\"") + 1);
        if (uid >= g_tg_offset) {
            g_tg_offset = uid + 1;
#if PERSIST_IMPL == 1
            prefs.begin("femtoclaw", false);
            prefs.putLong64("tg_offset", g_tg_offset);
            prefs.end();
#else
            cfg_save();
#endif
        }

        const char *msg_start = strstr(p, "\"message\"");
        if (!msg_start) { ++p; continue; }

        char from_id[ALLOW_ID_LEN] = {0};
        char chat_id[ALLOW_ID_LEN] = {0};
        char text[PROMPT_S]        = {0};

        const char *from_sec = strstr(msg_start, "\"from\"");
        if (from_sec) {
            const char *id_v = jfind(from_sec, "id");
            if (id_v) id_from_int64(jint(id_v), from_id, sizeof(from_id));
        }
        const char *chat_sec = strstr(msg_start, "\"chat\"");
        if (chat_sec) {
            const char *id_v = jfind(chat_sec, "id");
            if (id_v) id_from_int64(jint(id_v), chat_id, sizeof(chat_id));
        }
        const char *tv = jfind(msg_start, "text");
        if (tv) jstr(tv, text, PROMPT_S);

        Serial.printf("[Telegram] update_id=%lld from=%s chat=%s text='%s'\r\n",
                      (long long)uid, from_id, chat_id, text);

        if (!text[0]) { ++p; continue; }

        // Open mode: if no allow list, accept all. Otherwise check.
        if (g_cfg.telegram.allow_count > 0 && !is_allowed(g_cfg.telegram, from_id)) {
            Serial.printf("[Telegram] BLOCKED — from_id=%s not in allow list\r\n", from_id);
            Serial.printf("[Telegram]   add with: tg allow %s\r\n", from_id);
            ++p; continue;
        }
        // First-time auto-add: if allow list empty, log chat_id
        if (g_cfg.telegram.allow_count == 0) {
            Serial.printf("[Telegram] OPEN MODE (no allow list) — your chat_id=%s\r\n", from_id);
        }

        const char *reply;
        {
          // Show Thinking face on OLED BEFORE blocking LLM call.
          // CRITICAL: must actually draw to buffer, not just set state, because
          // agent_run() blocks for seconds and the main loop doesn't run.
          disp_set_state(DISP_LLM_THINKING);
          disp_draw_state(millis());   // actually render Thinking to buffer
          disp_show();                  // commit to OLED NOW
          uint32_t think_start = millis();
          reply = agent_run(text);
          g_last_interaction_ms = millis();
          g_mood = MOOD_HAPPY;
          // Enforce minimum 1.5s thinking display so user actually sees it
          uint32_t think_elapsed = millis() - think_start;
          if (think_elapsed < 1500) {
            delay(1500 - think_elapsed);
          }
        }
        // Strip <think>...</think> blocks and [ACTION:...] tags before sending
        char clean_reply[512];
        const char *src = reply;
        char *dst = clean_reply;
        // First strip [ACTION:...]
        while (*src && (dst - clean_reply) < (int)sizeof(clean_reply) - 1) {
            const char *tag = strstr(src, "[ACTION:");
            if (!tag) { while (*src && (dst - clean_reply) < (int)sizeof(clean_reply) - 1) *dst++ = *src++; break; }
            while (src < tag && (dst - clean_reply) < (int)sizeof(clean_reply) - 1) *dst++ = *src++;
            const char *end = strchr(tag, ']');
            src = end ? end + 1 : tag + 1;
        }
        *dst = '\0';
        // Then strip <think>...</think>
        char final_reply[512];
        {
            const char *p = clean_reply;
            char *q = final_reply;
            while (*p && (q - final_reply) < (int)sizeof(final_reply) - 1) {
                if (strncmp(p, "<think>", 7) == 0) {
                    const char *end = strstr(p + 7, "</think>");
                    if (end) {
                        p = end + 8;
                        while (*p == ' ' || *p == '\n') p++;
                        continue;
                    }
                }
                *q++ = *p++;
            }
            *q = '\0';
        }
        Serial.printf("[Telegram] replying (%u chars) → chat %s\r\n",
                      (unsigned)strlen(final_reply), chat_id);

        delay(TLS_SETTLE_MS);
        int16_t sc = tg_send(chat_id, final_reply);
        if (sc != 200) {
          Serial.printf("[Telegram] send FAILED code=%d resp=%.100s\r\n", sc, g_http_resp);
        } else {
          Serial.printf("[Telegram] sent OK to chat %s (%u chars)\r\n", chat_id, (unsigned)strlen(final_reply));
        }

        // Show response on OLED briefly before going back to HOME
        disp_show_response(final_reply);
        disp_set_state(DISP_LLM_RESPONDING);
        delay(500);

        ++p;
    }
}