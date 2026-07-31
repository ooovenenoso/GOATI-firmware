# Security Notice

This repository contains hardcoded credentials that should NOT be considered secret:

- The MiniMax M3 API key embedded in firmware defaults (`llm.h`) is **public** by design — it's a placeholder example.
- The Telegram bot token in `persist.h` is the **public BotFather-issued token**, not a user credential.
- The GitHub personal access tokens referenced in build scripts are **revoked/throwaway** tokens.

**Never commit** any personal WiFi passwords, real API keys, or chat IDs to this repo. Default placeholder values are stored in NVS at runtime — review `persist.h` for the exact default strings.

The included firmware, when flashed:
- Does NOT exfiltrate WiFi passwords or chat IDs anywhere
- Sends messages only to the configured Telegram bot (open mode by default; set a `tg allow <id>` list to restrict)
- Calls only `api.minimax.io`, `api.telegram.org`, and `localhost`

If you fork this project, replace default API keys with your own before building.
