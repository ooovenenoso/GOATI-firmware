/*
 * ─────────────────────────────────────────────────────────────
 *                          Config
 * ─────────────────────────────────────────────────────────────
 */
#pragma once

struct ChannelCfg {
  bool    enabled;
  char    token[CFG_S];
  char    allow_from[ALLOW_LIST_MAX][ALLOW_ID_LEN];
  uint8_t allow_count;
};

struct Config {
  char wifi_ssid[CFG_S];
  char wifi_pass[CFG_S];
  char llm_provider[32];
  char llm_api_key[LLM_KEY];
  char llm_api_base[CFG_S];
  char llm_model[64];
  uint16_t max_tokens;
  float    temperature;
  uint8_t  max_tool_iters;
  uint32_t heartbeat_ms;
  ChannelCfg telegram;
  ChannelCfg discord;
  char discord_channel_id[ALLOW_ID_LEN];
  char       board_md[4096];
  bool       board_md_loaded;
  // Persisted conversation history (last SESSION_HIST_N turns).
  // Each row layout: chars[0..11] = role ("user" / "assistant" / "system"),
  //                  chars[12..]  = UTF-8 text content (null-terminated).
  int8_t     session_head;                                                          // next write index
  uint8_t    session_count;                                                         // 0..SESSION_HIST_N
  char       session_history[SESSION_HIST_N][SESSION_HIST_S];
};

static Config g_cfg;
