import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class VoiceAutoListenScriptTest(unittest.TestCase):
    def test_run_mission_starts_screen_and_voice(self):
        script = ROOT / "scripts" / "run_mission.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("start_retail_lvds_ui.sh", text)
        self.assertIn("start_voice_auto_listen.sh", text)
        self.assertIn("configure_board_voice_speaker.sh", text)
        self.assertIn("/tmp/qsm_auto_voice.log", text)
        self.assertIn("/tmp/qsm_auto_voice.pid", text)
        self.assertIn("run_mission is ready", text)

    def test_run_mission_stops_previous_shell_script_listeners_by_command_line(self):
        script = ROOT / "scripts" / "run_mission.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("pkill -f", text)
        self.assertIn("scripts/start_voice_auto_listen.sh", text)
        self.assertIn("scripts/run_voiceask_speaker.sh", text)
        self.assertIn("tinycap", text)
        self.assertNotIn("killall start_voice_auto_listen.sh", text)

    def test_auto_listen_has_single_instance_lock(self):
        script = ROOT / "scripts" / "start_voice_auto_listen.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("LOCK_DIR=", text)
        self.assertIn("mkdir \"$LOCK_DIR\"", text)
        self.assertIn("another auto voice listener is already running", text)
        self.assertIn("rmdir \"$LOCK_DIR\"", text)

    def test_auto_listen_script_runs_voiceask_without_read_prompt(self):
        script = ROOT / "scripts" / "start_voice_auto_listen.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8")
        self.assertIn("run_voiceask_speaker.sh", text)
        self.assertIn("--wake-once", text)
        self.assertIn("while true", text)
        self.assertNotIn("read -r", text)
        self.assertIn("trap", text)
        self.assertIn("welcome_tts.mp3", text)

    def test_auto_listen_has_wake_word_flow_with_chinese_ack(self):
        script = ROOT / "scripts" / "start_voice_auto_listen.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("--wake-once", text)
        self.assertIn("WAKE_ACK_TEXT", text)
        self.assertIn("我在", text)
        self.assertIn("VOICE_WAKE_SECONDS", text)
        self.assertIn("VOICE_COMMAND_SECONDS", text)
        self.assertIn("Listening for wake word", text)

    def test_voiceask_speaker_writes_ui_state_file(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("VOICE_STATE_FILE", text)
        self.assertIn("/tmp/qsm_retail_voice_state", text)
        self.assertIn("write_voice_state", text)
        self.assertIn("voice_cart_command", text)
        self.assertIn("QUESTION=", text)
        self.assertIn("ANSWER=", text)
        self.assertIn("CART_CMD=", text)
        self.assertIn("小智小智", text)
        self.assertIn("contains_wake_word", text)
        self.assertIn("未检测到唤醒词", text)
        self.assertNotIn("CHINESE REPLY PLAYED", text)

    def test_voice_checkout_command_updates_ui_cart_command(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("checkout", text)
        self.assertIn("结账", text)
        self.assertIn("支付", text)
        self.assertIn("买单", text)

    def test_cart_commands_bypass_open_chat(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("local_cart_reply()", text)
        self.assertIn("cart_cmd=\"$(voice_cart_command \"$question\")\"", text)
        cart_pos = text.index("cart_cmd=\"$(voice_cart_command \"$question\")\"")
        open_pos = text.index("answer=\"$(request_open_chat")
        self.assertLess(cart_pos, open_pos)
        self.assertIn("Retail command:", text)

    def test_checkout_asks_for_payment_method_before_showing_code(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("checkout_pending", text)
        self.assertIn("pay:wechat", text)
        self.assertIn("pay:alipay", text)
        self.assertIn("pay:unionpay", text)
        self.assertIn("PAYMENT_WAIT_FILE", text)
        self.assertIn("voice_payment_method_command()", text)
        self.assertIn("请选择微信支付、支付宝支付或银联云闪付", text)
        self.assertIn("银联云闪付暂不可用", text)
        payment_pos = text.index('cart_cmd="$(voice_payment_method_command "$question")"')
        checkout_pos = text.index('cart_cmd="$(voice_cart_command "$question")"')
        self.assertLess(payment_pos, checkout_pos)

    def test_checkout_and_payment_keywords_cover_common_asr_variants(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        for keyword in ["付钱", "买好了", "付一下", "扫微信", "用微信", "扫支付宝", "用支付宝", "云闪付", "用银联"]:
            with self.subTest(keyword=keyword):
                self.assertIn(keyword, text)

    def test_voice_state_keeps_utf8_chinese_for_ui(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertNotIn("CHINESE REPLY PLAYED", text)
        self.assertNotIn("VOICE INPUT", text)
        self.assertIn('question="$(printf', text)
        self.assertIn('answer="$(printf', text)

    def test_voiceask_speaker_can_extract_wake_word_commands(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("is_wake_text()", text)
        self.assertIn("extract_wake_command()", text)
        self.assertIn("run_wake_once()", text)
        self.assertIn("WAKE_ACK_TEXT", text)
        self.assertIn("play_wake_ack", text)
        self.assertIn("Wake word detected", text)
        self.assertIn("Retail command detected without wake word.", text)
        self.assertIn('voice_cart_command "$wake_text"', text)
        self.assertIn('voice_payment_method_command "$wake_text"', text)
        self.assertIn("run_voice_question_once", text)

    def test_voiceask_uses_cached_wake_ack_and_two_minute_session(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("WAKE_ACK_WAV", text)
        self.assertIn("build_wake_ack_audio()", text)
        self.assertIn("play_wake_ack()", text)
        self.assertIn("run_active_session()", text)
        self.assertIn("VOICE_SESSION_SECONDS", text)
        self.assertIn("session_end=$((now + session_seconds))", text)
        self.assertIn("run_voice_question_once", text)
        self.assertNotIn("play_text_tts \"$WAKE_ACK_TEXT\" || true", text)

    def test_wake_recognition_stdout_contains_only_asr_text(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn('echo "Listening for ${label}. Recording ${seconds} seconds..." >&2', text)

    def test_voiceask_prompt_uses_actual_command_seconds(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("display_seconds=", text)
        self.assertIn('echo "Please speak now. Recording ${display_seconds} seconds..."', text)

    def test_speaker_config_uses_slightly_louder_lp_ln_volume(self):
        script = ROOT / "scripts" / "configure_board_voice_speaker.sh"
        self.assertTrue(script.exists())
        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("export VOICE_PLAYBACK_PATH=3", text)
        self.assertIn("export VOICE_SPK_VOLUME=200", text)
        self.assertIn("export VOICE_GAIN_DB=1", text)
        self.assertIn("export VOICE_SECONDS=4", text)
        self.assertIn("export VOICE_WAKE_SECONDS=2", text)
        self.assertIn("export VOICE_COMMAND_SECONDS=5", text)
        self.assertIn("export VOICE_SESSION_SECONDS=60", text)
        self.assertIn('export WAKE_ACK_TEXT="我在"', text)


if __name__ == "__main__":
    unittest.main()
