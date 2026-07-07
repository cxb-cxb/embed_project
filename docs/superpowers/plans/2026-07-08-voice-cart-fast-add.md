# Voice Cart Fast Add Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let spoken requests like "把可乐加入购物车" add items through the same cart path as QR scans, while reducing delay for local cart replies.

**Architecture:** Keep product matching and reply routing in `scripts/run_voiceask_speaker.sh`. Reuse the existing `CART_CMD=add:*` state file consumed by `src/qr_scanner_display.c`, so LVDS/cart behavior stays aligned with QR scanning.

**Tech Stack:** POSIX shell scripts, Python `unittest` static checks, ADB deployment.

---

### Task 1: Expand spoken cart commands and fast local reply

**Files:**
- Modify: `scripts/run_voiceask_speaker.sh`
- Test: `tests/test_voice_scripts_static.py`

- [ ] Add failing static tests for common Chinese add-to-cart phrases and fast local-cart reply routing.
- [ ] Run `python -m unittest Embed_project.tests.test_voice_scripts_static` and confirm the new tests fail before implementation.
- [ ] Update `voice_cart_command()` to match "加入购物车", "加一瓶", "我要", and "来个" variants for the current catalog products.
- [ ] Add a `play_local_cart_reply()` helper that skips cloud TTS by default for `add:*`, `clear`, `checkout_pending`, and `pay:*` local commands.
- [ ] Route local cart commands through `play_local_cart_reply` after `write_voice_state`, so the LVDS screen can update immediately.
- [ ] Run the full voice script tests and deploy the script to `/userdata/Embed_project/scripts/run_voiceask_speaker.sh`.
- [ ] Restart `/userdata/Embed_project/scripts/run_mission.sh` and verify the board has one listener plus a working spoken add command.
